# Build options. Presets in CMakePresets.json set these per runtime; keep defaults safe for a
# CPU-only developer build.

option(VQEC_BUILD_TESTS "Build unit tests" ON)
option(VQEC_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(VQEC_ENABLE_SANITIZERS "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

set(VQEC_RUNTIME "cpu" CACHE STRING "Target runtime class: cpu, gpu or npu")
set_property(CACHE VQEC_RUNTIME PROPERTY STRINGS cpu gpu npu)
if(NOT VQEC_RUNTIME MATCHES "^(cpu|gpu|npu)$")
    message(FATAL_ERROR "VQEC_RUNTIME must be cpu, gpu or npu (got '${VQEC_RUNTIME}')")
endif()

# Adapter switches. Each one adds src/adapters/<path> (see src/adapters/CMakeLists.txt).
# Defaults stay OFF until the adapter is implemented; then enable it in the matching presets.
option(VQEC_WITH_FFMPEG "Media adapter: RTSP/MP4 sources and sinks via FFmpeg" OFF)
option(VQEC_WITH_DROGON "HTTP adapter: REST, WebSocket and WebUI via Drogon" OFF)
option(VQEC_WITH_SQLITE "Persistence adapter: SQLite" OFF)
option(VQEC_WITH_POSTGRES "Persistence adapter: PostgreSQL" OFF)
option(VQEC_WITH_QDRANT "Vector store adapter: Qdrant over REST" OFF)
option(VQEC_WITH_BYTETRACK "Tracking adapter: ByteTrack" OFF)
option(VQEC_WITH_ONNXRUNTIME "Inference backend: ONNX Runtime (cpu, optional CUDA EP)" OFF)
option(VQEC_WITH_TENSORRT "Inference backend: TensorRT (gpu)" OFF)
option(VQEC_WITH_OPENVINO "Inference backend: OpenVINO (cpu/igpu)" OFF)

if(VQEC_ENABLE_SANITIZERS)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()
