// vsgOcean ocean surface: baseline water shading, fragment stage
#version 450

layout(location = 0) in vec3 worldPosition;
layout(location = 1) in vec3 worldNormal;
layout(location = 0) out vec4 fragColor;

layout(std140, set = 0, binding = 0) uniform Ocean
{
    vec4 sunDirection;    // xyz: unit vector towards the sun
    vec4 fogColorDensity; // rgb: fog color, a: fog density
    vec4 waterColor;      // rgb: water body colour from the Jerlov water type, a: mean attenuation (1/m)
    vec4 params;          // x: time t [s], y: tau [s], z: number of waves, w: assumed water depth [m]
    vec4 cameraEye;       // xyz: camera position in world space
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

void main()
{
    const vec3 sunDirection = normalize(ocean.sunDirection.xyz);
    const vec3 n = normalize(worldNormal);
    const vec3 toEye = normalize(ocean.cameraEye.xyz - worldPosition);

    // day factor matching the scene lighting: dim at night
    const float day = smoothstep(-0.05, 0.25, sunDirection.z);

    // Fresnel reflectance of the water surface (Schlick, F0 = 0.02)
    const float fresnel = 0.02 + 0.98 * pow(1.0 - max(dot(n, toEye), 0.0), 5.0);

    // reflection of the sky, approximated by the same gradient as the sky shader
    const vec3 reflected = reflect(-toEye, n);
    const vec3 zenith = mix(vec3(0.015, 0.02, 0.05), vec3(0.20, 0.42, 0.80), day);
    const vec3 horizon = mix(vec3(0.05, 0.06, 0.10), vec3(0.75, 0.85, 0.95), day);
    const vec3 sky = mix(horizon, zenith, pow(clamp(reflected.z, 0.0, 1.0), 0.4));

    // sun specular (Blinn-Phong), warm at low sun, fading below the horizon
    const vec3 halfway = normalize(toEye + sunDirection);
    const float sunFade = clamp(sunDirection.z * 10.0 + 0.5, 0.0, 1.0);
    const float specular = pow(max(dot(n, halfway), 0.0), 256.0) * 4.0 * sunFade;
    const vec3 sunColor = mix(vec3(1.0, 0.55, 0.25), vec3(1.0, 0.97, 0.90), day);

    // water body colour from the Jerlov water type, dimmed at night like the scene
    const vec3 water = ocean.waterColor.rgb * (0.1 + 0.9 * day);

    // transparency: the water column absorbs light according to the Jerlov attenuation
    // over an assumed depth, and the surface is more transparent looking straight down
    // (small Fresnel) and mirror-like at grazing angles
    const float clarity = exp(-ocean.waterColor.a * ocean.params.w);
    float alpha = fresnel + (1.0 - fresnel) * (1.0 - clarity);

    vec3 color = mix(water, sky, fresnel) + specular * sunColor;

    // distance haze towards the fog color, making the surface opaque with distance
    const float distance = length(ocean.cameraEye.xyz - worldPosition);
    const float haze = 1.0 - exp(-ocean.fogColorDensity.a * distance * 10.0);
    color = mix(color, ocean.fogColorDensity.rgb, haze);
    alpha = max(alpha, haze);

    // dithering to avoid banding in the gradients on 8 bit framebuffers
    color += (ign(gl_FragCoord.xy) - 0.5) / 255.0;

    fragColor = vec4(color, alpha);
}
