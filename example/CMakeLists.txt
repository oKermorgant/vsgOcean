# Minimal example: load a scene (SDF world when vsgOcean is built with SDF support, otherwise a model file) and render it with vsg

# Executable
add_executable(vsgOcean_example src/main.cpp)

target_compile_definitions(vsgOcean_example PRIVATE vsgXchange_FOUND)

# Link libraries - using target-based linking
target_link_libraries(vsgOcean_example PRIVATE
    vsgOcean::vsgOcean
    vsgXchange::vsgXchange
)

# Example resources: by default the executable reads the models directly from the source
# tree, so edits to the SDF files apply without any copy step. Set VSGOCEAN_EMBED_RESOURCES=ON
# to copy the models to the build directory instead (for standalone / installed runs).
option(VSGOCEAN_EMBED_RESOURCES "Copy the example models to the build directory" OFF)

if (VSGOCEAN_EMBED_RESOURCES)
    add_custom_command(TARGET vsgOcean_example POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/models
            $<TARGET_FILE_DIR:vsgOcean_example>/models)
else ()
    target_compile_definitions(vsgOcean_example PRIVATE
        VSGOCEAN_EXAMPLE_MODELS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/models")
endif ()
