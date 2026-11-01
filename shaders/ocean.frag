// vsgOcean ocean surface: the boundary between air and water, shaded in both
// directions around the analytic sky model (shared with sky.frag - keep in sync):
//
// - seen from ABOVE: the surface reflects the sky (Fresnel) and transmits the
//   refracted underwater view; the sun shows as the specular glitter path, broadened
//   by a glare lobe (osgOcean-style) on the rippled normals
// - seen from BELOW: the view refracts through the surface into the Snell window
//   (the sky compressed into a ~97 deg cone); beyond the critical angle the surface
//   totally internally reflects the water body - the physical reason the outside of
//   the window is a dark mirror
// - the composite pass then applies the water transport over the surface pixels
//   (vertical attenuation by the viewer's submersion here, horizontal Beer-Lambert
//   with the beam attenuation there)
#version 450

layout(std140, set = 0, binding = 0) uniform Ocean
{
    vec4 sunDirection;    // xyz: unit vector towards the sun, w: cloudiness 0..1
    vec4 fogColorDensity; // rgb: fog color, a: fog density
    vec4 waterColor;      // rgb: water body colour from the Jerlov water type, a: mean vertical attenuation (1/m)
    vec4 params;          // x: time t [s], y: tau [s], z: number of waves, w: assumed water depth [m]
    vec4 seaLevel;        // rgb: per-channel vertical Jerlov attenuation (1/m), w: mean sea level (m)
    vec4 amplitude[8];
    vec4 wavenumber[8];
    vec4 omega[8];
    vec4 steepness[8];
    vec4 direction[8];    // xy: horizontal direction of the component
} ocean;

layout(location = 0) in vec3 worldPosition;
layout(location = 1) in vec3 worldNormal;
layout(location = 2) in vec3 cameraEye; // world position of the camera of this view
layout(location = 0) out vec4 fragColor;

// the interleaved gradient noise used for dithering (shared with the sky shader)
float ign(vec2 p)
{
    return fract(52.9829189 * fract(0.06711056 * p.x + 0.00583715 * p.y));
}

// ---- shared sky model (keep in sync with sky.frag; the stars are omitted here: they
// are far below the surface's reflectance floor) ----
vec3 skyColor(vec3 direction, vec3 sunDirection, float pixelAngle)
{
    const float sunHeight = sunDirection.z;
    const float day = smoothstep(-0.05, 0.25, sunHeight);
    const float dusk = 1.0 - clamp(abs(sunHeight) / 0.2, 0.0, 1.0);

    const vec3 zenithDay = vec3(0.20, 0.42, 0.80);
    const vec3 horizonDay = vec3(0.75, 0.85, 0.95);
    const vec3 zenithNight = vec3(0.001, 0.0015, 0.003);
    const vec3 horizonNight = vec3(0.002, 0.003, 0.006);

    // the reflection sees the horizon band REMOVED relative to the sky itself (the
    // intentional difference from sky.frag): grazing reflections would otherwise be
    // dominated by the sky's milky horizon colour, dimming the far surface into
    // grey-white patches against the near blue water. The gradient clamps its
    // elevation from below, so the reflection never samples the sky below ~10 deg -
    // the sun disc, glow and dusk terms below keep the TRUE direction, so the
    // glitter path stays aligned with the sun
    const float up = clamp(max(direction.z, 0.18), 0.0, 1.0);
    const float gradient = smoothstep(0.0, 0.15, up);
    vec3 sky = mix(mix(horizonNight, horizonDay, day), mix(zenithNight, zenithDay, day), gradient);

    // the cloud cover (in [0].w, independent of the fog): the reflected sky greys
    // towards overcast like the sky itself (keep in sync with sky.frag)
    const float clouds = ocean.sunDirection.w;
    const vec3 overcast = vec3(0.42, 0.45, 0.50) * day;
    sky = mix(sky, overcast, clouds * 0.9);

    // dusk glow around the sun azimuth near the horizon
    const vec2 sunHorizontal = normalize(sunDirection.xy + vec2(1e-5, 0.0));
    const float towardsSun = max(dot(normalize(direction.xy + vec2(1e-5, 0.0)), sunHorizontal), 0.0);
    sky += dusk * pow(1.0 - up, 4.0) * pow(towardsSun, 4.0) * vec3(0.9, 0.45, 0.15) * 0.8 * (1.0 - clouds);

    // sun disc with an edge smoothed over the pixel footprint and a smooth glow,
    // worked in 1 - cos(angle) space (acos has catastrophic precision near 1 in fp32)
    const float sunRadius = 0.01; // radians, about twice the real sun
    const float cosSun = 1.0 - clamp(dot(direction, sunDirection), -1.0, 1.0);
    const float discCenter = 0.5 * sunRadius * sunRadius;
    const float discEdge = max(pixelAngle * 1.5, 0.002) * sunRadius;
    const float sunDisc = 1.0 - smoothstep(discCenter - discEdge, discCenter + discEdge, cosSun);
    const float sunAngle = sqrt(2.0 * cosSun);
    const float glow = exp(-sunAngle / 0.15) * 0.22 + exp(-sunAngle / 0.7) * 0.08;
    const vec3 sunColor = mix(vec3(1.0, 0.55, 0.25), vec3(1.0, 0.97, 0.90), day);
    const float sunFade = clamp(sunHeight * 10.0 + 0.5, 0.0, 1.0);
    sky += (sunDisc * 5.0 + glow) * sunColor * sunFade * pow(1.0 - clouds, 2.0);

    // haze towards the fog color at the horizon (Beer-Lambert, effective 400 m
    // path); the band follows the same clamped elevation as the gradient
    const float haze = 1.0 - exp(-ocean.fogColorDensity.a * 0.05 * 400.0);
    sky = mix(sky, ocean.fogColorDensity.rgb, haze * (1.0 - smoothstep(-0.05, 0.04, up)));
    return sky;
}

