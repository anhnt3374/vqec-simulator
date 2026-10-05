find_package(yaml-cpp REQUIRED)

# Expose adapter switches to C++ as true/false for build_info.hpp.
foreach(feature ONNXRUNTIME TENSORRT OPENVINO FFMPEG DROGON SQLITE POSTGRES QDRANT BYTETRACK)
    if(VQEC_WITH_${feature})
        set(VQEC_WITH_${feature}_CXX true)
    else()
        set(VQEC_WITH_${feature}_CXX false)
    endif()
endforeach()
configure_file(build_info.hpp.in ${CMAKE_CURRENT_BINARY_DIR}/generated/bootstrap/build_info.hpp
               @ONLY)

# Config parsing and the solution factory live in a library so tests can link them.
add_library(vqec_bootstrap
    app_config.cpp
    solution_factory.cpp)
target_include_directories(vqec_bootstrap PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(vqec_bootstrap PUBLIC vqec_application yaml-cpp)
vqec_set_warnings(vqec_bootstrap)

add_executable(vqec_simulator main.cpp)
target_link_libraries(vqec_simulator PRIVATE vqec_bootstrap)
vqec_set_warnings(vqec_simulator)

install(TARGETS vqec_simulator RUNTIME DESTINATION bin)
