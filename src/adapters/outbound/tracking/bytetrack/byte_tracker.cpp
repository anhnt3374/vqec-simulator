#include "adapters/outbound/tracking/bytetrack/byte_tracker.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

#include <opencv2/core.hpp>

#include "common/log.hpp"
namespace vqec::adapters::bytetrack {
namespace {
// Algorithm reference: FoundationVision/ByteTrack yolox/tracker (MIT).
// Native implementation uses PTS-based dt/expiry and bounded track capacity.
constexpr double kPositionNoise = 1.0 / 20.0;
constexpr double kVelocityNoise = 1.0 / 160.0;
constexpr double kReferenceFps = 30.0;
constexpr double kForbiddenCost = 1e6;
constexpr float kDuplicateIou = 0.85F;
using State = cv::Vec<double, 8>;
using Covariance = cv::Matx<double, 8, 8>;
using Measurement = cv::Vec<double, 4>;
struct Motion {
    State mean{};
    Covariance covariance{};
    static Measurement measure(const domain::BoxF& box) {
        return {static_cast<double>(box.x) + static_cast<double>(box.width) / 2.0,
                static_cast<double>(box.y) + static_cast<double>(box.height) / 2.0,
                static_cast<double>(box.width) / static_cast<double>(box.height),
                static_cast<double>(box.height)};
    }
    explicit Motion(const domain::BoxF& box) {
        const auto m = measure(box);
        for (int i = 0; i < 4; ++i)
            mean[i] = m[i];
        const double h = m[3];
        const std::array<double, 8> noise{
            2 * kPositionNoise * h,  2 * kPositionNoise * h,  1e-2, 2 * kPositionNoise * h,
            10 * kVelocityNoise * h, 10 * kVelocityNoise * h, 1e-5, 10 * kVelocityNoise * h};
        for (int i = 0; i < 8; ++i)
            covariance(i, i) =
                noise[static_cast<std::size_t>(i)] * noise[static_cast<std::size_t>(i)];
    }
    void predict(double dt, bool lost) {
        if (lost)
            mean[7] = 0;
        auto transition = Covariance::eye();
        for (int i = 0; i < 4; ++i)
            transition(i, i + 4) = dt;
        const double h = std::max(1.0, mean[3]);
        const std::array<double, 8> noise{
            kPositionNoise * h, kPositionNoise * h, 1e-2, kPositionNoise * h,
            kVelocityNoise * h, kVelocityNoise * h, 1e-5, kVelocityNoise * h};
        mean = transition * mean;
        covariance = transition * covariance * transition.t();
        for (int i = 0; i < 8; ++i)
            covariance(i, i) +=
                noise[static_cast<std::size_t>(i)] * noise[static_cast<std::size_t>(i)] * dt;
    }
    void correct(const domain::BoxF& box) {
        cv::Matx<double, 4, 4> innovation;
        const double h = std::max(1.0, mean[3]);
        const std::array<double, 4> noise{kPositionNoise * h, kPositionNoise * h, 0.1,
                                          kPositionNoise * h};
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                innovation(r, c) = covariance(r, c);
        for (int i = 0; i < 4; ++i)
            innovation(i, i) +=
                noise[static_cast<std::size_t>(i)] * noise[static_cast<std::size_t>(i)];
        cv::Matx<double, 8, 4> cross;
        for (int r = 0; r < 8; ++r)
            for (int c = 0; c < 4; ++c)
                cross(r, c) = covariance(r, c);
        const auto gain = cross * innovation.inv(cv::DECOMP_CHOLESKY);
        auto residual = measure(box);
        for (int i = 0; i < 4; ++i)
            residual[i] -= mean[i];
        mean += gain * residual;
        covariance -= gain * innovation * gain.t();
    }
    domain::BoxF box() const {
        const double h = std::max(1.0, mean[3]);
        const double w = std::max(1.0, mean[2] * h);
        return {static_cast<float>(mean[0] - w / 2), static_cast<float>(mean[1] - h / 2),
                static_cast<float>(w), static_cast<float>(h)};
    }
};
float iou(const domain::BoxF& a, const domain::BoxF& b) {
    const auto width = std::max(0.0F, std::min(a.x + a.width, b.x + b.width) - std::max(a.x, b.x));
    const auto height =
        std::max(0.0F, std::min(a.y + a.height, b.y + b.height) - std::max(a.y, b.y));
    const auto intersection = width * height;
    const auto total = a.width * a.height + b.width * b.height - intersection;
    return total > 0 ? intersection / total : 0.0F;
}
// Rectangular Hungarian assignment with one unmatched column per track. Dummy costs implement
// the cost limit inside the optimization, instead of rejecting an unconstrained match.
std::vector<std::pair<std::size_t, std::size_t>> assign(
    const std::vector<std::vector<double>>& cost, std::size_t columns, double limit) {
    const auto rows = cost.size();
    const auto n = rows + columns;
    if (!rows || !columns)
        return {};
    std::vector<double> u(n + 1), v(n + 1);
    std::vector<std::size_t> p(n + 1), way(n + 1);
    auto value = [&](std::size_t r, std::size_t c) {
        if (r < rows && c < columns)
            return cost[r][c] <= limit ? cost[r][c] : kForbiddenCost;
        return limit + 1e-9;
    };
    for (std::size_t row = 1; row <= rows; ++row) {
        p[0] = row;
        std::size_t column = 0;
        std::vector<double> minimum(n + 1, std::numeric_limits<double>::infinity());
        std::vector<bool> used(n + 1, false);
        do {
            used[column] = true;
            const auto current = p[column];
            double delta = std::numeric_limits<double>::infinity();
            std::size_t next = 0;
            for (std::size_t j = 1; j <= n; ++j)
                if (!used[j]) {
                    const double candidate = value(current - 1, j - 1) - u[current] - v[j];
                    if (candidate < minimum[j]) {
                        minimum[j] = candidate;
                        way[j] = column;
                    }
                    if (minimum[j] < delta) {
                        delta = minimum[j];
                        next = j;
                    }
                }
            for (std::size_t j = 0; j <= n; ++j) {
                if (used[j]) {
                    u[p[j]] += delta;
                    v[j] -= delta;
                } else
                    minimum[j] -= delta;
            }
            column = next;
        } while (p[column] != 0);
        do {
            const auto previous = way[column];
            p[column] = p[previous];
            column = previous;
        } while (column != 0);
    }
    std::vector<std::pair<std::size_t, std::size_t>> matches;
    for (std::size_t j = 1; j <= columns; ++j)
        if (p[j] > 0 && p[j] <= rows && cost[p[j] - 1][j - 1] <= limit)
            matches.emplace_back(p[j] - 1, j - 1);
    return matches;
}
bool valid(const domain::Detection& detection) {
    const auto& b = detection.box;
    return detection.label == "person" && std::isfinite(detection.score) &&
           detection.score <= 1.0F && std::isfinite(b.x) && std::isfinite(b.y) &&
           std::isfinite(b.width) && std::isfinite(b.height) && b.width > 0 && b.height > 0;
}
}  // namespace
struct ByteTracker::Impl {
    struct Entry {
        domain::Track track;
        Motion motion;
        std::chrono::nanoseconds born, seen;
        bool removed = false;
        Entry(domain::TrackId id, const domain::Detection& d, std::chrono::nanoseconds pts,
              bool first)
            : track{id,      first ? domain::TrackState::Confirmed : domain::TrackState::Tentative,
                    d.box,   d.class_id,
                    d.label, d.score},
              motion(d.box),
              born(pts),
              seen(pts) {}
    };
    ByteTrackerOptions options;
    std::vector<Entry> entries;
    domain::TrackId next_id = 1;
    std::optional<std::chrono::nanoseconds> previous;
    void associate(const std::vector<std::size_t>& pool,
                   const std::vector<domain::Detection>& detections, std::vector<bool>& used,
                   std::vector<bool>& matched, double limit, bool fuse,
                   std::chrono::nanoseconds pts) {
        std::vector<std::size_t> candidates;
        for (std::size_t i = 0; i < detections.size(); ++i)
            if (!used[i])
                candidates.push_back(i);
        std::vector<std::vector<double>> costs(pool.size(), std::vector<double>(candidates.size()));
        for (std::size_t r = 0; r < pool.size(); ++r)
            for (std::size_t c = 0; c < candidates.size(); ++c) {
                const auto& d = detections[candidates[c]];
                const auto& t = entries[pool[r]].track;
                costs[r][c] = t.class_id == d.class_id
                                  ? 1.0 - static_cast<double>(iou(t.box, d.box)) *
                                              (fuse ? static_cast<double>(d.score) : 1.0)
                                  : kForbiddenCost;
            }
        for (const auto& pair : assign(costs, candidates.size(), limit)) {
            const auto index = pool[pair.first], detection = candidates[pair.second];
            auto& entry = entries[index];
            const auto& d = detections[detection];
            entry.motion.correct(d.box);
            entry.track.box = entry.motion.box();
            entry.track.score = d.score;
            entry.track.state = domain::TrackState::Confirmed;
            entry.seen = pts;
            matched[index] = true;
            used[detection] = true;
        }
    }
};
ByteTracker::ByteTracker(ByteTrackerOptions options) : impl_(std::make_unique<Impl>()) {
    assert(options.low_threshold > 0 && options.low_threshold < options.high_threshold &&
           options.high_threshold <= options.new_threshold && options.new_threshold <= 1 &&
           options.lost_timeout.count() > 0 && options.max_tracks > 0);
    impl_->options = options;
    VQEC_LOG_INFO(
        "[tracker=bytetrack] initialized high_threshold={} low_threshold={} lost_timeout_ms={} "
        "max_tracks={}",
        options.high_threshold, options.low_threshold, options.lost_timeout.count(),
        options.max_tracks);
}
ByteTracker::~ByteTracker() = default;
void ByteTracker::reset() {
    impl_->entries.clear();
    impl_->previous.reset();
    VQEC_LOG_INFO("[tracker=bytetrack] reset next_id={}", impl_->next_id);
}
std::vector<domain::Track> ByteTracker::update(const domain::Frame& frame,
                                               const std::vector<domain::Detection>& detections) {
    try {
        auto& state = *impl_;
        const auto pts = frame.pts;
        if (state.previous && pts < *state.previous)
            reset();
        const bool first = !state.previous;
        const double dt =
            state.previous
                ? std::max(0.0, std::chrono::duration<double>(pts - *state.previous).count() *
                                    kReferenceFps)
                : 1.0;
        state.previous = pts;
        state.entries.erase(std::remove_if(state.entries.begin(), state.entries.end(),
                                           [&](const auto& e) {
                                               return pts - e.seen > state.options.lost_timeout;
                                           }),
                            state.entries.end());
        std::vector<domain::Detection> selected;
        for (const auto& d : detections)
            if (valid(d) && d.score >= state.options.low_threshold)
                selected.push_back(d);
        std::stable_sort(selected.begin(), selected.end(),
                         [](const auto& a, const auto& b) { return a.score > b.score; });
        if (selected.size() > state.options.max_tracks)
            selected.resize(state.options.max_tracks);
        std::vector<domain::Detection> high, low;
        for (const auto& d : selected)
            (d.score >= state.options.high_threshold ? high : low).push_back(d);
        std::vector<std::size_t> pool, tentative;
        for (std::size_t i = 0; i < state.entries.size(); ++i) {
            auto& e = state.entries[i];
            if (e.track.state == domain::TrackState::Tentative)
                tentative.push_back(i);
            else {
                e.motion.predict(dt, e.track.state == domain::TrackState::Lost);
                e.track.box = e.motion.box();
                pool.push_back(i);
            }
        }
        std::vector<bool> matched(state.entries.size(), false), used_high(high.size(), false),
            used_low(low.size(), false);
        state.associate(pool, high, used_high, matched, state.options.match_cost, true, pts);
        std::vector<std::size_t> remaining;
        for (const auto i : pool)
            if (!matched[i] && state.entries[i].track.state == domain::TrackState::Confirmed)
                remaining.push_back(i);
        state.associate(remaining, low, used_low, matched, state.options.low_match_cost, false,
                        pts);
        for (const auto i : pool)
            if (!matched[i])
                state.entries[i].track.state = domain::TrackState::Lost;
        state.associate(tentative, high, used_high, matched, state.options.tentative_match_cost,
                        true, pts);
        for (const auto i : tentative)
            if (!matched[i])
                state.entries[i].removed = true;
        for (std::size_t a = 0; a < state.entries.size(); ++a)
            for (std::size_t b = a + 1; b < state.entries.size(); ++b) {
                auto& left = state.entries[a];
                auto& right = state.entries[b];
                if (!left.removed && !right.removed && left.track.state != right.track.state &&
                    (left.track.state == domain::TrackState::Lost ||
                     right.track.state == domain::TrackState::Lost) &&
                    iou(left.track.box, right.track.box) > kDuplicateIou)
                    (left.seen - left.born > right.seen - right.born ? right : left).removed = true;
            }
        state.entries.erase(std::remove_if(state.entries.begin(), state.entries.end(),
                                           [](const auto& e) { return e.removed; }),
                            state.entries.end());
        for (std::size_t i = 0; i < high.size() && state.entries.size() < state.options.max_tracks;
             ++i)
            if (!used_high[i] && high[i].score >= state.options.new_threshold)
                state.entries.emplace_back(state.next_id++, high[i], pts, first);
        std::vector<domain::Track> output;
        output.reserve(state.entries.size());
        for (const auto& e : state.entries)
            output.push_back(e.track);
        return output;
    } catch (...) {
        // Existing tracker port cannot return Status. Fail safely and report once here.
        reset();
        VQEC_LOG_ERROR("[tracker=bytetrack] update failed code=internal tracks discarded");
        return {};
    }
}
}  // namespace vqec::adapters::bytetrack
