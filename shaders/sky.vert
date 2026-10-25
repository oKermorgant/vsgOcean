// vsgOcean baseline sky: fullscreen pass, vertex stage
#version 450

layout(location = 0) in vec3 position; // clip space position of a fullscreen triangle
layout(location = 0) out vec2 ndc;

void main()
{
    ndc = position.xy;
    gl_Position = vec4(position.xy, 0.0, 1.0);
}
