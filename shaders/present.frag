// vsgOcean present pass: fullscreen fragment stage
//
// Draws the pipeline's composited image to the window, in front of which the HUD
// and GUI overlays are drawn. Kept separate from the composite so the composited
// image stays a stable capture target (screenshots, later image readback).
#version 450

layout(location = 0) in vec2 ndc;
layout(location = 0) out vec4 fragColor;

layout(std140, set = 0, binding = 0) uniform Present
{
    vec4 extent; // xy: render extent in pixels
} env;

layout(set = 0, binding = 1) uniform sampler2D scene;

void main()
{
    // 1:1 copy of the composited image at this pixel (framebuffer aligned, so no
    // vertical flip regardless of the viewport convention)
    fragColor = texture(scene, gl_FragCoord.xy / env.extent.xy);
}
