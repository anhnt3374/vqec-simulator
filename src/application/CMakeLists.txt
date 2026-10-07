# Use cases: stream pipeline, solutions, services implementing inbound ports.
add_library(vqec_application
    recognition/face_recognition.cpp
    pipeline/stream_pipeline.cpp
    pipeline/live_ai_pipeline.cpp
    pipeline/live_line_monitor.cpp
    pipeline/live_zone_monitor.cpp
    solutions/solution_utils.cpp
    solutions/line_crossing/line_crossing_solution.cpp
    solutions/zone_intrusion/zone_intrusion_solution.cpp)
target_link_libraries(vqec_application PUBLIC vqec_ports vqec_log)
vqec_set_warnings(vqec_application)
