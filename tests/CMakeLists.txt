find_package(GTest REQUIRED)
include(GoogleTest)

# Unit tests: no network, GPU, real media or sleeps. Mirrors src/ under unit/.
add_executable(vqec_unit_tests
    unit/adapters/outbound/inference/tensor_test.cpp
    unit/adapters/outbound/inference/models/detectors_test.cpp
    unit/adapters/outbound/inference/models/edgeface_extractor_test.cpp
    unit/application/line_crossing_solution_test.cpp
    unit/application/stream_pipeline_test.cpp
    unit/application/live_line_monitor_test.cpp
    unit/application/live_zone_monitor_test.cpp
    unit/application/live_ai_pipeline_test.cpp
    unit/application/ai_simulation_budget_test.cpp
    unit/application/recognition/face_recognition_test.cpp
    unit/application/zone_intrusion_solution_test.cpp
    unit/bootstrap/app_config_test.cpp
    unit/bootstrap/live_ai_config_test.cpp
    unit/bootstrap/face_gallery_io_test.cpp
    unit/bootstrap/live_line_bridge_test.cpp
    unit/common/bounded_queue_test.cpp
    unit/common/uri_test.cpp
    unit/domain/gate_test.cpp
    unit/domain/polygon_test.cpp)
target_include_directories(vqec_unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vqec_unit_tests PRIVATE
    vqec_bootstrap
    vqec_inference_engine
    vqec_detection_models
    GTest::gtest_main
    GTest::gmock)
target_compile_definitions(vqec_unit_tests PRIVATE
    VQEC_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
vqec_set_warnings(vqec_unit_tests)
if(VQEC_WITH_FFMPEG)
    target_sources(vqec_unit_tests PRIVATE unit/adapters/outbound/media/ffmpeg/overlay_renderer_test.cpp)
    target_link_libraries(vqec_unit_tests PRIVATE vqec_ffmpeg)
endif()
if(VQEC_WITH_BYTETRACK)
    target_sources(vqec_unit_tests PRIVATE unit/adapters/outbound/tracking/bytetrack/byte_tracker_test.cpp)
    target_link_libraries(vqec_unit_tests PRIVATE vqec_bytetrack)
endif()
gtest_discover_tests(vqec_unit_tests PROPERTIES LABELS unit)

# Integration tests (real adapters, label "integration") are added here per adapter, e.g.
#   if(VQEC_WITH_SQLITE) add_subdirectory(integration/persistence/sqlite) endif()

if(VQEC_WITH_FFMPEG AND VQEC_WITH_ONNXRUNTIME)
    find_package(OpenCV REQUIRED COMPONENTS core imgcodecs)
    add_executable(vqec_live_ai_integration_tests integration/inference/live_ai_models_test.cpp integration/inference/edgeface_model_test.cpp integration/bootstrap/face_gallery_io_test.cpp)
    target_include_directories(vqec_live_ai_integration_tests SYSTEM PRIVATE ${OpenCV_INCLUDE_DIRS})
    target_compile_definitions(vqec_live_ai_integration_tests PRIVATE VQEC_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    target_link_libraries(vqec_live_ai_integration_tests PRIVATE vqec_bootstrap vqec_detection_models vqec_onnxruntime vqec_ffmpeg GTest::gtest_main ${OpenCV_LIBS})
    if(VQEC_WITH_BYTETRACK)
        target_sources(vqec_live_ai_integration_tests PRIVATE integration/tracking/person_tracking_test.cpp)
        target_link_libraries(vqec_live_ai_integration_tests PRIVATE vqec_bytetrack)
    endif()
    vqec_set_warnings(vqec_live_ai_integration_tests)
    gtest_discover_tests(vqec_live_ai_integration_tests PROPERTIES LABELS integration TIMEOUT 45)
endif()

add_executable(vqec_board_simulation_tests integration/application/board_simulation_test.cpp)
target_include_directories(vqec_board_simulation_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vqec_board_simulation_tests PRIVATE vqec_application GTest::gtest_main)
vqec_set_warnings(vqec_board_simulation_tests)
gtest_discover_tests(vqec_board_simulation_tests PROPERTIES LABELS integration TIMEOUT 15)
