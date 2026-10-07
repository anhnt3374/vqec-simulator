#include <algorithm>
#include <cstring>
#include <filesystem>
#include <map>

#include <opencv2/imgcodecs.hpp>

#include "adapters/outbound/inference/models/edgeface_extractor.hpp"
#include "adapters/outbound/inference/models/yunet_detector.hpp"
#include "adapters/outbound/inference/runtimes/onnxruntime/onnx_runtime_engine.hpp"
#include "bootstrap/face_gallery_io.hpp"
#include "bootstrap/live_ai_config.hpp"
#include "common/log.hpp"
namespace {
int run(int argc, char** argv) {
    using namespace vqec;
    namespace inference = adapters::inference;
    std::map<std::string, std::string> args;
    for (int i = 1; i + 1 < argc; i += 2)
        args[argv[i]] = argv[i + 1];
    if (args.count("--images") == 0 || args.count("--output") == 0 ||
        args.count("--models-dir") == 0)
        return 64;
    log::LogConfig logging;
    logging.directory = "/tmp/vqec-gallery-logs";
    auto status = log::init(logging);
    if (!status.ok())
        return 1;
    const auto models = std::filesystem::path(args["--models-dir"]);
    status = bootstrap::verify_live_ai_models(models);
    if (status.ok())
        status = bootstrap::verify_edgeface_model(models / "edgeface_s_gamma_05_batch4.onnx");
    if (!status.ok()) {
        VQEC_LOG_ERROR("[gallery] model verification code={} message={}",
                       to_string(status.error().code), status.error().message);
        return 1;
    }
    auto detector_engine = std::make_unique<inference::OnnxRuntimeEngine>();
    auto embedder_engine = std::make_unique<inference::OnnxRuntimeEngine>();
    status = detector_engine->load({(models / "yunet_2023mar.onnx").string(), {}, 0, 1});
    if (status.ok())
        status = embedder_engine->load(
            {(models / "edgeface_s_gamma_05_batch4.onnx").string(), {}, 0, 1});
    if (!status.ok()) {
        VQEC_LOG_ERROR("[gallery] model load failed message={}", status.error().message);
        return 1;
    }
    inference::YuNetDetector detector(std::move(detector_engine));
    inference::EdgeFaceExtractor embedder(std::move(embedder_engine));
    std::vector<std::filesystem::path> employees;
    for (const auto& entry : std::filesystem::directory_iterator(args["--images"]))
        if (entry.is_directory())
            employees.push_back(entry.path());
    std::sort(employees.begin(), employees.end());
    std::map<std::string, std::size_t> rejected;
    const app::FaceRecognitionOptions quality;
    std::vector<domain::FaceGallerySample> samples;
    std::size_t skipped = 0, unusable = 0, read = 0;
    for (const auto& employee : employees) {
        std::vector<std::filesystem::path> images;
        for (const auto& entry : std::filesystem::directory_iterator(employee))
            if (entry.is_regular_file())
                images.push_back(entry.path());
        std::sort(images.begin(), images.end());
        std::size_t accepted = 0;
        for (const auto& path : images) {
            if (accepted >= 12)
                break;
            ++read;
            const auto image = cv::imread(path.string());
            if (image.empty()) {
                ++skipped;
                ++rejected["unreadable"];
                continue;
            }
            auto buffer = std::make_shared<domain::ImageBuffer>();
            buffer->width = image.cols;
            buffer->height = image.rows;
            buffer->stride_bytes = image.cols * 3;
            buffer->data.resize(static_cast<std::size_t>(buffer->stride_bytes) *
                                static_cast<std::size_t>(buffer->height));
            for (int y = 0; y < image.rows; ++y)
                std::memcpy(
                    buffer->data.data() + static_cast<std::size_t>(y) *
                                              static_cast<std::size_t>(buffer->stride_bytes),
                    image.ptr(y), static_cast<std::size_t>(buffer->stride_bytes));
            domain::Frame frame;
            frame.width = image.cols;
            frame.height = image.rows;
            frame.image = buffer;
            auto faces = detector.detect(frame);
            if (!faces.ok() || faces.value().size() != 1) {
                ++rejected[!faces.ok()             ? "detector_error"
                           : faces.value().empty() ? "no_face"
                                                   : "multiple_faces"];
                ++skipped;
                continue;
            }
            const auto& face = faces.value()[0];
            if (face.box.width < quality.minimum_width ||
                face.box.height < quality.minimum_height ||
                face.box.width * face.box.height < quality.minimum_area) {
                ++rejected["too_small"];
                ++skipped;
                continue;
            }
            auto embeddings = embedder.extract_batch(frame, {{face.box, face.landmarks}});
            if (!embeddings.ok() || !embeddings.value()[0].ok()) {
                ++rejected[!embeddings.ok() ? "embedding_error" : "alignment_rejected"];
                ++skipped;
                continue;
            }
            samples.push_back(
                {employee.filename().string(), std::move(embeddings.value()[0]).value()});
            ++accepted;
        }
        if (accepted == 0) {
            ++unusable;
            VQEC_LOG_WARN("[gallery] employee_id={} usable_samples=0",
                          employee.filename().string());
        }
    }
    for (const auto& reason : rejected)
        VQEC_LOG_INFO("[gallery] rejected reason={} images={}", reason.first, reason.second);
    status = bootstrap::write_face_gallery(args["--output"], samples);
    if (!status.ok()) {
        VQEC_LOG_ERROR("[gallery] save failed message={}", status.error().message);
        return 1;
    }
    VQEC_LOG_INFO(
        "[gallery] built employees_total={} employees_usable={} samples={} images_read={} "
        "images_skipped={} employees_unusable={}",
        employees.size(), employees.size() - unusable, samples.size(), read, skipped, unusable);
    return 0;
}
}  // namespace
int main(int argc, char** argv) {
    try {
        const int code = run(argc, argv);
        vqec::log::shutdown();
        return code;
    } catch (const std::exception&) {
        VQEC_LOG_ERROR("[gallery] enrollment failed");
        vqec::log::shutdown();
        return 1;
    }
}
