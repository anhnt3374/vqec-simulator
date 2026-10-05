#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "common/result.hpp"
#include "domain/vision/embedding.hpp"

namespace vqec::ports {

struct VectorRecord {
    std::string id;
    domain::Embedding embedding;
    std::map<std::string, std::string> payload;  // e.g. {"person_id": "...", "name": "..."}.
};

struct VectorMatch {
    std::string id;
    float score = 0.0F;  // Cosine similarity, higher is closer.
    std::map<std::string, std::string> payload;
};

// Vector database (Qdrant, in-memory). Thread-safe; may block on I/O, so pipeline code calls it
// through an asynchronous service, never directly per frame.
// A collection holds embeddings of exactly one model: name it "<purpose>_<model_id>".
class IVectorStore {
public:
    virtual ~IVectorStore() = default;

    // Creates the collection if missing. InvalidArgument if it exists with another dimension.
    virtual Status ensure_collection(std::string_view collection, std::size_t dimension) = 0;

    virtual Status upsert(std::string_view collection, const VectorRecord& record) = 0;

    virtual Result<std::vector<VectorMatch>> search(std::string_view collection,
                                                    const domain::Embedding& query,
                                                    std::size_t top_k) const = 0;

    virtual Status remove(std::string_view collection, std::string_view id) = 0;
};

}  // namespace vqec::ports
