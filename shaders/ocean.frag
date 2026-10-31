// vsgOcean ocean surface: baseline water shading, fragment stage
#version 450

layout(location = 0) in vec3 worldPosition;
layout(location = 1) in vec3 worldNormal;
layout(location = 2) in vec3 cameraEye; // world position of the camera of this view
layout(location = 0) out vec4 fragColor;

layout(std140, set = 0, binding = 0) uniform Ocean
{
    vec4 sunDirection;    // xyz: unit vector towards the sun
    vec4 fogColorDensity; // rgb: fog color, a: fog density
    vec4 waterColor;      // rgb: water body colour from the Jerlov water type, a: mean attenuation (1/m)
    vec4 params;          // x: time t [s], y: tau [s], z: number of waves, w: assumed water depth [m]
    vec4 seaLevel;        // rgb: per-channel vertical Jerlov attenuation (1/m), w: mean sea level (m)
    vec4 amplitude[8];
    vec4 wavenumber[8];
    vec4 omega[8];
    vec4 steepness[8];
    vec4 direction[8];    // xy: horizontal direction of the component
} ocean;

// interleaved gradient noise: a stable per-pixel pattern used for dithering
float ign(vec2 p)
{
    return fract(52.9829189 * fract(0.06711056 * p.x + 0.00583715 * p.y));
}

// the height of the wave field at a horizontal position, from the component waves
float waveHeight(vec2 p)
{
    const float t = ocean.params.x;
    const float grow = 1.0 - exp(-t / max(ocean.params.y, 1e-3));
    const int number = int(ocean.params.z);

    float height = ocean.seaLevel.w; // mean sea level
    for (int i = 0; i < 8; ++i)
    {
        if (i >= number) break;
        const float theta = dot(ocean.direction[i].xy, p) * ocean.wavenumber[i].x - ocean.omega[i].x * t;
        height += ocean.amplitude[i].x * grow * cos(theta);
    }
    return height;
}

void main()
{
    const vec3 sunDirection = normalize(ocean.sunDirection.xyz);
    const vec3 n = normalize(worldNormal);
    const vec3 toEye = normalize(cameraEye - worldPosition);

    // day factor matching the scene lighting: black at night, no ambient floor
    const float day = smoothstep(-0.05, 0.25, sunDirection.z);

    // Fresnel reflectance of the water surface (Schlick, F0 = 0.02)
    const float fresnel = 0.02 + 0.98 * pow(1.0 - max(dot(n, toEye), 0.0), 5.0);

    // reflection of the sky, approximated by the same gradient as the sky shader
    const vec3 reflected = reflect(-toEye, n);
    const vec3 zenith = mix(vec3(0.015, 0.02, 0.05), vec3(0.20, 0.42, 0.80), day);
    const vec3 horizon = mix(vec3(0.002, 0.003, 0.006), vec3(0.75, 0.85, 0.95), day);
    const vec3 sky = mix(horizon, zenith, pow(clamp(reflected.z, 0.0, 1.0), 0.4));

    // sun specular (Blinn-Phong), warm at low sun, fading below the horizon
    const vec3 halfway = normalize(toEye + sunDirection);
    const float sunFade = clamp(sunDirection.z * 10.0 + 0.5, 0.0, 1.0);
    const float specular = pow(max(dot(n, halfway), 0.0), 256.0) * 4.0 * sunFade;
    const vec3 sunColor = mix(vec3(1.0, 0.55, 0.25), vec3(1.0, 0.97, 0.90), day);

    // water body colour from the Jerlov water type, sun-driven only
    const vec3 water = ocean.waterColor.rgb * day;

    // transparency: the water column absorbs light according to the Jerlov attenuation
    // over an assumed depth, and the surface is more transparent looking straight down
    // (small Fresnel) and mirror-like at grazing angles
    const float clarity = exp(-ocean.waterColor.a * ocean.params.w);
    float alpha = fresnel + (1.0 - fresnel) * (1.0 - clarity);

    vec3 color = mix(water, sky, fresnel) + specular * sunColor;

    // surface colours by the viewer's submersion depth, per channel (red is absorbed
    // first) - the light path is vertical, so the vertical diffuse Jerlov attenuation
    // applies, unlike the horizontal beam of the composite. The depth uses the MEAN
    // sea level (the same stable underwater criterion as the composite and the sky)
    const float submersion = max(ocean.seaLevel.w - cameraEye.z, 0.0);
    if (submersion > 0.0)
    {
        color *= exp(-ocean.seaLevel.rgb * submersion);
    }

    // the atmospheric fog by distance is applied on the whole scene by the composite
    // pass, which uses this surface's depth (the surface writes depth: it is drawn
    // after the opaque scene, so nothing is drawn after it in the scene pass)

    // dithering to avoid banding in the gradients on 8 bit framebuffers
    color += (ign(gl_FragCoord.xy) - 0.5) / 255.0;

    fragColor = vec4(color, alpha);
}
