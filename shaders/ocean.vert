// SPDX-License-Identifier: MIT
// The Gerstner surface displacement and normal formulation below is derived from the wave
// shaders of the UUV Simulator (Copyright (c) 2016 The UUV Simulator Authors, Apache-2.0),
// as modified in asv_wave_sim (Copyright (C) 2019 Rhys Mainwaring, Apache-2.0).
// License: http://www.apache.org/licenses/LICENSE-2.0

// vsgOcean ocean surface: Gerstner wave displacement, vertex stage
#version 450

layout(push_constant) uniform PushConstants
{
    mat4 projection; // offset 0, provided by the vsg record traversal
    mat4 modelview;  // offset 64, provided by the vsg record traversal
} pc;

layout(std140, set = 0, binding = 0) uniform Ocean
{
    vec4 sunDirection;    // xyz: unit vector towards the sun
    vec4 fogColorDensity; // rgb: fog color, a: fog density
    vec4 waterColor;      // rgb: water body colour from the Jerlov water type
    vec4 params;          // x: time t [s], y: tau [s], z: number of waves, w: assumed water depth [m]
    vec4 seaLevel;        // rgb: per-channel vertical Jerlov attenuation (1/m), w: mean sea level (m) above the origin;
                          // the camera state comes through push constants, so one surface renders from any camera
    vec4 amplitude[8];
    vec4 wavenumber[8];
    vec4 omega[8];
    vec4 steepness[8];
    vec4 direction[8];    // xy: horizontal direction of the component
    vec4 fft;             // x: 1 when the surface is the FFT sea, y: its tile size [m]
} ocean;

layout(set = 0, binding = 1) uniform sampler2D fftDisplacement; // (dx, dy, height) of the tile
layout(set = 0, binding = 2) uniform sampler2D fftSlopes;       // (dh/dx, dh/dy, jacobian)

layout(location = 0) in vec3 position; // grid point on the mean water level (z = 0)

layout(location = 0) out vec3 worldPosition;
layout(location = 1) out vec3 worldNormal;
layout(location = 2) out vec3 cameraEye; // world position of the camera of this view

void main()
{
    const float t = ocean.params.x;
    const int number = int(ocean.params.z);
    const float grow = 1.0 - exp(-t / ocean.params.y);

    // the camera position of this view, from the modelview push constant; the disc
    // re-centres on it, so the ocean is endless. The wave field is evaluated at world
    // positions: the waves stay fixed in the world as the mesh follows the camera
    cameraEye = vec3(inverse(pc.modelview) * vec4(0.0, 0.0, 0.0, 1.0));

    vec3 p = position + vec3(cameraEye.x, cameraEye.y, 0.0);

    // tangent along x, binormal along y and normal of the displaced surface,
    // accumulated over the component waves (Gerstner form of Tessendorf, as the
    // Gazebo wave model: theta = k * dir . x - omega * t)
    vec3 T = vec3(1.0, 0.0, 0.0);
    vec3 B = vec3(0.0, 1.0, 0.0);
    vec3 N = vec3(0.0, 0.0, 1.0);

    for (int i = 0; i < 8; ++i)
    {
        if (i >= number) break;

        const float a = ocean.amplitude[i].x * grow;
        const float k = ocean.wavenumber[i].x;
        const float w = ocean.omega[i].x;
        const float q = ocean.steepness[i].x;
        const vec2 d = ocean.direction[i].xy;

        const float theta = dot(d, p.xy) * k - w * t;
        const float c = cos(theta);
        const float s = sin(theta);

        // displacement of the point due to the wave
        p.x -= q * a * d.x * s;
        p.y -= q * a * d.y * s;
        p.z += a * c;

        // surface derivatives
        const float ka = a * k;
        const float qkac = q * ka * c;
        const float kas = ka * s;
        const float dxy = d.x * d.y;

        T += vec3(-qkac * d.x * d.x, -qkac * dxy, -kas * d.x);
        B += vec3(-qkac * dxy, -qkac * d.y * d.y, -kas * d.y);
        N += vec3(d.x * kas, d.y * kas, -qkac);
    }

    if (ocean.fft.x > 0.5)
    {
        // the FFT sea replaces the component waves: the periodic tile is looked up at the
        // world position, fading out beyond a few tiles where its texels turn sub-pixel
        const vec2 uv = p.xy / ocean.fft.y;
        const float dist = length(p.xy - cameraEye.xy);
        const float fade = grow * (1.0 - smoothstep(2.0 * ocean.fft.y, 6.0 * ocean.fft.y, dist));
        const vec3 d = textureLod(fftDisplacement, uv, 0.0).xyz;
        const vec2 slope = textureLod(fftSlopes, uv, 0.0).xy;
        p += fade * vec3(d.xy, d.z);
        N = normalize(vec3(-fade * slope, 1.0));
    }

    // the mean water level offset (sea level)
    p.z += ocean.seaLevel.w;

    worldPosition = p;
    worldNormal = normalize(N);

    gl_Position = pc.projection * (pc.modelview * vec4(p, 1.0));
}
