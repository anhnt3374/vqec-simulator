cmake_minimum_required(VERSION 3.22)

project(vqec_simulator
    VERSION 0.1.0
    DESCRIPTION "Video analytics simulator: RTSP/MP4 in, AI inference + business solutions, events out"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(vqec_options)
include(vqec_warnings)

add_subdirectory(src)

if(VQEC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

install(DIRECTORY web/ DESTINATION share/vqec/web)
install(DIRECTORY db/migrations/ DESTINATION share/vqec/db/migrations)
