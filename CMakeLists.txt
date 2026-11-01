cmake_minimum_required(VERSION 3.22)
project(vsgOcean LANGUAGES CXX)

# Version
set(VERSION 0.1.0)

# C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find dependencies
# sdformat is optional: SDF scene support is enabled when it is found
find_package(sdformat 16.1.0 QUIET)

find_package(Vulkan REQUIRED)
message(STATUS "Found Vulkan: ${Vulkan_DIR}")

find_package(vsg REQUIRED)
message(STATUS "Found vsg: ${vsg_DIR}")

find_package(vsgXchange REQUIRED)
message(STATUS "Found vsgXchange: ${vsgXchange_DIR}")
# vsgXchange/all.h aggregates every reader the library was built with; when it
# exists, export the vsgXchange_all define so consumers (e.g. Coral) register
# all readers in their vsg::Options through the shared target interface
get_target_property(VSGOCEAN_XCHANGE_INCLUDE_DIRS vsgXchange::vsgXchange INTERFACE_INCLUDE_DIRECTORIES)
list(GET VSGOCEAN_XCHANGE_INCLUDE_DIRS 0 VSGOCEAN_XCHANGE_INCLUDE_DIR)
if(EXISTS "${VSGOCEAN_XCHANGE_INCLUDE_DIR}/vsgXchange/all.h")
    set(VSGOCEAN_WITH_XCHANGE_ALL ON)
    message(STATUS "vsgXchange/all.h found: exporting vsgXchange_all")
else()
    set(VSGOCEAN_WITH_XCHANGE_ALL OFF)
endif()

# PNG screenshots
find_package(ZLIB REQUIRED)
message(STATUS "Found ZLIB: ${ZLIB_VERSION}")

# optional GUI (environment sliders, key bindings help), through vsgImGui
find_package(vsgImGui QUIET)
message(STATUS "Found vsgImGui: ${vsgImGui_DIR}")

# GUI support (set to OFF to build a core without the ImGui dependency); the
# environment key bindings are always available
option(VSGOCEAN_WITH_GUI "Build the environment tuning GUI (requires vsgImGui)" ${vsgImGui_FOUND})

# SDF support (set to OFF to build a core without any robot-description dependency)
option(VSGOCEAN_WITH_SDF "Build vsgOcean with SDF scene support (requires sdformat)" ${sdformat_FOUND})
if(VSGOCEAN_WITH_SDF)
    if(NOT sdformat_FOUND)
        message(FATAL_ERROR "VSGOCEAN_WITH_SDF is ON but sdformat could not be found")
    endif()
    message(STATUS "Found sdformat: ${sdformat_DIR} - SDF support enabled")
else()
    message(STATUS "SDF support disabled")
endif()

# Embed the GLSL shader files into generated headers, so the library does not
# depend on shader files at runtime. sky.vert -> generated/sky_vert.h etc.
function(vsgocean_embed_shader shader)
    get_filename_component(name ${shader} NAME)
    string(REPLACE "." "_" identifier ${name})
    set(output ${CMAKE_CURRENT_BINARY_DIR}/generated/${identifier}.h)
    add_custom_command(
        OUTPUT ${output}
        COMMAND ${CMAKE_COMMAND}
            -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader}
            -DOUTPUT=${output}
            -DVARIABLE=${identifier}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedShader.cmake
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader}
        COMMENT "Embedding shader ${shader}"
        VERBATIM)
    set(VSGOCEAN_EMBEDDED_SHADERS ${VSGOCEAN_EMBEDDED_SHADERS} ${output} PARENT_SCOPE)
endfunction()

set(VSGOCEAN_EMBEDDED_SHADERS "")
vsgocean_embed_shader(sky.vert)
vsgocean_embed_shader(sky.frag)
vsgocean_embed_shader(ocean.vert)
vsgocean_embed_shader(ocean.frag)
vsgocean_embed_shader(underwater.vert)
vsgocean_embed_shader(underwater.frag)
vsgocean_embed_shader(present.vert)
vsgocean_embed_shader(snow.vert)
vsgocean_embed_shader(snow.frag)
vsgocean_embed_shader(present.frag)

# vsgOcean library
set(VSGOCEAN_LIBRARY_SOURCES
    # core: environment state and the scene model tree
    src/core/Environment.cpp
    src/core/Model.cpp
    src/core/UriResolver.cpp
    src/core/Wavefield.cpp
    # scene: environment rendering, per-camera render pipelines
    src/scene/OceanSurface.cpp
    src/scene/PresentPass.cpp
    src/scene/RenderPipeline.cpp
    src/scene/RenderTarget.cpp
    src/scene/LampLights.cpp
    src/scene/MarineSnow.cpp
    src/scene/Scene.cpp
    src/scene/Sky.cpp
    src/scene/Underwater.cpp
    # ui: runtime controls, GUI panel, HUD, camera controllers
    src/ui/CameraFrame.cpp
    src/ui/EnvironmentControls.cpp
    src/ui/EnvironmentGui.cpp
    src/ui/HorizonController.cpp
    src/ui/Hud.cpp
    ${VSGOCEAN_EMBEDDED_SHADERS}
)
if(VSGOCEAN_WITH_SDF)
    list(APPEND VSGOCEAN_LIBRARY_SOURCES src/scene/SDFLoader.cpp)
endif()

# shared library: consumers link a single .so and do not need vsgOcean's
# private dependencies (ZLIB, sdformat, vsgImGui) at their own link time
add_library(vsgOcean SHARED ${VSGOCEAN_LIBRARY_SOURCES})
add_library(vsgOcean::vsgOcean ALIAS vsgOcean)

target_compile_definitions(vsgOcean PRIVATE VSGOCEAN_LIBRARY)

target_include_directories(vsgOcean PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# the generated headers embedding the shaders
target_include_directories(vsgOcean PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)

target_link_libraries(vsgOcean
    PUBLIC vsg::vsg
    PRIVATE ZLIB::ZLIB
)

if(VSGOCEAN_WITH_SDF)
    target_compile_definitions(vsgOcean PUBLIC VSGOCEAN_WITH_SDF)
    target_link_libraries(vsgOcean PRIVATE sdformat::sdformat)
endif()

if(VSGOCEAN_WITH_GUI)
    # PUBLIC: consumers may call the GUI utilities, so they must see the same flag
    target_compile_definitions(vsgOcean PUBLIC VSGOCEAN_WITH_GUI)
    target_link_libraries(vsgOcean PRIVATE vsgImGui::vsgImGui)
endif()

if(VSGOCEAN_WITH_XCHANGE_ALL)
    target_compile_definitions(vsgOcean PUBLIC vsgXchange_all)
endif()

# Install and export the library so that find_package(vsgOcean) works from an install tree
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS vsgOcean
    EXPORT vsgOceanTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/vsgOcean DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT vsgOceanTargets
    FILE vsgOceanTargets.cmake
    NAMESPACE vsgOcean::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/vsgOcean
)

configure_package_config_file(cmake/vsgOceanConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/vsgOceanConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/vsgOcean
)

write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/vsgOceanConfigVersion.cmake
    VERSION ${VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/vsgOceanConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/vsgOceanConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/vsgOcean
)

# Add example subdirectory
add_subdirectory(example)
