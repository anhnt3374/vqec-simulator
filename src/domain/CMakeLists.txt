# Pure business model: no I/O, no threads, no third-party dependencies.
add_library(vqec_domain
    geometry/box.cpp
    geometry/polygon.cpp
    scene/gate.cpp
    scene/zone.cpp)
target_link_libraries(vqec_domain PUBLIC vqec_common)
vqec_set_warnings(vqec_domain)
