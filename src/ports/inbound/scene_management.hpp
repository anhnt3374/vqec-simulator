#pragma once

#include "common/result.hpp"
#include "domain/ids.hpp"
#include "domain/scene/scene.hpp"

namespace vqec::ports {

// Driving port for editing zones and gates (WebUI zone editor). Thread-safe.
class ISceneManagement {
public:
    virtual ~ISceneManagement() = default;

    virtual Result<domain::Scene> get_scene(const domain::StreamId& stream_id) const = 0;

    // Persists the scene and applies it to the running pipeline before its next frame.
    virtual Status update_scene(const domain::StreamId& stream_id, domain::Scene scene) = 0;
};

}  // namespace vqec::ports
