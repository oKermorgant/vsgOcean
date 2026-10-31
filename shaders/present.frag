// vsgOcean present pass: draws the pipeline's composited output image to the window.
//
// The offscreen render target is a FIXED size (created with the window's initial
// extent; resizing it mid-run would mean tearing down and rebinding live Vulkan
// objects, which deadlocks the render). Instead the window resize EXTENDS the
// view: the camera's projection aspect follows the window (a plain matrix update,
// changeExtent), the composite renders that widened/narrowed frustum anamorphically
// squeezed into the fixed target, and this pass stretches it back to fill the
// window - no letterbox bars, no distortion (the non-square target pixels cancel
// against the frustum aspect), and the view centre is kept.
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
    // stretch the composite over the whole window (the frustum aspect matches the
    // window, so the anamorphic squeeze of the fixed-size composite cancels here)
    fragColor = texture(scene, gl_FragCoord.xy / env.extent.zw);
}
