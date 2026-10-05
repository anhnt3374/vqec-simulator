# Use cases: stream pipeline, solutions, services implementing inbound ports.
add_library(vqec_application
    pipeline/stream_pipeline.cpp
    solutions/solution_utils.cpp
    solutions/line_crossing/line_crossing_solution.cpp
    solutions/zone_intrusion/zone_intrusion_solution.cpp)
target_link_libraries(vqec_application PUBLIC vqec_ports vqec_log)
vqec_set_warnings(vqec_application)
