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
    vec4 surface;     // x: height scale of the waves (m)
} env;

layout(set = 0, binding = 1) uniform sampler2D sceneColor;
layout(set = 0, binding = 2) uniform sampler2D sceneDepth;

// the scene's point and spot lights (collected in world space each frame): the
// underwater composite ray-marches them as volumetric light cones over the
// submerged segment of each view ray (the 10-slot Composite block is full, so the
// lights travel in this second block, together with the scattering coefficient)
layout(std140, set = 0, binding = 3) uniform Lamps
{
    vec4 header; // x: light count, yzw: per-channel scattering coefficient b (1/m)
    vec4 lamp[32]; // 4 per light: position + intensity, direction + cos(inner),
                   // cos(outer) + range, colour
} lamps;

// interleaved gradient noise (Jimenez): a per-pixel jitter in [0, 1)
float ign(vec2 p)
{
    return fract(52.9829189 * fract(0.06711056 * p.x + 0.00583715 * p.y));
}

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

// The volumetric lamp cones: single scattering ray-marched per light over a
// submerged segment of the view ray (Stonefish-style): the lamp's 1/(d^2+1)
// light with a smooth cone aperture and range cutoff, per-channel Beer-Lambert
// from the lamp to the sample and on to the eye, and the Henyey-Greenstein phase
// (the same g as the sun in-scatter) for the scatter towards the eye.
vec3 lampMarch(vec3 viewDir, float tStart, float tEnd)
{
    vec3 inscatter = vec3(0.0);
    if (tEnd <= tStart || lamps.header.x < 0.5) return inscatter;

    const int lightCount = int(lamps.header.x);
    const float g = 0.924;
    for (int i = 0; i < lightCount; ++i)
    {
        const vec4 posIntensity = lamps.lamp[i * 4 + 0];
        const vec4 dirCosInner = lamps.lamp[i * 4 + 1];
        const vec4 coneRange = lamps.lamp[i * 4 + 2];
        const vec4 lampColor = lamps.lamp[i * 4 + 3];

        // clip the march to the sphere of the light's reach (range 0: far)
        const vec3 lampPos = posIntensity.xyz;
        const float radius = coneRange.y > 0.0 ? coneRange.y : 1.0e5;
        const vec3 oc = lampPos - env.eye.xyz;
        const float projection = dot(oc, viewDir);
        const float disc = projection * projection - (dot(oc, oc) - radius * radius);
        if (disc <= 0.0) continue;
        const float hit = sqrt(disc);
        const float nearT = max(projection - hit, tStart);
        const float farT = min(projection + hit, tEnd);
        if (farT <= nearT) continue;

        // equi-angular sampling (Kulla and Fajardo): the light falls off as 1/(d^2 + 1), so
        // the samples are spread evenly in the angle seen from the lamp instead of evenly
        // in distance. Fixed ~0.5 m steps left a handful of bright blobs along the ray, one
        // per sample near the lamp (the beam read as several stacked ellipsoids); a
        // per-pixel jitter decorrelates the remaining error into fine noise
        const float lampDistance = sqrt(max(dot(oc, oc) - projection * projection, 0.0) + 1.0);
        const float angleNear = atan((nearT - projection) / lampDistance);
        const float angleFar = atan((farT - projection) / lampDistance);
        const int steps = 16;
        const float jitter = ign(gl_FragCoord.xy + float(i) * 17.0);
        for (int s = 0; s < steps; ++s)
        {
            const float angle = mix(angleNear, angleFar, (float(s) + jitter) / float(steps));
            const float along = lampDistance * tan(angle);
            const float t = projection + along;
            const float dt = (angleFar - angleNear) * (lampDistance * lampDistance + along * along) / (lampDistance * float(steps));
            const vec3 p = env.eye.xyz + viewDir * t;
            const vec3 toLamp = lampPos - p;
            const float d = length(toLamp);
            const vec3 incident = -toLamp / max(d, 1.0e-4); // the light's travel direction at the sample

            // the spot aperture: smooth falloff between the bright inner cone and
            // the outer (point lights carry no cone). incident points from the lamp
            // to the sample, so its alignment with the lamp's axis is the cone test
            float aperture = 1.0;
            if (dirCosInner.w > -0.999)
            {
                aperture = smoothstep(coneRange.x, dirCosInner.w, dot(incident, dirCosInner.xyz));
            }
            if (aperture <= 0.0) continue;

            // the light reaching the sample: inverse-square falloff softened near
            // the lamp, Beer-Lambert through the water over the lamp distance. The
            // SDF/vsg light intensities are calibrated for the surface shading pass,
            // so the volumetric term carries a scale bridge between the two
            // conventions (tuned on the night wreck scene: the cone reads bright at
            // the lamp and grades out over its range)
            const float lampScale = 0.125;
            const vec3 arriving = lampColor.rgb * (posIntensity.w * lampScale) * aperture
                                / (d * d + 1.0) * exp(-env.attenuation.rgb * d);

            // scattered towards the eye and attenuated over the remaining water
            // path. The phase is the Henyey-Greenstein peak (the same g as the sun
            // in-scatter) clamped - the raw g = 0.924 peak is so narrow that an
            // oncoming beam saturates to white over metres - plus an isotropic
            // floor: the multiply-scattered light that real water builds, without
            // which a beam is invisible from the side in clear water
            const float cosTheta = dot(incident, -viewDir);
            const float phase = min((1.0 - g * g) / pow(1.0 + g * g - 2.0 * g * cosTheta, 1.5), 12.0) + 0.5;
            inscatter += lamps.header.yzw * phase * arriving * exp(-env.attenuation.rgb * t) * dt;
        }
    }
    return inscatter;
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
    // haze and underwater background - keep the scene color as is, but a submerged
    // camera still sees the volumetric lamp cones against the open water: march the
    // ray out to the surface crossing when looking up, or the fog-out distance
    // (beyond it the lamps' inverse-square falloff makes the contribution negligible)
    if (depth <= 0.0)
    {
        if (env.eye.w > 0.0)
        {
            // the view ray through a background pixel: unprojected at the far plane
            const vec4 backgroundH = env.inverseViewProjection * vec4(ndc, 1.0, 1.0);
            const vec3 backgroundDir = normalize(backgroundH.xyz / backgroundH.w - env.eye.xyz);
            float backgroundEnd;
            if (backgroundDir.z > 1.0e-4) backgroundEnd = (env.extent.w - env.eye.z) / backgroundDir.z;
            else backgroundEnd = 150.0;
            color += lampMarch(backgroundDir, 0.0, backgroundEnd);
        }
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
    else
    {
        // geometry below the waves, seen from above: the ray refracts into the water, so
        // the path is the depth over the cosine of the REFRACTED angle (at most ~1.5x the
        // depth). The straight view ray past the crossing would be hundreds of metres long
        // at a grazing angle and turn a shoreline's submerged terrain, or the surface in a
        // wave trough, into solid water colour (the cyan bands along the horizon). The
        // depth is counted from the wave troughs: the surface itself lies above them
        const float depthBelow = seaLevel - env.surface.x - world.z;
        if (depthBelow > 0.0)
        {
            const float sinView2 = 1.0 - pow((world.z - env.eye.z) / dist, 2.0);
            waterPath = depthBelow / sqrt(max(1.0 - sinView2 / (1.33 * 1.33), 0.05));
        }
    }

    // atmospheric fog over the air segment of the ray: the density is the air's
    // extinction coefficient (Beer-Lambert): haze = 1 - exp(-density * distance).
    // The sea-surface pixels (within the wave band around the mean sea level)
    // take a REDUCED haze: the surface already carries its own sky mirror and
    // water body colour, and the full-strength haze washes the entire far field
    // into one white fog band; the islands, masts and hulls above the band keep
    // the full aerial perspective. The band only spans the wave heights - the
    // storm sea overshoots it, but then the crests are foam-grey anyway.
    // The reduction TAPERS OFF at grazing view angles: the sea right below the
    // horizon takes the full haze again, so it meets the fully hazed sky
    // seamlessly (without the taper the darker far sea reads as a dark line
    // against the fog-white horizon)
    const float surfaceBand = smoothstep(1.2, 0.3, abs(world.z - seaLevel));
    const float grazing = 1.0 - smoothstep(0.0, 0.06, abs(world.z - env.eye.z) / dist);
    const float haze = (1.0 - exp(-env.fog.a * (dist - waterPath))) * (1.0 - 0.45 * surfaceBand * (1.0 - grazing));
    color = mix(color, env.fog.rgb, haze);

    // the underwater depth of field (as osgOcean's underwater DOF): the water's
    // turbidity blurs the view with the water path - near objects stay sharp,
    // distant ones dissolve into the water. The blur strength follows the mean
    // Jerlov beam attenuation, so a coastal water blurs much sooner than a clear
    // oceanic one. The toggle rides in the Lamps block (lamp[2].z - the spare
    // component of the first light's cone slot; the Composite block is full)
    if (env.eye.w > 0.0 && waterPath > 0.0 && lamps.lamp[2].z > 0.5)
    {
        const float beamMean = (env.attenuation.r + env.attenuation.g + env.attenuation.b) / 3.0;
        const float blurAmount = 1.0 - exp(-beamMean * waterPath * 0.8);
        const float radius = blurAmount * 10.0; // pixels
        if (radius > 0.5)
        {
            // an 8-tap disc gather around the pixel (the golden angle spreads
            // the taps evenly), weighted towards the centre
            vec3 blurred = color;
            const vec2 texel = 1.0 / env.extent.xy;
            const float golden = 2.39996323;
            for (int i = 1; i < 8; ++i)
            {
                const float angle = float(i) * golden;
                const vec2 offset = vec2(cos(angle), sin(angle)) * texel * radius;
                blurred += texture(sceneColor, uv + offset).rgb;
            }
            color = mix(color, blurred / 8.0, blurAmount);
        }
    }

    // underwater transport over the water segment: per-channel Beer-Lambert with
    // the in-scattered water light filling in. No ambient floor: the water light
    // is sun-driven only, so a deep night scene attenuates to black
    {
        // the light ON the viewed geometry is filtered by the geometry's own
        // depth below the wave troughs: the models are shaded by the air sun and
        // ambient (the standard vsg material shaders), so the composite applies
        // the vertical diffuse descent exp(-3 Kd z) to the light their pixels
        // carry. A 48 m wreck is no longer air-lit, whichever side the camera is
        // on; the surface pixels sit at the troughs (depth 0) and are untouched
        const float objectDepth = max(seaLevel - env.surface.x - world.z, 0.0);
        color *= exp(-env.attenuation.rgb * 3.0 * objectDepth);

        const vec3 transmit = exp(-env.attenuation.rgb * waterPath);
        // the sun light surviving along the submerged segment, PER CHANNEL
        // (the beam attenuation is the vertical diffuse Kd over 3): red is
        // absorbed first, so the deep background keeps its blue-green instead of
        // dimming with the RGB-mean attenuation, which darkens like red water.
        // The vertical diffuse light exp(-3 Kd z) is INTEGRATED over the segment
        // (straight, so z is linear in the path length) and divided by it: the
        // veil reflects the water the ray actually crosses, not the water at
        // the camera - a deep wreck seen from above is no longer washed out by
        // surface-bright scatter, a mid-descent view no longer veiled by the
        // dark water around the camera
        const float depthStart = env.eye.w > 0.0 ? env.eye.w : 0.0;
        const float depthEnd = max(seaLevel - world.z, 0.0);
        const vec3 diffuseRate = 3.0 * env.attenuation.rgb;
        const vec3 depthStep = diffuseRate * (depthEnd - depthStart);
        // (1 - exp(-a dz)) / (a dz) -> 1 as a dz -> 0 (a horizontal ray)
        const vec3 stepMean = mix((vec3(1.0) - exp(-depthStep)) / max(abs(depthStep), vec3(1.0e-6)),
                                  vec3(1.0), lessThan(abs(depthStep), vec3(1.0e-6)));
        const vec3 light = exp(-diffuseRate * depthStart) * stepMean * max(env.attenuation.w, 0.0);
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

        // the volumetric lamp cones over this ray's submerged segment: from a
        // submerged camera the whole path to the geometry/crossing, from above the
        // segment past the surface crossing
        float lampStart = 0.0;
        float lampEnd = -1.0;
        if (env.eye.w > 0.0) lampEnd = waterPath;
        else if (waterPath > 0.0) { lampEnd = dist; lampStart = dist - waterPath; }
        const vec3 lampInscatter = lampMarch(viewDir, lampStart, lampEnd);

        const vec3 underwater = color * transmit + scatter * (1.0 - transmit) + lampInscatter;

        // the transport applies fully to the water segment; the fade-in only spans
        // the first centimetres as the camera dips through the waves, so there is
        // no clear band just below the surface
        const float inWater = env.eye.w > 0.0 ? smoothstep(0.0, 0.05, env.eye.w) : 1.0;
        color = mix(color, underwater, inWater);
    }

    fragColor = vec4(color, 1.0);
}
