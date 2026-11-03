// vsgOcean semantic overlay: fullscreen triangle, vertex stage (see SemanticOverlay).
// The mode params live in the uniform block: in mode 0 (photo, both alphas 0)
// the triangle is pushed entirely outside the clip space, so the view still
// records - masking a view that shares a render pass with sibling views
// corrupts their draws (a vsg/Vulkan pitfall, see SemanticOverlay.h) - but no
// fragment is produced: no per-pixel cost.
#version 450

layout(location = 0) in vec3 position; // clip space position of a fullscreen triangle

layout(std140, set = 0, binding = 0) uniform SemanticDisplay
{
    vec4 params;      // x: model pixel alpha, y: background alpha, zw: window extent
    vec4 palette[64]; // the model colors in ID order
} display;

void main()
{
    if (display.params.x == 0.0 && display.params.y == 0.0)
    {
        gl_Position = vec4(2.0, 2.0, 0.0, 1.0); // fully outside the clip volume
        return;
    }
    gl_Position = vec4(position.xy, 0.0, 1.0);
}