// ---- shared underwater background (keep in sync with sky.frag) ----
vec3 underwaterColor(vec3 direction, vec3 sunDirection, float depth)
{
    const float k = ocean.waterColor.a;
    const float light = exp(-k * max(depth, 0.5)) * max(sunDirection.z, 0.0);
    const float up = clamp(direction.z * 0.5 + 0.5, 0.0, 1.0);
    vec3 water = ocean.waterColor.rgb * light * (0.45 + 0.55 * up);

    // the sun glowing down through the water
    const float cosSun = 1.0 - clamp(dot(direction, sunDirection), -1.0, 1.0);
    const float sunAngle = sqrt(2.0 * cosSun);
    water += exp(-sunAngle / 0.3) * vec3(0.6, 0.95, 1.0) * 0.4 * light * max(sunDirection.z, 0.0);
    return water;
}

// smooth value noise (the classic hash + smoothstep interpolation), the workhorse
// behind the ripple detail: a continuum of directions with no periodic lattice, the
// way a real capillary-scale water surface is organized
float rippleHash(vec2 p)
{
    // the Hoskins hash: cheaper than the classic sin() hash (which is slow on
    // software rasterizers) and without its large-scale lattice artefacts
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float rippleNoise(vec2 p)
{
    const vec2 i = floor(p);
    const vec2 f = fract(p);
    const vec2 u = f * f * (3.0 - 2.0 * f);
    const float a = rippleHash(i);
    const float b = rippleHash(i + vec2(1.0, 0.0));
    const float c = rippleHash(i + vec2(0.0, 1.0));
    const float d = rippleHash(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y) * 2.0 - 1.0;
}

// ripple detail: drifting octave bands of the noise above, evaluated as slopes per
// pixel on top of the low-frequency Gerstner geometry. Each octave drifts along a
// wind-relative direction at its deep-water phase speed; the amplitude follows the
// sea state (the wave field's largest component) and fades with distance where the
// slopes would alias to sub-pixel noise. Unlike a sum of discrete plane waves (the
// earlier attempt, whose superposition organized into square-like interference
// cells), noise has no preferred directions: the surface reads organic.
vec2 rippleSlope(vec2 p, float t, float fade)
{
    if (fade <= 0.0) return vec2(0.0);

    // the capillary ripples follow the WIND (less wind = fewer ripples): 1.0 at the
    // default 5 m/s breeze, near calm below ~2 m/s, saturated above ~10 m/s
    const float sea = pow(clamp(ocean.direction[0].z / 5.0, 0.0, 1.0), 1.5) + clamp((ocean.direction[0].z - 10.0) / 10.0, 0.0, 0.5);
    const vec2 wind = normalize(ocean.direction[7].xy + vec2(1e-5, 0.0));
    const mat2 windFrame = mat2(wind.x, -wind.y, wind.y, wind.x);

    // characteristic wavelength [m], wind-relative drift direction [rad], slope at
    // the default sea state - two octave bands spanning the capillary range
    const vec3 octave[2] = vec3[2](vec3(1.1, 0.0, 0.09), vec3(0.45, 1.1, 0.06));

    vec2 slope = vec2(0.0);
    for (int i = 0; i < 2; ++i)
    {
        const float k = 6.2831853 / octave[i].x;
        const float speed = sqrt(9.81 / k); // deep-water phase speed sqrt(g/k)
        const vec2 dir = windFrame * vec2(cos(octave[i].y), sin(octave[i].y));
        const vec2 q = (p - dir * speed * t) * k; // the noise domain, in radians

        // the slope from the noise gradient (central differences over a fraction of
        // the characteristic period, smoothed by the interpolation)
        const float e = 0.25;
        const float n0 = rippleNoise(q);
        const float nx = rippleNoise(q + vec2(e, 0.0));
        const float ny = rippleNoise(q + vec2(0.0, e));
        slope += octave[i].z * sea * fade * vec2(nx - n0, ny - n0) / e;
    }
    return slope;
}

void main()
{
    const vec3 sunDirection = normalize(ocean.sunDirection.xyz);
    const vec3 toEye = normalize(cameraEye - worldPosition);
    const float t = ocean.params.x;

    // the fragment normal: the Gerstner geometry plus the ripple slopes; the ripple
    // detail fades with distance and only stirs the surface tangent, never flips it
    const float rippleFade = exp(-length(cameraEye - worldPosition) * 0.03);
    const vec2 slope = rippleSlope(worldPosition.xy, t, rippleFade);
    const vec3 n = normalize(worldNormal + vec3(-slope.x, -slope.y, 0.0));

    // day factor matching the scene lighting: black at night, no ambient floor
    const float day = smoothstep(-0.05, 0.25, sunDirection.z);

    // the viewer's side of the surface (the mean sea level criterion)
    const float submersion = max(ocean.seaLevel.w - cameraEye.z, 0.0);

    // Fresnel reflectance (Schlick, F0 = 0.02)
    const float fresnel = 0.02 + 0.98 * pow(1.0 - max(dot(n, toEye), 0.0), 5.0);

    // the sun's specular glitter on the rippled normals, plus a wide glare lobe
    const vec3 halfway = normalize(toEye + sunDirection);
    const float sunFade = clamp(sunDirection.z * 10.0 + 0.5, 0.0, 1.0);
    const float glitter = pow(max(dot(n, halfway), 0.0), 256.0) * 4.0;
    const float glare = pow(max(dot(n, halfway), 0.0), 96.0) * 0.12;
    const vec3 sunColor = mix(vec3(1.0, 0.55, 0.25), vec3(1.0, 0.97, 0.90), day);

    vec3 color;
    float alpha;

    if (submersion <= 0.0)
    {
        // seen from ABOVE: reflect the sky, transmit the refracted underwater view.
        // The sky mirror fades IN with distance: the near and mid water keeps its
        // body colour (the sky's pale gradient extends too deep into the surface
        // otherwise - the horizon-hugging compromise), and only the far field
        // approaches the full grazing-angle Fresnel reflection
        const vec3 reflected = reflect(-toEye, n);
        const vec3 reflectedSky = skyColor(reflected, sunDirection, length(fwidth(reflected)));
        const float viewDistance = length(worldPosition - cameraEye);
        const float skyWeight = mix(0.15, 1.0, smoothstep(20.0, 300.0, viewDistance));

        // the refracted view into the water: the near-surface volume back-scatters the
        // sunlight in the Jerlov colour over a shallow effective depth (~1.5 m, the
        // reason the sea reads bright blue from a boat while the deep is dark); the
        // scene behind still shows through the alpha
        const vec3 intoWater = refract(-toEye, n, 1.0 / 1.33);
        const float scatter = exp(-ocean.waterColor.a * 1.5) * max(sunDirection.z, 0.0) * (1.0 - ocean.sunDirection.w);
        const vec3 throughWater = ocean.waterColor.rgb * scatter + underwaterColor(intoWater, sunDirection, ocean.params.w) * 0.5;

        color = mix(throughWater, reflectedSky, fresnel * skyWeight) + (glitter + glare) * sunColor * sunFade * pow(1.0 - ocean.sunDirection.w, 2.0);

        // transparency towards the scene behind the surface: more mirror-like at
        // grazing angles, more transparent looking straight down
        const float clarity = exp(-ocean.waterColor.a * ocean.params.w);
        alpha = fresnel + (1.0 - fresnel) * (1.0 - clarity);
    }
    else
    {
        // seen from BELOW: the view refracts through the surface into the Snell
        // window (the whole above-surface sky compressed into the critical cone);
        // beyond the critical angle (~48.6 deg) the surface totally internally
        // reflects the water body - a dark mirror ring around the bright window
        const vec3 view = -toEye; // from the eye towards the surface
        const vec3 mirrored = underwaterColor(reflect(view, n), sunDirection, submersion);
        vec3 through = mirrored;
        // refract wants the normal opposing the incoming ray: from below that is -n
        const vec3 outRay = refract(view, -n, 1.33); // water -> air; zero on TIR
        if (dot(outRay, outRay) > 0.0)
        {
            // the water -> air reflectance from the transmitted angle: it rises to
            // total reflection as the incidence approaches the critical angle
            const float reflectance = 0.02 + 0.98 * pow(1.0 - clamp(dot(outRay, n), 0.0, 1.0), 5.0);
            through = mix(mirrored, skyColor(outRay, sunDirection, length(fwidth(outRay))), 1.0 - reflectance);
        }
        color = through;

        // the light from above survives the viewer's submersion depth, per channel
        color *= exp(-ocean.seaLevel.rgb * submersion);
        alpha = 1.0;
    }

    // dithering to avoid banding in the gradients on 8 bit framebuffers
    color += (ign(gl_FragCoord.xy) - 0.5) / 255.0;

    fragColor = vec4(color, alpha);
}
