// vsgOcean baseline sky: fullscreen pass, fragment stage
#version 450

layout(location = 0) in vec2 ndc;
layout(location = 0) out vec4 fragColor;

layout(std140, set = 0, binding = 0) uniform Environment
{
    vec4 sunDirection;    // xyz: unit vector towards the sun
    vec4 fogColorDensity; // rgb: fog color, a: fog density
    mat4 inverseViewProjection;
    vec4 stars[32];       // xyz: local direction, w: brightness; w = 0 when the slot is unused
    vec4 water;           // rgb: Jerlov water color, a: mean attenuation (1/m)
    vec4 submersion;      // x: camera depth below the surface (m), 0 when above the water
} env;

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
    const vec3 zenithNight = vec3(0.015, 0.02, 0.05);
    const vec3 horizonNight = vec3(0.05, 0.06, 0.10);

    const float up = clamp(direction.z, 0.0, 1.0);
    const float gradient = pow(up, 0.4);
    vec3 sky = mix(mix(horizonNight, horizonDay, day), mix(zenithNight, zenithDay, day), gradient);

    // dusk glow around the sun azimuth near the horizon
    const vec2 sunHorizontal = normalize(sunDirection.xy + vec2(1e-5, 0.0));
    const float towardsSun = max(dot(normalize(direction.xy + vec2(1e-5, 0.0)), sunHorizontal), 0.0);
    sky += dusk * pow(1.0 - up, 4.0) * pow(towardsSun, 4.0) * vec3(0.9, 0.45, 0.15) * 0.8;

    // sun disc with an edge smoothed over the pixel footprint, and a smooth glow,
    // fading as the sun sinks below the horizon
    const float sunAngle = acos(clamp(dot(direction, sunDirection), -1.0, 1.0));
    const float sunRadius = 0.01; // radians, about twice the real sun
    const float edge = max(pixelAngle * 1.5, 0.002);
    const float sunDisc = 1.0 - smoothstep(sunRadius - edge, sunRadius + edge, sunAngle);
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

            const float angle = acos(clamp(dot(direction, star.xyz), -1.0, 1.0));
            const float sigma = max(pixelAngle * 1.5, 0.0008);
            sky += night * star.w * exp(-0.5 * pow(angle / sigma, 2.0)) * vec3(0.92, 0.94, 1.0) * 1.2;
        }
    }

    // haze: blend towards the fog color at and below the horizon
    const float haze = clamp(env.fogColorDensity.a * 300.0, 0.0, 1.0);
    sky = mix(sky, env.fogColorDensity.rgb, haze * (1.0 - smoothstep(-0.05, 0.15, direction.z)));

    return sky;
}

// underwater background: the Jerlov water color, darkening with depth, brighter towards
// the surface and the sun, fading in smoothly as the camera dives below the waves
vec3 underwaterColor(vec3 direction, vec3 sunDirection)
{
    const float depth = env.submersion.x;
    const vec3 waterColor = env.water.rgb;
    const float k = env.water.a;

    // overall light level falls exponentially with depth
    const float light = exp(-k * max(depth, 0.5));
    // brighter looking up towards the surface, darker looking down
    const float up = clamp(direction.z * 0.5 + 0.5, 0.0, 1.0);
    vec3 water = waterColor * (0.1 + 0.9 * light) * (0.45 + 0.55 * up);

    // the sun glowing down through the water
    const float sunAngle = acos(clamp(dot(direction, sunDirection), -1.0, 1.0));
    const vec3 sunColor = vec3(0.6, 0.95, 1.0);
    water += exp(-sunAngle / 0.3) * sunColor * 0.4 * light * max(sunDirection.z, 0.0);

    return water;
}

void main()
{
    // view ray from the inverse view projection: direction from the camera through the pixel
    const vec4 nearPoint = env.inverseViewProjection * vec4(ndc, -1.0, 1.0);
    const vec4 farPoint = env.inverseViewProjection * vec4(ndc, 1.0, 1.0);
    const vec3 direction = normalize(farPoint.xyz / farPoint.w - nearPoint.xyz / nearPoint.w);

    // angular size of a pixel on the sky, used to anti-alias the sun edge and star points
    const float pixelAngle = length(fwidth(direction));

    vec3 sky;
    if (env.submersion.x > 0.0)
    {
        sky = underwaterColor(direction, normalize(env.sunDirection.xyz));
    }
    else
    {
        sky = skyColor(direction, normalize(env.sunDirection.xyz), pixelAngle);
    }

    // dithering to avoid banding in the gradients on 8 bit framebuffers
    sky += (ign(gl_FragCoord.xy) - 0.5) / 255.0;

    fragColor = vec4(sky, 1.0);
}
