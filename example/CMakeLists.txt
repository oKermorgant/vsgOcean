# Minimal example: load a scene (SDF world when vsgOcean is built with SDF support, otherwise a model file) and render it with vsg

# Executable
add_executable(vsgOcean_example src/main.cpp)

target_compile_definitions(vsgOcean_example PRIVATE vsgXchange_FOUND)

# Link libraries - using target-based linking; the GUI, when built, comes with vsgOcean
# the semantic image export uses zlib's PNG writer
find_package(ZLIB REQUIRED)
target_link_libraries(vsgOcean_example PRIVATE
  ZLIB::ZLIB
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

# The tutorial: the smallest complete marine scene (see src/tutorial.cpp)
add_executable(vsgOcean_tutorial src/tutorial.cpp)
target_compile_definitions(vsgOcean_tutorial PRIVATE vsgXchange_FOUND)
target_link_libraries(vsgOcean_tutorial PRIVATE
  vsgOcean::vsgOcean
    vsgXchange::vsgXchange
)
if (NOT VSGOCEAN_EMBED_RESOURCES)
    target_compile_definitions(vsgOcean_tutorial PRIVATE
        VSGOCEAN_EXAMPLE_MODELS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/models")
endif ()
