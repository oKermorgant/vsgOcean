// vsgOcean baseline sky: fullscreen pass, fragment stage
#version 450

layout(location = 0) in vec2 ndc;
layout(location = 1) in vec3 nearPoint;
layout(location = 2) in vec3 farPoint;
layout(location = 3) in vec3 cameraEye; // the camera of this view
layout(location = 0) out vec4 fragColor;

layout(std140, set = 0, binding = 0) uniform Environment
{
    vec4 sunDirection;    // xyz: unit vector towards the sun
    vec4 fogColorDensity; // rgb: fog color, a: fog density
    vec4 stars[32];       // xyz: local direction, w: brightness; w = 0 when the slot is unused
    vec4 water;           // rgb: Jerlov water color, a: mean attenuation (1/m)
    vec4 params;          // x: time t [s], y: tau [s], z: mean sea level [m], w: number of waves
    vec4 amplitude[8];    // x: component wave amplitude [m]
    vec4 wavenumber[8];   // x: component wave wavenumber [1/m]
    vec4 omega[8];        // x: component wave angular frequency [1/s]
    vec4 direction[8];    // xy: horizontal direction of the component
} env;

// the depth of THIS camera below the wave field, evaluated in-shader so each camera
// gets its own underwater sky (the main view and the sensor cameras are independent)
float cameraSubmersion()
{
    const float t = env.params.x;
    const float grow = 1.0 - exp(-t / max(env.params.y, 1e-3));
    const int number = int(env.params.w);

    float height = env.params.z; // mean sea level
    for (int i = 0; i < 8; ++i)
    {
        if (i >= number) break;
        const float theta = dot(env.direction[i].xy, cameraEye.xy) * env.wavenumber[i].x - env.omega[i].x * t;
        height += env.amplitude[i].x * grow * cos(theta);
    }
    return max(height - cameraEye.z, 0.0);
}

// interleaved gradient noise: a stable per-pixel pattern used for dithering
float ign(vec2 p)
{
    return fract(52.9829189 * fract(0.06711056 * p.x + 0.00583715 * p.y));
}

