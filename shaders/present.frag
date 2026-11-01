// vsgOcean present pass: draws the pipeline's composited output image to the window.
//
// The composite is rendered at the window extent (the pipeline is resized with the
// window), so this pass is a plain 1:1 copy.
#version 450

layout(location = 0) in vec2 ndc;
layout(location = 0) out vec4 fragColor;

layout(std140, set = 0, binding = 0) uniform Present
{
    vec4 extent; // xy: composite (offscreen) extent, zw: window extent
} env;

layout(set = 0, binding = 1) uniform sampler2D scene;

void main()
{
    fragColor = texture(scene, gl_FragCoord.xy / env.extent.zw);
}
