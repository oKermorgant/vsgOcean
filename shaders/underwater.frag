// vsgOcean scene composite: fullscreen fragment stage
//
// The scene (sky, lighting, models, ocean surface) is rendered into an offscreen
// color+depth target; this pass draws it to the pipeline's output image, applying:
//
// - atmospheric fog by view distance, on every pixel with scene geometry (the models
//   use the standard vsg shaders, which have no fog of their own), blended towards
//   the day-dimmed fog color like the sky horizon haze
// - underwater, the Jerlov light transport per pixel: the water path comes from the
//   scene depth buffer, so attenuation follows the view distance to each object,
//   and the transmittance is per channel (red is absorbed first), with the
//   in-scattered water light replacing what was absorbed
//
// The world position of each pixel is reconstructed from the scene depth buffer and
// this camera's inverse view projection (uploaded per frame by the pass). The ray is
// split at the water surface: the atmospheric fog applies to the air segment, the
// underwater transport to the submerged segment.
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

// FXAA (the compact 3.11 console variant): the offscreen render target has no
// multisampling (the software rasterizer makes MSAA prohibitively expensive), so
// the composite blends the scene colour across aliased geometric edges before the
// water transport. Both the presented window and the screenshots inherit it.
vec3 fxaa(vec2 uv, vec2 texel)
{
    const vec3 lum = vec3(0.299, 0.587, 0.114);

    const vec3 rgbNW = texture(sceneColor, uv + vec2(-1.0, -1.0) * texel).rgb;
    const vec3 rgbNE = texture(sceneColor, uv + vec2( 1.0, -1.0) * texel).rgb;
    const vec3 rgbSW = texture(sceneColor, uv + vec2(-1.0,  1.0) * texel).rgb;
    const vec3 rgbSE = texture(sceneColor, uv + vec2( 1.0,  1.0) * texel).rgb;
    const vec3 rgbM  = texture(sceneColor, uv).rgb;

    const float lNW = dot(rgbNW, lum);
    const float lNE = dot(rgbNE, lum);
    const float lSW = dot(rgbSW, lum);
    const float lSE = dot(rgbSE, lum);
    const float lM  = dot(rgbM, lum);
    const float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));
    const float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));

    // no edge in the neighbourhood: keep the centre sample
    if (lMax - lMin < lMax * 0.125) return rgbM;

    // the edge direction, scaled by the local contrast
    vec2 dir = vec2(-((lNW + lNE) - (lSW + lSE)), ((lNW + lSW) - (lNE + lSE)));
    const float dirReduce = max((lNW + lNE + lSW + lSE) * 0.125, 1.0 / 128.0);
    const float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = clamp(dir * rcpDirMin, vec2(-8.0), vec2(8.0)) * texel;

    // blend along the edge
    const vec3 rgbA = 0.5 * (texture(sceneColor, uv + dir * (1.0 / 3.0 - 0.5)).rgb
                           + texture(sceneColor, uv + dir * (2.0 / 3.0 - 0.5)).rgb);
    const vec3 rgbB = rgbA * 0.5 + 0.25 * (texture(sceneColor, uv + dir * -0.5).rgb
                                          + texture(sceneColor, uv + dir *  0.5).rgb);
    const float lB = dot(rgbB, lum);
    return (lB < lMin || lB > lMax) ? rgbA : rgbB;
}

void main()
{
    const vec2 uv = gl_FragCoord.xy / env.extent.xy;
    // reversed depth (vsg convention): 1 near, 0 cleared / background
    const float depth = texture(sceneDepth, uv).r;
    // the anti-aliased scene colour: only geometry edges alias (the background
    // sky and open-water gradients are smooth), so the FXAA's extra taps are
    // paid only where there is scene geometry
    vec3 color = depth > 0.0 ? fxaa(uv, 1.0 / env.extent.xy) : texture(sceneColor, uv).rgb;

    // the light path through the water: from a submerged camera up to the surface
    // haze and underwater background - keep the scene color as is
    if (depth <= 0.0)
    {
        fragColor = vec4(color, 1.0);
        return;
    }

    // fragment world position and view distance, reconstructed from the depth buffer
    // (reversed depth, Vulkan NDC z in [0,1]: the window depth is the NDC z as is)
    const vec4 worldH = env.inverseViewProjection * vec4(ndc, depth, 1.0);
    const vec3 world = worldH.xyz / worldH.w;
    const float dist = length(world - env.eye.xyz);

    // the submerged segment of the view ray: the water path runs between the
    // camera and the geometry, split at the mean sea level plane - from below the
    // surface it ends at the crossing, from above it starts there
    const float seaLevel = env.extent.w;
    float waterPath = 0.0;
    if (env.eye.w > 0.0)
    {
        waterPath = dist;
        if (world.z > seaLevel) // geometry above the surface, seen from below
        {
            waterPath *= clamp((env.eye.z - seaLevel) / (env.eye.z - world.z), 0.0, 1.0);
        }
    }
    else if (world.z < seaLevel) // geometry below the surface, seen from above
    {
        // the ray crosses the surface plane partway along: only the segment past
        // the crossing is water (for the surface itself that is next to nothing)
        const float crossing = clamp((seaLevel - env.eye.z) / (world.z - env.eye.z), 0.0, 1.0);
        waterPath = dist * (1.0 - crossing);
    }

    // atmospheric fog over the air segment of the ray: the density is the air's
    // extinction coefficient (Beer-Lambert): haze = 1 - exp(-density * distance)
    const float haze = 1.0 - exp(-env.fog.a * (dist - waterPath));
    color = mix(color, env.fog.rgb, haze);

    // underwater transport over the water segment: per-channel Beer-Lambert with
    // the in-scattered water light filling in. No ambient floor: the water light
    // is sun-driven only, so a deep night scene attenuates to black
    {
        const vec3 transmit = exp(-env.attenuation.rgb * waterPath);
        // the sun light surviving to the depth of the viewed water: the camera's
        // submersion when below, the surface when looking down from above
        const float depth = max(env.eye.w, 0.0);
        const float light = exp(-env.water.a * max(depth, 0.5)) * max(env.attenuation.w, 0.0);
        // the in-scattered light is the sum of the multiply-scattered ambient
        // (isotropic, the water colour) and the singly-scattered sun light, which
        // follows the Henyey-Greenstein phase (g = 0.924, strongly forward-peaked -
        // the same model as Stonefish): bright when looking towards the sun, faint
        // elsewhere. The sun vector is reconstructed from [6].w (height) + [7].z (az)
        const vec3 sunDir = normalize(vec3(cos(env.attenuation.w) * cos(env.extent.z),
                                           cos(env.attenuation.w) * sin(env.extent.z),
                                           env.attenuation.w));
        const vec3 viewDir = normalize(world - env.eye.xyz);
        const float cosTheta = dot(viewDir, sunDir);
        const float g = 0.924;
        const float phase = (1.0 - g * g) / pow(1.0 + g * g - 2.0 * g * cosTheta, 1.5);
        const vec3 scatter = env.water.rgb * light * (1.0 + 0.003 * phase);
        const vec3 underwater = color * transmit + scatter * (1.0 - transmit);

        // the transport applies fully to the water segment; the fade-in only spans
        // the first centimetres as the camera dips through the waves, so there is
        // no clear band just below the surface
        const float inWater = env.eye.w > 0.0 ? smoothstep(0.0, 0.05, env.eye.w) : 1.0;
        color = mix(color, underwater, inWater);
    }

    fragColor = vec4(color, 1.0);
}
