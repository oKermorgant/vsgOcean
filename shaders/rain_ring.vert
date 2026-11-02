// vsgOcean rain: the splash rings where the drops hit the water, vertex
// stage. Each ring is a flat quad on the mean sea level, cycling spawn ->
// expand -> fade on a seed-scaled lifetime, wrapped in a camera-following
// box around the eye.
#version 450

layout(push_constant) uniform PushConstants
{
    mat4 projection; // offset 0, provided by the vsg record traversal
    mat4 modelview;  // offset 64, provided by the vsg record traversal
} pc;

layout(std140, set = 0, binding = 0) uniform Rain
{
    vec4 params;  // x: time t [s], y: rain rate 0..1, z: box edge [m], w: cloudiness 0..1
    vec4 weather; // xy: wind [m/s], z: mean sea level [m], w: sky brightness 0..1
} rain;

layout(std430, set = 0, binding = 2) readonly buffer Splashes
{
    vec4 splash[]; // xyz: random base position in [0,1), w: random seed
} splashes;

layout(location = 0) out vec4 particleColor; // rgb: the veiling tone, a: alpha
layout(location = 1) out vec2 corner;         // the quad corner, [-1,1]
layout(location = 2) flat out float mode;     // 1: a splash ring
layout(location = 3) flat out float ringPhase; // the ring's cycle phase, 0..1

void main()
{
    const int index = gl_VertexIndex / 6;
    const vec4 base = splashes.splash[index];
    mode = 1.0f;

    const vec3 eye = vec3(inverse(pc.modelview) * vec4(0.0, 0.0, 0.0, 1.0));

    // the splash point, wrapped around the eye
    const float box = rain.params.z;
    vec2 point;
    point.x = mod(base.x * box - eye.x + 0.5 * box, box) - 0.5 * box + eye.x;
    point.y = mod(base.y * box - eye.y + 0.5 * box, box) - 0.5 * box + eye.y;

    // the ring's life: spawn, expand over ~1 s, on a seed-scaled period
    const float period = 0.7 + 0.6 * fract(base.w * 3.7);
    ringPhase = fract(rain.params.x / period + base.w * 7.13);
    const float radius = 0.04 + 0.3 * ringPhase;

    const vec2 corners[6] = {{-1.0, -1.0}, {1.0, -1.0}, {1.0, 1.0}, {-1.0, -1.0}, {1.0, 1.0}, {-1.0, 1.0}};
    corner = corners[gl_VertexIndex % 6];

    // a flat quad on the mean sea level (a touch above, so the surface
    // geometry does not z-fight the ring)
    const vec3 pos = vec3(point + corner * radius, rain.weather.z + 0.02);

    const vec4 viewPos = pc.modelview * vec4(pos, 1.0);
    gl_Position = pc.projection * viewPos;

    // the fades: out with the camera's submersion, and the ring's own life
    const float submersion = rain.weather.z - eye.z;
    const float aboveWater = 1.0 - smoothstep(0.0, 1.0, submersion);
    const float life = smoothstep(0.0, 0.15, ringPhase) * (1.0 - smoothstep(0.5, 1.0, ringPhase));
    const float alpha = 0.12 * life * aboveWater;

    // the rings read as a brief brightening of the water where the drop hits
    particleColor = vec4(vec3(rain.weather.w * 1.3), alpha);
}
