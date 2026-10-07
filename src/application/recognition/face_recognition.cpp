#include "application/recognition/face_recognition.hpp"

#include <algorithm>
#include <cassert>

#include "application/solutions/line_crossing/line_crossing_solution.hpp"
#include "domain/vision/face_association.hpp"
namespace vqec::app {
FaceRecognition::FaceRecognition(FaceRecognitionOptions options,
                                 std::vector<domain::FaceGallerySample> gallery)
    : options_(options), gallery_(std::move(gallery)) {
    assert(options_.max_batch > 0 && options_.max_batch <= 4);
}
void FaceRecognition::annotate(const domain::Frame& frame,
                               ports::FrameAnnotations& annotations) const {
    annotations.employee_ids.clear();
    for (const auto& track : annotations.tracks) {
        if (track.label != "person" || track.state != domain::TrackState::Confirmed)
            continue;
        const auto found = states_.find(track.id);
        if (found != states_.end() && !found->second.confirmed.empty())
            annotations.employee_ids.emplace(track.id, found->second.confirmed);
    }
    std::vector<domain::Event> attendance;
    for (const auto& crossing : annotations.events) {
        if (crossing.type != LineCrossingSolution::kCrossEvent || !crossing.track_id)
            continue;
        const auto direction =
            crossing.attributes.find(std::string(LineCrossingSolution::kDirectionAttribute));
        if (direction == crossing.attributes.end() ||
            direction->second != domain::to_string(domain::CrossingDirection::Forward))
            continue;
        const auto employee = annotations.employee_ids.find(*crossing.track_id);
        if (employee == annotations.employee_ids.end())
            continue;
        auto event = crossing;
        event.type = kAttendanceEvent;
        event.attributes[kEmployeeIdAttribute] = employee->second;
        event.attributes["pts_ms"] = std::to_string(
            std::chrono::duration_cast<std::chrono::milliseconds>(frame.pts).count());
        attendance.push_back(std::move(event));
    }
    for (auto& event : attendance)
        annotations.events.push_back(std::move(event));
}
void FaceRecognition::reset() {
    states_.clear();
    deferred_.clear();
}
std::vector<FaceRecognitionRequest> FaceRecognition::select(
    const domain::Frame& frame, const std::vector<domain::Track>& tracks,
    const std::vector<domain::Detection>& faces) {
    std::unordered_set<domain::TrackId> visible;
    for (const auto& t : tracks)
        visible.insert(t.id);
    for (auto it = states_.begin(); it != states_.end();)
        if (visible.count(it->first) == 0) {
            deferred_.erase(it->first);
            it = states_.erase(it);
        } else
            ++it;
    auto owners = domain::associate_faces(tracks, faces);
    std::vector<std::size_t> candidates;
    std::unordered_set<domain::TrackId> owned;
    for (std::size_t i = 0; i < faces.size(); ++i) {
        const auto& f = faces[i];
        if (!owners[i]) {
            ++stats_.unowned;
            continue;
        }
        if (!f.landmarks || f.box.width < options_.minimum_width ||
            f.box.height < options_.minimum_height ||
            f.box.width * f.box.height < options_.minimum_area) {
            ++stats_.source_rejected;
            continue;
        }
        const auto id = *owners[i];
        owned.insert(id);
        if (states_.count(id) == 0 && states_.size() >= options_.max_tracks)
            continue;
        auto& state = states_[id];
        if (!state.confirmed.empty() && state.pending.empty() && state.has_embedding &&
            frame.pts >= state.last_embedding &&
            frame.pts - state.last_embedding < options_.refresh_interval)
            continue;
        candidates.push_back(i);
    }
    for (auto& entry : states_)
        if (owned.count(entry.first) == 0) {
            entry.second.pending.clear();
            entry.second.hits = 0;
            deferred_.erase(entry.first);
        }
    std::sort(candidates.begin(), candidates.end(), [&](auto a, auto b) {
        const bool da = deferred_.count(*owners[a]) != 0, db = deferred_.count(*owners[b]) != 0;
        if (da != db)
            return da;
        const float qa = faces[a].box.width * faces[a].box.height * faces[a].score,
                    qb = faces[b].box.width * faces[b].box.height * faces[b].score;
        return qa == qb ? *owners[a] < *owners[b] : qa > qb;
    });
    std::vector<FaceRecognitionRequest> jobs;
    for (auto index : candidates) {
        auto id = *owners[index];
        if (jobs.size() >= options_.max_batch) {
            ++stats_.deferred;
            deferred_.insert(id);
            continue;
        }
        deferred_.erase(id);
        jobs.push_back({id, {faces[index].box, faces[index].landmarks}});
    }
    return jobs;
}
std::vector<domain::Event> FaceRecognition::apply(
    const domain::Frame& frame, const std::vector<FaceRecognitionRequest>& requests,
    const ports::EmbeddingBatch& results) {
    std::vector<domain::Event> events;
    if (results.size() != requests.size())
        return events;
    for (std::size_t i = 0; i < requests.size(); ++i) {
        auto found = states_.find(requests[i].track_id);
        if (found == states_.end())
            continue;
        auto& state = found->second;
        state.last_embedding = frame.pts;
        state.has_embedding = true;
        if (!results[i].ok()) {
            state.pending.clear();
            state.hits = 0;
            continue;
        }
        auto matched = domain::match_face(gallery_, results[i].value());
        if (!matched.ok() || matched.value().employee_id.empty() ||
            matched.value().score < options_.match_threshold) {
            state.pending.clear();
            state.hits = 0;
            continue;
        }
        const auto& match = matched.value();
        const bool weak =
            match.second_score && match.score - *match.second_score < options_.minimum_gap;
        const bool change = !state.confirmed.empty() && state.confirmed != match.employee_id;
        if (weak || change) {
            if (state.pending == match.employee_id)
                ++state.hits;
            else {
                state.pending = match.employee_id;
                state.hits = 1;
            }
            if (state.hits < 2)
                continue;
        }
        const bool newly = state.confirmed != match.employee_id;
        state.confirmed = match.employee_id;
        state.pending.clear();
        state.hits = 0;
        if (newly) {
            domain::Event event;
            event.type = kRecognizedEvent;
            event.stream_id = frame.stream_id;
            event.frame_sequence = frame.sequence;
            event.occurred_at = frame.captured_at;
            event.track_id = requests[i].track_id;
            event.attributes = {
                {"employee_id", match.employee_id},
                {"score", std::to_string(match.score)},
                {"confirmation", weak || change ? "two_observations" : "clear_gap"},
                {"pts_ms",
                 std::to_string(
                     std::chrono::duration_cast<std::chrono::milliseconds>(frame.pts).count())}};
            if (match.second_score) {
                event.attributes["second_score"] = std::to_string(*match.second_score);
                event.attributes["gap"] = std::to_string(match.score - *match.second_score);
            }
            events.push_back(std::move(event));
        }
    }
    return events;
}
}  // namespace vqec::app
