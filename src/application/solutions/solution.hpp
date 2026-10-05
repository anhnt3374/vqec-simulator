#pragma once

#include <string_view>
#include <vector>

#include "domain/event/event.hpp"
#include "domain/scene/scene.hpp"
#include "domain/vision/frame.hpp"
#include "domain/vision/track.hpp"

namespace vqec::app {

// Everything a solution sees for one frame. Built once per frame by the pipeline.
struct FrameContext {
    domain::Frame frame;
    std::vector<domain::Track> tracks;
};

// A business use case ("bài toán"): turns tracked objects plus the stream's scene into events.
// Instances belong to one stream and are called only from that stream's pipeline thread, so
// implementations keep per-track state without locks. Shared track-state conventions:
// Tentative tracks are ignored, Lost tracks keep their state, absent tracks are forgotten.
class ISolution {
public:
    virtual ~ISolution() = default;

    // Stable identifier used in config and as the event type prefix, e.g. "zone_intrusion".
    virtual std::string_view type() const = 0;

    // Called before the first frame and whenever the stream's zones/gates change.
    virtual void set_scene(const domain::Scene& scene) = 0;

    virtual std::vector<domain::Event> process(const FrameContext& context) = 0;
};

}  // namespace vqec::app
