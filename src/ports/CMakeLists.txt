# Hexagon boundary: interfaces and DTOs only, so the target is header-only.
add_library(vqec_ports INTERFACE)
target_link_libraries(vqec_ports INTERFACE vqec_domain)
