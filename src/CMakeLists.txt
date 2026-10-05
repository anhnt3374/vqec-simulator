# Layers are added inner to outer; each target links only the layers it may depend on
# (see AGENTS.md section 4). scripts/check_layers.sh enforces the include rules.
add_subdirectory(common)
add_subdirectory(domain)
add_subdirectory(ports)
add_subdirectory(application)
add_subdirectory(adapters)
add_subdirectory(bootstrap)
