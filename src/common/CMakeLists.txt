find_package(Threads REQUIRED)
find_package(spdlog REQUIRED)

# Header-only utilities usable from every layer, including the domain.
add_library(vqec_common INTERFACE)
target_include_directories(vqec_common INTERFACE ${PROJECT_SOURCE_DIR}/src)
target_compile_features(vqec_common INTERFACE cxx_std_17)
target_link_libraries(vqec_common INTERFACE Threads::Threads)

# Logging facade. Not available to the domain layer.
add_library(vqec_log log.cpp)
target_link_libraries(vqec_log PUBLIC vqec_common spdlog::spdlog)
# Compile every level in; the runtime level from config decides what is emitted.
target_compile_definitions(vqec_log PUBLIC SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)
vqec_set_warnings(vqec_log)
