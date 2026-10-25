# Embed a shader file into a generated C++ header as a raw string literal.
#
# usage: cmake -DINPUT=<shader file> -DOUTPUT=<header file> -DVARIABLE=<C identifier> -P EmbedShader.cmake

file(READ "${INPUT}" content)

# the raw string delimiter must not appear in the shader content
if(content MATCHES "\\)VSGOCEAN\"")
    message(FATAL_ERROR "EmbedShader: ${INPUT} contains the raw string delimiter )VSGOCEAN\"")
endif()

file(WRITE "${OUTPUT}" "// generated from ${INPUT} by cmake/EmbedShader.cmake - do not edit\n\nstatic const char ${VARIABLE}[] = R\"VSGOCEAN(\n${content})VSGOCEAN\";\n")
