find_package(GTest REQUIRED)
include(GoogleTest)

# Unit tests: no network, GPU, real media or sleeps. Mirrors src/ under unit/.
add_executable(vqec_unit_tests
    unit/adapters/outbound/inference/tensor_test.cpp
    unit/application/line_crossing_solution_test.cpp
    unit/application/stream_pipeline_test.cpp
    unit/application/zone_intrusion_solution_test.cpp
    unit/bootstrap/app_config_test.cpp
    unit/common/bounded_queue_test.cpp
    unit/common/uri_test.cpp
    unit/domain/gate_test.cpp
    unit/domain/polygon_test.cpp)
target_include_directories(vqec_unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vqec_unit_tests PRIVATE
    vqec_bootstrap
    vqec_inference_engine
    GTest::gtest_main
    GTest::gmock)
target_compile_definitions(vqec_unit_tests PRIVATE
    VQEC_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
vqec_set_warnings(vqec_unit_tests)
gtest_discover_tests(vqec_unit_tests PROPERTIES LABELS unit)

# Integration tests (real adapters, label "integration") are added here per adapter, e.g.
#   if(VQEC_WITH_SQLITE) add_subdirectory(integration/persistence/sqlite) endif()
