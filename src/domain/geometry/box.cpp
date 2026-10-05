#include "domain/geometry/box.hpp"

#include <cassert>

namespace vqec::domain {

Point2f anchor_of(const BoxF& box, AnchorPoint anchor) {
    const float center_x = box.x + box.width / 2.0F;
    switch (anchor) {
        case AnchorPoint::Center:
            return {center_x, box.y + box.height / 2.0F};
        case AnchorPoint::BottomCenter:
            return {center_x, box.y + box.height};
    }
    assert(false && "unhandled AnchorPoint");
    return {center_x, box.y + box.height / 2.0F};
}

Point2f normalize(Point2f pixel, int frame_width, int frame_height) {
    assert(frame_width > 0 && frame_height > 0);
    return {pixel.x / static_cast<float>(frame_width), pixel.y / static_cast<float>(frame_height)};
}

}  // namespace vqec::domain
