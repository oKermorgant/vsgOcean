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

# vsgOcean library
set(VSGOCEAN_LIBRARY_SOURCES
    src/Environment.cpp
    src/HorizonController.cpp
    src/Model.cpp
    src/OceanSurface.cpp
    src/Scene.cpp
    src/Sky.cpp
    src/Underwater.cpp
    src/UriResolver.cpp
    src/Wavefield.cpp
    ${VSGOCEAN_EMBEDDED_SHADERS}
)
if(VSGOCEAN_WITH_SDF)
    list(APPEND VSGOCEAN_LIBRARY_SOURCES src/SDFLoader.cpp)
endif()

add_library(vsgOcean ${VSGOCEAN_LIBRARY_SOURCES})
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
)

if(VSGOCEAN_WITH_SDF)
    target_compile_definitions(vsgOcean PUBLIC VSGOCEAN_WITH_SDF)
    target_link_libraries(vsgOcean PRIVATE sdformat::sdformat)
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
