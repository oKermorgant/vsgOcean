// vsgOcean rain: the falling streaks, vertex stage. The drops fall at their
// terminal velocity, advected by the wind, wrapped in a camera-following box
// that spans the air column above the water; each drop renders as a short
// streak (the shutter exposure of a fast fall) billboarded around the view
// axis. The camera state comes through the push constants, so one rain field
// renders from any number of cameras.
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

layout(std430, set = 0, binding = 1) readonly buffer Drops
{
    vec4 drop[]; // xyz: random base position in [0,1), w: random seed
} drops;

layout(location = 0) out vec4 particleColor; // rgb: the veiling tone, a: alpha
layout(location = 1) out vec2 corner;         // the quad corner, [-1,1]
layout(location = 2) flat out float mode;     // 0: a streak (the shared fragment)

void main()
{
    const int index = gl_VertexIndex / 6;
    const vec4 base = drops.drop[index];
    mode = 0.0f;

    // this view's camera, from the modelview push constant
    const vec3 eye = vec3(inverse(pc.modelview) * vec4(0.0, 0.0, 0.0, 1.0));

    // the fall: terminal velocity ~9 m/s, advected by the wind
    const vec3 velocity = vec3(rain.weather.x, rain.weather.y, -9.0);
    const vec3 drifted = base.xyz * vec3(rain.params.z, rain.params.z, 25.0) + velocity * rain.params.x;

    // wrapped in a camera-following box: horizontally around the eye,
    // vertically within the air column above the mean sea level
    const float box = rain.params.z;
    const float column = 25.0;
    vec3 world;
    world.x = mod(drifted.x - eye.x + 0.5 * box, box) - 0.5 * box + eye.x;
    world.y = mod(drifted.y - eye.y + 0.5 * box, box) - 0.5 * box + eye.y;
    world.z = rain.weather.z + mod(drifted.z - rain.weather.z, column);

    // the streak: a quad along the fall direction, billboarded around the
    // view axis; ~80 cm long (the shutter exposure), ~6 cm wide - a rain
    // streak is the motion blur of a fast drop, not the drop itself
    const vec2 corners[6] = {{-1.0, -1.0}, {1.0, -1.0}, {1.0, 1.0}, {-1.0, -1.0}, {1.0, 1.0}, {-1.0, 1.0}};
    corner = corners[gl_VertexIndex % 6];

    const vec3 dir = normalize(velocity);
    const vec3 side = normalize(cross(dir, normalize(eye - world)));
    const vec3 pos = world + dir * (corner.y * 0.8) + side * (corner.x * 0.06);

    const vec4 viewPos = pc.modelview * vec4(pos, 1.0);
    gl_Position = pc.projection * viewPos;

    // the fades: rain is a surface phenomenon (out with submersion) and very
    // close drops would flash by - fade them in. The streak's own shape (the
    // soft ends) is a FRAGMENT matter: it would be zero at every vertex here
    const float submersion = rain.weather.z - eye.z;
    const float aboveWater = 1.0 - smoothstep(0.0, 1.0, submersion);
    const float near = smoothstep(0.4, 2.0, distance(eye, world));
    const float alpha = mix(0.12, 0.4, fract(base.w * 5.7)) * aboveWater * near;

    // the veiling tone: the sky brightness (dimmed by the cloud cover, black
    // at night), slightly brighter than the fog so the rain reads against it
    particleColor = vec4(vec3(rain.weather.w * 1.1), alpha);
}
