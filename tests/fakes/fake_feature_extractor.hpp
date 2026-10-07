#pragma once
#include <thread>

#include "ports/outbound/feature_extractor.hpp"
namespace vqec::test {
class FakeFeatureExtractor final : public ports::IFeatureExtractor {
public:
    bool fail = false;
    std::size_t calls = 0;
    std::thread::id worker;
    Result<domain::Embedding> extract(const domain::Frame&, const domain::BoxF&) override {
        return make_error(ErrorCode::Unsupported, "use batch");
    }
    Result<ports::EmbeddingBatch> extract_batch(
        const domain::Frame&, const std::vector<ports::FeatureRegion>& regions) override {
        ++calls;
        worker = std::this_thread::get_id();
        if (fail)
            return make_error(ErrorCode::Internal, "scripted embedding error");
        ports::EmbeddingBatch outputs;
        for (std::size_t i = 0; i < regions.size(); ++i) {
            std::vector<float> values(512, 0);
            values[0] = 1;
            outputs.emplace_back(domain::Embedding{"edgeface_s_gamma_05_v1", std::move(values)});
        }
        return outputs;
    }
};
}  // namespace vqec::test
