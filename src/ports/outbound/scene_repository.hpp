#pragma once

#include "common/result.hpp"
#include "domain/ids.hpp"
#include "domain/scene/scene.hpp"

namespace vqec::ports {

// SQL persistence of zones and gates per stream. Thread-safe; may block on I/O, so never call
// it from a pipeline thread.
class ISceneRepository {
public:
    virtual ~ISceneRepository() = default;

    // NotFound when nothing is stored for the stream yet.
    virtual Result<domain::Scene> load(const domain::StreamId& stream_id) const = 0;

    // Replaces the whole scene of the stream atomically.
    virtual Status store(const domain::StreamId& stream_id, const domain::Scene& scene) = 0;
};

}  // namespace vqec::ports
