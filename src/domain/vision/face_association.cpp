#include "domain/vision/face_association.hpp"

#include <cmath>
#include <map>
namespace vqec::domain {
std::vector<std::optional<TrackId>> associate_faces(const std::vector<Track>& tracks,
                                                    const std::vector<Detection>& faces) {
    std::vector<std::optional<TrackId>> owners(faces.size());
    std::map<TrackId, std::size_t> counts;
    for (std::size_t i = 0; i < faces.size(); ++i) {
        const auto& box = faces[i].box;
        const float x = box.x + box.width / 2, y = box.y + box.height / 2;
        if (faces[i].label != "face" || !std::isfinite(x) || !std::isfinite(y) || box.width <= 0 ||
            box.height <= 0)
            continue;
        std::size_t candidates = 0;
        for (const auto& t : tracks) {
            if (t.state != TrackState::Confirmed || t.label != "person" || t.id == 0)
                continue;
            if (x >= t.box.x && x <= t.box.x + t.box.width && y >= t.box.y &&
                y <= t.box.y + t.box.height / 2) {
                ++candidates;
                owners[i] = t.id;
            }
        }
        if (candidates != 1)
            owners[i].reset();
        else
            ++counts[*owners[i]];
    }
    for (auto& owner : owners)
        if (owner && counts[*owner] != 1)
            owner.reset();
    return owners;
}
}  // namespace vqec::domain
