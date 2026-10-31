// vsgOcean present pass: draws the pipeline's composited output image to the window.
//
// The offscreen render target is a FIXED size (created with the window's initial
// extent; resizing it mid-run would mean tearing down and rebinding live Vulkan
// objects, which deadlocks the render). Instead the composite is fitted to the
// window here, preserving its aspect ratio: letterboxed with black bars when the
// window proportions differ.
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
    // fit the composite into the window, centered, preserving the aspect ratio
    const vec2 windowPerComposite = env.extent.zw / env.extent.xy;
    const float fit = min(windowPerComposite.x, windowPerComposite.y);
    const vec2 fitted = env.extent.xy * fit;
    const vec2 offset = (env.extent.zw - fitted) * 0.5;
    const vec2 pixel = gl_FragCoord.xy - offset;

    if (any(lessThan(pixel, vec2(0.0))) || any(greaterThanEqual(pixel, fitted)))
    {
        fragColor = vec4(0.0, 0.0, 0.0, 1.0); // the letterbox bars
        return;
    }
    fragColor = texture(scene, pixel / (fit * env.extent.xy));
}
