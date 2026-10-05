# Contracts shared by model codecs (models/) and backends (runtimes/). Header-only, no deps.
add_library(vqec_inference_engine INTERFACE)
target_link_libraries(vqec_inference_engine INTERFACE vqec_common)
