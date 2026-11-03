// vsgOcean semantic overlay, fragment: the on-screen semantic annotation view.
// Samples SemanticPass's ID image (UNORM, so the sampled bytes ARE the model
// IDs, id = r | g << 8 | b << 16, 0 = background) and paints each model with
// the palette color of its name.
//
// The palette comes as a uniform (the first 64 models, ID order, colors from
// SemanticPalette - the name-to-color rule lives there and in the header
// documentation). Models beyond it fall back to an in-place golden-angle hue:
// ad hoc colors for an unlikely scene, the ID data stays exact either way.
#version 450

layout(std140, set = 0, binding = 0) uniform SemanticDisplay
{
    vec4 params;      // x: model pixel alpha, y: background alpha, zw: window extent
    vec4 palette[64]; // the model colors in ID order
} display; // the same block the vertex stage reads (see semantic_overlay.vert)

layout(set = 0, binding = 3) uniform sampler2D ids; // the semantic pass's image

layout(location = 0) out vec4 fragColor;

// the in-place fallback for models past the uniform palette: the same pastel
// HSV the CPU palette builds (keep in sync with SemanticPalette)
vec3 fallbackColor(uint id)
{
    const float hue = mod(float(id) * 137.507764, 360.0) / 60.0;
    const float c = 0.92 * 0.55;
    const float x = c * (1.0 - abs(mod(hue, 2.0) - 1.0));
    vec3 rgb = hue < 1.0 ? vec3(c, x, 0.0)
              : hue < 2.0 ? vec3(x, c, 0.0)
              : hue < 3.0 ? vec3(0.0, c, x)
              : hue < 4.0 ? vec3(0.0, x, c)
              : hue < 5.0 ? vec3(x, 0.0, c)
                          : vec3(c, 0.0, x);
    return rgb + (0.92 - c);
}

void main()
{
    // the UNORM image keeps the bytes exact: byte = round(channel * 255)
    vec4 raw = texture(ids, gl_FragCoord.xy / display.params.zw);
    uint id = uint(raw.r * 255.0 + 0.5)
            | (uint(raw.g * 255.0 + 0.5) << 8)
            | (uint(raw.b * 255.0 + 0.5) << 16);

    if (id == 0)
    {
        // the background: opaque black in the semantic view, transparent in
        // the overlay mode (the photo stays visible)
        fragColor = vec4(0.0, 0.0, 0.0, display.params.y);
        return;
    }

    const vec3 color = id <= 64u ? display.palette[id - 1u].rgb : fallbackColor(id);
    fragColor = vec4(color, display.params.x);
}
