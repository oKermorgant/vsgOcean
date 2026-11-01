// vsgOcean marine snow: drifting particles in a camera-following box, vertex stage.
// The particles wrap around the camera (the box re-centres on the eye every frame,
// so the snow follows the view), advect along the water current, and are lit by
// the sun surviving to their depth plus the scene's point and spot lamps (the same
// model as the composite's volumetric cones) - snow sparkles in the ROV beams at
// night. The camera state comes through the push constants, so one snow field
// renders from any number of cameras.
#version 450

layout(push_constant) uniform PushConstants
{
    mat4 projection; // offset 0, provided by the vsg record traversal
    mat4 modelview;  // offset 64, provided by the vsg record traversal
} pc;

layout(std140, set = 0, binding = 0) uniform Snow
{
    vec4 params;       // x: time t [s], y: density fraction 0..1, z: box size [m], w: cloudiness 0..1
    vec4 current;      // xyz: water current [m/s], w: mean sea level [m]
    vec4 sunDirection; // xyz: unit vector towards the sun
    vec4 waterColor;   // rgb: Jerlov water colour, a: mean vertical attenuation (1/m)
    vec4 attenuation;  // rgb: per-channel vertical attenuation (1/m), w: unused
    vec4 beam;         // rgb: per-channel horizontal beam attenuation (1/m), w: unused
    vec4 lampHeader;   // x: light count, yzw: per-channel scattering coefficient b (1/m)
    vec4 lamp[32];     // 4 per light, as packed by vsgOcean::packLampLights
} snow;

layout(std430, set = 0, binding = 1) readonly buffer Particles
{
    vec4 particle[];   // xyz: random base position in [0,1), w: random seed
} particles;

layout(location = 0) out vec4 particleColor; // rgb: light reflected by the particle, a: alpha
layout(location = 1) out vec2 corner;         // the quad corner, [-1,1], for the round mask

void main()
{
    const int particleIndex = gl_VertexIndex / 6;
    const vec4 base = particles.particle[particleIndex];

    // this view's camera, from the modelview push constant
    const mat4 inverseModelview = inverse(pc.modelview);
    const vec3 eye = vec3(inverseModelview * vec4(0.0, 0.0, 0.0, 1.0));

    // density: a stable per-particle draw against the density (0 disables the field)
    const float draw = fract(base.w * 7.13);
    if (snow.params.y <= 0.0 || draw > snow.params.y)
    {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0); // clipped away
        return;
    }

    // wrap the drifting particle in a box around the camera: advected from its
    // base position along the water current, wrapped seamlessly (mod is positive
    // in GLSL), so the field is endless and follows the view
    const float box = snow.params.z;
    const vec3 drifted = base.xyz * box + snow.current.xyz * snow.params.x;
    const vec3 world = mod(drifted - eye + 0.5 * box, box) - 0.5 * box + eye;

    // the particle radius: 6..30 mm flakes, seeded per particle
    const float radius = mix(0.006, 0.03, fract(base.w * 3.71));

    // the quad corner for this vertex, in view space (a camera-facing billboard)
    const vec2 corners[6] = {{-1.0, -1.0}, {1.0, -1.0}, {1.0, 1.0}, {-1.0, -1.0}, {1.0, 1.0}, {-1.0, 1.0}};
    corner = corners[gl_VertexIndex % 6];

    const vec4 viewPos = pc.modelview * vec4(world, 1.0);
    gl_Position = pc.projection * (viewPos + vec4(corner * radius, 0.0, 0.0));

    // the whole field fades in with the camera's submersion (the mean sea level
    // criterion, as everywhere) and out just below the surface
    const float submersion = snow.current.w - eye.z;
    const float depth = max(snow.current.w - world.z, 0.0);
    const float fade = smoothstep(0.0, 1.0, submersion) * smoothstep(0.0, 0.5, depth);
    if (fade <= 0.0)
    {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }

    // the light AT the particle: the sun surviving to its depth (per-channel
    // Beer-Lambert over the vertical path, dimmed by the cloud cover) plus the
    // multiply-scattered ambient
    const float sunHeight = max(snow.sunDirection.z, 0.0);
    const float day = smoothstep(-0.05, 0.25, snow.sunDirection.z);
    const float sunThrough = (day * sunHeight) * (1.0 - snow.params.w);
    vec3 light = vec3(1.0, 0.97, 0.90) * sunThrough * exp(-snow.attenuation.rgb * depth)
               + snow.waterColor.rgb * (0.5 * sunThrough) * exp(-snow.waterColor.a * depth);

    // the scene's lamps, with the same model as the composite's volumetric march
    for (int i = 0; i < int(snow.lampHeader.x); ++i)
    {
        const vec4 posIntensity = snow.lamp[i * 4 + 0];
        const vec4 dirCosInner = snow.lamp[i * 4 + 1];
        const vec4 coneRange = snow.lamp[i * 4 + 2];
        const vec4 lampColor = snow.lamp[i * 4 + 3];

        const vec3 toLamp = posIntensity.xyz - world;
        const float d = length(toLamp);
        if (coneRange.y > 0.0 && d > coneRange.y) continue;

        const vec3 incident = -toLamp / max(d, 1.0e-4); // the light's travel direction at the particle
        float aperture = 1.0;
        if (dirCosInner.w > -0.999)
        {
            aperture = smoothstep(coneRange.x, dirCosInner.w, dot(incident, dirCosInner.xyz));
        }
        if (aperture <= 0.0) continue;

        // the light arriving at the particle, attenuated through the water
        light += lampColor.rgb * (posIntensity.w * 0.125) * aperture / (d * d + 1.0)
               * exp(-snow.beam.rgb * d);
    }

    // the flake reflects a fraction of the light that reaches it
    particleColor = vec4(light * 0.6, fade);
}
