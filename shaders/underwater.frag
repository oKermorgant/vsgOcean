// vsgOcean scene composite: fullscreen fragment stage
//
// The scene (sky, lighting, models, ocean surface) is rendered into an offscreen
// color+depth target; this pass draws it to the window, applying:
//
// - atmospheric fog by view distance, on every pixel with scene geometry (the models
//   use the standard vsg shaders, which have no fog of their own), blended towards
//   the day-dimmed fog color like the sky horizon haze
// - underwater, the Jerlov light transport per pixel: the water path comes from the
//   scene depth buffer, so attenuation follows the view distance to each object,
//   and the transmittance is per channel (red is absorbed first), with the
//   in-scattered water light replacing what was absorbed
//
// The ray is split at the water surface: the atmospheric fog applies to the air
// segment, the underwater transport to the submerged segment.
#version 450

layout(location = 0) in vec2 ndc;
layout(location = 0) out vec4 fragColor;

layout(std140, set = 0, binding = 0) uniform Composite
{
    mat4 inverseViewProjection;
    vec4 eye;         // xyz: camera position, w: submersion depth (m), 0 when above water
    vec4 water;       // rgb: Jerlov water color, a: mean vertical attenuation (1/m)
    vec4 attenuation; // rgb: per-channel horizontal attenuation (1/m), w: sun height
    vec4 extent;      // xy: render extent in pixels, w: mean sea level (m)
    vec4 fog;         // rgb: fog color (already dimmed for the time of day), a: fog density
} env;

layout(set = 0, binding = 1) uniform sampler2D sceneColor;
layout(set = 0, binding = 2) uniform sampler2D sceneDepth;

void main()
{
    const vec2 uv = gl_FragCoord.xy / env.extent.xy;
    vec3 color = texture(sceneColor, uv).rgb;
    // reversed depth (vsg convention): 1 near, 0 cleared / background
    const float depth = texture(sceneDepth, uv).r;

    // pixels with no scene geometry: the sky pass already renders its own horizon
    // haze and underwater background - keep the scene color as is
    if (depth <= 0.0)
    {
        fragColor = vec4(color, 1.0);
        return;
    }

    // fragment world position and view distance, reconstructed from the depth buffer
    const vec4 worldH = env.inverseViewProjection * vec4(ndc, depth * 2.0 - 1.0, 1.0);
    const vec3 world = worldH.xyz / worldH.w;
    const float dist = length(world - env.eye.xyz);

    // the light path through the water: from a submerged camera up to the surface
    // crossing - geometry above the water, seen from below, only attenuates over
    // the submerged part of the ray
    const float seaLevel = env.extent.w;
    float waterPath = 0.0;
    if (env.eye.w > 0.0)
    {
        waterPath = dist;
        if (world.z > seaLevel)
        {
            waterPath *= clamp((env.eye.z - seaLevel) / (env.eye.z - world.z), 0.0, 1.0);
        }
    }

    // atmospheric fog over the air segment of the ray
    const float haze = 1.0 - exp(-env.fog.a * (dist - waterPath) * 10.0);
    color = mix(color, env.fog.rgb, haze);

    // underwater: Beer-Lambert per channel over the water segment, with the
    // in-scattered water light filling in
    if (env.eye.w > 0.0)
    {
        const vec3 transmit = exp(-env.attenuation.rgb * waterPath);
        const float light = exp(-env.water.a * max(env.eye.w, 0.5)) * (0.2 + 0.8 * max(env.attenuation.w, 0.0));
        const vec3 scatter = env.water.rgb * (0.25 + 0.75 * light);
        const vec3 underwater = color * transmit + scatter * (1.0 - transmit);

        // fade in smoothly as the camera dives below the waves
        color = mix(color, underwater, smoothstep(0.0, 0.5, env.eye.w));
    }

    fragColor = vec4(color, 1.0);
}