// analytic baseline sky: day/night gradient from the sun elevation, sun disc and glow,
// dusk tint near the horizon, stars at night and haze blending to the fog color
vec3 skyColor(vec3 direction, vec3 sunDirection, float pixelAngle)
{
    const float sunHeight = sunDirection.z;
    const float day = smoothstep(-0.05, 0.25, sunHeight); // 0 at night, 1 in full day
    const float dusk = 1.0 - clamp(abs(sunHeight) / 0.2, 0.0, 1.0);

    const vec3 zenithDay = vec3(0.20, 0.42, 0.80);
    const vec3 horizonDay = vec3(0.75, 0.85, 0.95);
    // moonless night: essentially black, a faint deep-blue hint only - the sea must
    // disappear at night (the stars render on top of this)
    const vec3 zenithNight = vec3(0.001, 0.0015, 0.003);
    const vec3 horizonNight = vec3(0.002, 0.003, 0.006);

    const float up = clamp(direction.z, 0.0, 1.0);
    const float gradient = pow(up, 0.4);
    vec3 sky = mix(mix(horizonNight, horizonDay, day), mix(zenithNight, zenithDay, day), gradient);

    // dusk glow around the sun azimuth near the horizon
    const vec2 sunHorizontal = normalize(sunDirection.xy + vec2(1e-5, 0.0));
    const float towardsSun = max(dot(normalize(direction.xy + vec2(1e-5, 0.0)), sunHorizontal), 0.0);
    sky += dusk * pow(1.0 - up, 4.0) * pow(towardsSun, 4.0) * vec3(0.9, 0.45, 0.15) * 0.8;

    // sun disc with an edge smoothed over the pixel footprint, and a smooth glow,
    // fading as the sun sinks below the horizon. Worked in 1 - cos(angle) space:
    // acos() near 1.0 has catastrophic precision in fp32, which showed up as a
    // blocky, flickering sun disc and star points
    const float sunRadius = 0.01; // radians, about twice the real sun
    const float cosSun = 1.0 - clamp(dot(direction, sunDirection), -1.0, 1.0); // ~= angle^2 / 2
    const float discCenter = 0.5 * sunRadius * sunRadius;
    const float discEdge = max(pixelAngle * 1.5, 0.002) * sunRadius;
    const float sunDisc = 1.0 - smoothstep(discCenter - discEdge, discCenter + discEdge, cosSun);
    const float sunAngle = sqrt(2.0 * cosSun); // precise enough for the wide glow
    const float glow = exp(-sunAngle / 0.15) * 0.22 + exp(-sunAngle / 0.7) * 0.08;
    const vec3 sunColor = mix(vec3(1.0, 0.55, 0.25), vec3(1.0, 0.97, 0.90), day);
    const float sunFade = clamp(sunHeight * 10.0 + 0.5, 0.0, 1.0);
    sky += (sunDisc * 5.0 + glow) * sunColor * sunFade;

    // the constellations of the night sky, visible when the sun is below the horizon
    const float night = 1.0 - day;
    if (night > 0.01)
    {
        for (int i = 0; i < 32; ++i)
        {
            const vec4 star = env.stars[i];
            if (star.w <= 0.0 || star.z <= 0.0) continue; // unused slots and stars below the horizon

            const float cosStar = 1.0 - clamp(dot(direction, star.xyz), -1.0, 1.0); // ~= angle^2 / 2
            const float sigma = max(pixelAngle * 1.5, 0.0015); // radians; at least ~3 pixels
            sky += night * star.w * exp(-cosStar / (sigma * sigma)) * vec3(0.92, 0.94, 1.0) * 1.2;
        }
    }

    // haze: blend towards the fog color at and below the horizon, in proportion to
    // haze: blend towards the fog color at and below the horizon. Beer-Lambert over
    // an effective horizontal path (~400 m), so it saturates smoothly at heavy fog
    // instead of clamping: the pre-normalization "density * 60" formula saturated to
    // a full white-out horizon at any density above 0.0167
    const float haze = 1.0 - exp(-env.fogColorDensity.a * 0.05 * 400.0);
    sky = mix(sky, env.fogColorDensity.rgb, haze * (1.0 - smoothstep(-0.05, 0.15, direction.z)));

    return sky;
}

// underwater background: the Jerlov water color, darkening with depth, brighter towards
// the surface and the sun, fading in smoothly as the camera dives below the waves. No
// ambient floor: the light comes from the sun only, so the underwater background is
// black at night, like the scene
vec3 underwaterColor(vec3 direction, vec3 sunDirection, float depth)
{
    const vec3 waterColor = env.water.rgb;
    const float k = env.water.a;

    // sunlight reaching this depth, falling exponentially: nothing at night
    const float light = exp(-k * max(depth, 0.5)) * max(sunDirection.z, 0.0);
    // brighter looking up towards the surface, darker looking down
    const float up = clamp(direction.z * 0.5 + 0.5, 0.0, 1.0);
    vec3 water = waterColor * light * (0.45 + 0.55 * up);

    // the sun glowing down through the water
    const float cosSun = 1.0 - clamp(dot(direction, sunDirection), -1.0, 1.0);
    const float sunAngle = sqrt(2.0 * cosSun);
    const vec3 sunColor = vec3(0.6, 0.95, 1.0);
    water += exp(-sunAngle / 0.3) * sunColor * 0.4 * light * max(sunDirection.z, 0.0);

    return water;
}

void main()
{
    // view ray from the endpoints interpolated across the fullscreen triangle
    // (per camera, through the push constants)
    const vec3 direction = normalize(farPoint - nearPoint);

    // angular size of a pixel on the sky, used to anti-alias the sun edge and star points
    const float pixelAngle = length(fwidth(direction));

    vec3 sky;
    const float submersion = cameraSubmersion();
    if (submersion > 0.0)
    {
        sky = underwaterColor(direction, normalize(env.sunDirection.xyz), submersion);
    }
    else
    {
        sky = skyColor(direction, normalize(env.sunDirection.xyz), pixelAngle);
    }

    // dithering to avoid banding in the gradients on 8 bit framebuffers
    sky += (ign(gl_FragCoord.xy) - 0.5) / 255.0;

    fragColor = vec4(sky, 1.0);
}
