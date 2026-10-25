// vsgOcean underwater pass: fullscreen fragment stage
//
// Blended over the whole scene when the camera is below the water surface, this pass
// adds the Jerlov in-scatter: light scattered into the view ray by the water, growing
// with distance so the scene fades towards the water colour. Combined with the Jerlov
// per-channel filtering of the scene lighting (see Scene::update), it is the baseline
// underwater rendering.
#version 450

layout(location = 0) in vec2 ndc;
layout(location = 0) out vec4 fragColor;

layout(std140, set = 0, binding = 0) uniform Underwater
{
    vec4 water;   // rgb: Jerlov water color, a: mean diffuse attenuation (1/m)
    vec4 params;  // x: camera submersion depth (m), y: sun height (-1..1)
} env;

void main()
{
    const float depth = env.params.x;
    const float sunHeight = max(env.params.y, 0.0);
    const float k = env.water.a;

    // in-scatter grows with the light path through the water; gated to fade in
    // smoothly below the waves and stay readable at shallow depth
    const float scatter = (1.0 - exp(-k * (1.5 + 0.5 * depth))) * smoothstep(0.0, 0.5, depth);

    // the in-scattered light itself: the water colour, lit by what remains of the sun
    const float light = exp(-k * max(depth, 0.5)) * (0.2 + 0.8 * sunHeight);
    const vec3 color = env.water.rgb * (0.25 + 0.75 * light);

    fragColor = vec4(color, scatter);
}
