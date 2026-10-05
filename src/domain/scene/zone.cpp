#include "domain/scene/zone.hpp"

#include <algorithm>
#include <utility>

namespace vqec::domain {

Result<Zone> make_zone(ZoneId id, std::string name, std::vector<Point2f> vertices) {
    if (id.empty()) {
        return make_error(ErrorCode::InvalidArgument, "zone id must not be empty");
    }
    if (!std::all_of(vertices.begin(), vertices.end(), is_normalized)) {
        return make_error(ErrorCode::InvalidArgument,
                          "zone '" + id + "' has vertices outside normalized [0, 1] coordinates");
    }
    Result<Polygon> area = Polygon::create(std::move(vertices));
    if (!area.ok()) {
        return make_error(area.error().code, "zone '" + id + "': " + area.error().message);
    }
    return Zone{std::move(id), std::move(name), std::move(area).value()};
}

}  // namespace vqec::domain
