#pragma once

#include <string>
#include <vector>

#include "ports/outbound/object_detector.hpp"

namespace vqec::test {

// Returns no detections; pair it with FakeObjectTracker, which scripts the tracks directly.
class FakeObjectDetector final : public ports::IObjectDetector {
public:
    Result<std::vector<domain::Detection>> detect(const domain::Frame& /*frame*/) override {
        ++calls_;
        return std::vector<domain::Detection>{};
    }

    const std::vector<std::string>& labels() const override { return labels_; }

    int calls() const { return calls_; }

private:
    std::vector<std::string> labels_{"person"};
    int calls_ = 0;
};

}  // namespace vqec::test
