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
    vec4 params;          // x: time t [s], y: tau [s], z: number of waves, w: depth of the refracted view's water light [m]
    vec4 seaLevel;        // rgb: per-channel vertical Jerlov attenuation (1/m), w: mean sea level (m)
    vec4 amplitude[8];
    vec4 wavenumber[8];
    vec4 omega[8];
    vec4 steepness[8];
    vec4 direction[8];    // xy: horizontal direction of the component
    vec4 fft;             // x: 1 when the surface is the FFT sea (see ocean.vert)
} ocean;
// the sun's visibility to the camera (0 occluded .. 1 clear sky - a ray from
// the camera towards the sun against the world geometry, smoothed) rides in
// the spare amplitude[0].y: the specular glitter, the glare lobe and the
// reflected sun disc need the sun to actually reach the viewer

layout(location = 0) in vec3 worldPosition;
layout(location = 1) in vec3 worldNormal;
layout(location = 2) in vec3 cameraEye; // world position of the camera of this view
layout(location = 3) in vec4 fftCoord;  // undisplaced xy [m], the FFT fade, the Gerstner Jacobian (see ocean.vert)
layout(set = 0, binding = 2) uniform sampler2D fftSlopes;     // (dh/dx, dh/dy, jacobian) of the FFT tile
layout(set = 0, binding = 4) uniform sampler2D detailSlopes;  // the same for the detail cascade
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
    const vec3 zenithNight = vec3(0.0003, 0.0004, 0.0008);
    const vec3 horizonNight = vec3(0.0005, 0.0008, 0.0015);

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
    sky += (sunDisc * 5.0 + glow) * sunColor * sunFade * pow(1.0 - clouds, 3.0) * ocean.amplitude[0].y;

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
    water += exp(-sunAngle / 0.3) * vec3(0.6, 0.95, 1.0) * 0.4 * light * max(sunDirection.z, 0.0) * ocean.amplitude[0].y;
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

// whitecap coverage in [0, 1] of the FFT sea: foam where the horizontal displacement folds
// over itself (the Jacobian of the displacement drops), broken up by noise so it reads as
// patches and streaks rather than the smooth contour of the Jacobian
float foamCoverage()
{
    const float amount = ocean.fft.w;
    if (amount <= 0.0) return 0.0;

    float jacobian;
    if (ocean.fft.x > 0.5)
    {
        jacobian = texture(fftSlopes, fftCoord.xy / ocean.fft.y).z;
        if (ocean.fft.z > 0.0) jacobian += texture(detailSlopes, fftCoord.xy / ocean.fft.z).z - 1.0;
        jacobian = 1.0 + (jacobian - 1.0) * fftCoord.z;
    }
    else
    {
        // the component (Gerstner) sea: the analytic Jacobian accumulated in
        // the vertex stage - the SAME 1/J criterion, thresholds and grain as
        // the FFT sea. The component sea never truly folds though (the
        // normalized steepness q = steepness/(a k N) caps the horizontal
        // compression at ~1%, and the components share one phase so they never
        // align): the raw Jacobian sits near 0.99 everywhere. The compression
        // is amplified by a fixed gain and scaled by the wind speed (the
        // whitecap coverage grows with the wind; direction[0].z is the wind in
        // m/s, as the capillary band uses it; the vertex's Jacobian already
        // carries the sea's grow factor - do not multiply it again) - the crest
        // sharpening localizes the foam on the crest tops, and the wind makes it
        // spread - CUBICALLY (the whitecap coverage rises steeply with the wind:
        // none at 4 m/s, crests at 8, broad at 14) - while the same --foam
        // threshold curve tunes it
        const float wind = pow(clamp(ocean.direction[0].z / 5.0, 0.0, 3.0), 3.0);
        jacobian = 1.0 - (1.0 - fftCoord.w) * 3.5 * wind;
    }

    const float threshold = mix(0.35, 0.95, amount);
    const float folded = 1.0 - smoothstep(threshold - 0.4, threshold, jacobian);
    const float grain = 0.5 + 0.5 * (0.6 * rippleNoise(worldPosition.xy * 1.7) + 0.4 * rippleNoise(worldPosition.xy * 5.3));
    return folded * smoothstep(0.2, 0.7, grain + 0.6 * folded);
}

// ripple detail: drifting octave bands of the noise above, evaluated as slopes per
// pixel on top of the low-frequency Gerstner geometry. Each octave drifts along a
// wind-relative direction at its deep-water phase speed; the amplitude follows the
// sea state (the wave field's largest component). Each octave fades with the PIXEL
// FOOTPRINT - the LOD criterion - so every octave hands over to the next longer one
// with distance and the total slope detail stays roughly constant: the discrete
// Gerstner components (8 coherent plane waves) are broken into organic facet
// variation at every range where their smooth Fresnel bands would otherwise read
// as oily patches. Without the intermediate band there is a spectral gap between
// the swell (metres-tens of metres) and the capillary band (sub-metre): at mid
// distance exactly the patch zone appeared.
vec2 rippleSlope(vec2 p, float t, vec2 foot)
{
    // the capillary ripples follow the WIND (less wind = fewer ripples): 1.0 at the
    // default 5 m/s breeze, a faint 0.2 floor in dead calm (water is never glass), saturated above ~10 m/s.
    // Rain adds its own capillary roughness (impact-plussed water) on top.
    const float sea = 0.2 + 0.8 * pow(clamp(ocean.direction[0].z / 5.0, 0.0, 1.0), 1.5) + clamp((ocean.direction[0].z - 10.0) / 10.0, 0.0, 0.5)
                    + 0.5 * ocean.amplitude[1].y;
    const vec2 wind = normalize(ocean.direction[7].xy + vec2(1e-5, 0.0));
    const mat2 windFrame = mat2(wind.x, -wind.y, wind.y, wind.x);

    // characteristic wavelength [m], wind-relative drift direction [rad], slope at
    // the default sea state - three octave bands: capillary-gravity, capillary,
    // and the intermediate chop that bridges towards the swell
    const vec3 octave[3] = vec3[3](vec3(1.1, 0.0, 0.09), vec3(0.45, 1.1, 0.06), vec3(3.2, -0.7, 0.05));

    vec2 slope = vec2(0.0);
    for (int i = 0; i < 3; ++i)
    {
        const float k = 6.2831853 / octave[i].x;
        const float speed = sqrt(9.81 / k); // deep-water phase speed sqrt(g/k)
        const vec2 dir = windFrame * vec2(cos(octave[i].y), sin(octave[i].y));
        const vec2 q = (p - dir * speed * t) * k; // the noise domain, in radians

        // the octave stays while its wavelength spans enough pixels (~3 to 8);
        // foot is the per-pixel world extent (its major axis - grazing views
        // stretch it along the view)
        const float foot = max(foot.x, foot.y);
        const float fade = smoothstep(3.0, 8.0, octave[i].x / foot);
        if (fade <= 0.0) continue;

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

    // the fragment normal: the Gerstner geometry plus the ripple slopes; each
    // ripple octave fades with its per-pixel footprint (see rippleSlope) and
    // only stirs the surface tangent, never flips it
    const vec2 slope = rippleSlope(worldPosition.xy, t, abs(fwidth(worldPosition.xy)));
    vec3 n = normalize(worldNormal + vec3(-slope.x, -slope.y, 0.0));

    // day factor matching the scene lighting: black at night, no ambient floor
    const float day = smoothstep(-0.05, 0.25, sunDirection.z);

    // the viewer's side of the surface (the mean sea level criterion)
    const float submersion = max(ocean.seaLevel.w - cameraEye.z, 0.0);

    // Fresnel reflectance (Schlick, F0 = 0.02) - TWICE, on purpose:
    // the sun glitter below uses the FULL normal (the sparkle tracks every
    // facet), but the sky/water COLOUR mix uses a fragmented normal: the fine
    // ripple texture at full strength, the smooth Gerstner slope damped to a
    // third. At 8 coherent swell components the undamped slope turns the sky
    // mirror into smooth oil-slick bands tens of pixels wide (the reflection
    // contrast across a wave face is much stronger than the eye expects from
    // a facet spectrum that should be broken at every scale); the damping
    // keeps the swell response but hands the fragmenting to the ripples
    vec3 nMix = normalize(vec3(worldNormal.xy * 0.35 - slope, worldNormal.z));

    // a facet turned away from the viewer cannot be seen: at grazing angles the interpolated
    // swell slope plus the ripples tilt some normals past the view direction, and the sky
    // lookup of their reflection then points below the horizon - the cyan streaks along the
    // horizon and in the troughs. Lift such normals back to just facing the viewer
    if (submersion <= 0.0)
    {
        n = normalize(n + toEye * max(0.04 - dot(n, toEye), 0.0));
        nMix = normalize(nMix + toEye * max(0.04 - dot(nMix, toEye), 0.0));
    }
    const float fresnel = 0.02 + 0.98 * pow(1.0 - max(dot(nMix, toEye), 0.0), 5.0);

    // the sun's specular glitter on the rippled normals, plus a wide glare lobe
    // (the glare is the sun's broad reflection path - in a real sea it is carried
    // by the fine facet statistics, so it follows the FRAGMENTED normal: on the
    // full normal the smooth swell slopes sweep through the wide lobe coherently,
    // the exact oily drifting bands this split exists to avoid; the narrow
    // glitter sparks keep the full normal)
    const vec3 halfway = normalize(toEye + sunDirection);
    const float sunFade = clamp(sunDirection.z * 10.0 + 0.5, 0.0, 1.0);
    // rain flattens the specular terms (the impact-plussed water breaks the
    // mirror path; amplitude[1].y is the rain rate)
    const float glitter = pow(max(dot(n, halfway), 0.0), 256.0) * 4.0 * (1.0 - 0.8 * ocean.amplitude[1].y);
    const float glare = pow(max(dot(nMix, halfway), 0.0), 96.0) * 0.12 * (1.0 - 0.5 * ocean.amplitude[1].y);
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
        vec3 reflected = reflect(-toEye, n);
        reflected.z = abs(reflected.z); // a ray still dipping below the horizon bounces off the next wave
        // an overcast sky is a dim, flat dome: its mirror image darkens the sea instead of
        // veiling the far field in pale grey (the water body dominates under cloud)
        const vec3 reflectedSky = skyColor(reflected, sunDirection, length(fwidth(reflected))) * (1.0 - 0.5 * ocean.sunDirection.w);
        const float viewDistance = length(worldPosition - cameraEye);
        const float skyWeight = mix(0.15, 1.0, smoothstep(20.0, 300.0, viewDistance));

        // the refracted view into the water: the near-surface volume back-scatters the
        // sunlight in the Jerlov colour over a shallow effective depth (~1.5 m, the
        // reason the sea reads bright blue from a boat while the deep is dark), and
        // the deep assumed column closes the open-water look. Where actual GEOMETRY
        // sits behind the surface, the pipeline's composite replaces this term with
        // the honest per-pixel transport of that geometry (the skin blends by its
        // reflectance); over open water this colour is what shows through
        const vec3 intoWater = refract(-toEye, n, 1.0 / 1.33);
        const float scatter = exp(-ocean.waterColor.a * 1.5) * max(sunDirection.z, 0.0) * (1.0 - ocean.sunDirection.w);
        const vec3 throughWater = ocean.waterColor.rgb * scatter
                                 + underwaterColor(intoWater, sunDirection, ocean.params.w) * 0.5;

        // the whitecaps: diffusely lit, so neither the sky mirror nor the sun glitter shows through
        const float foam = foamCoverage();
        const float foamLight = 0.45 + 0.55 * mix(max(dot(n, sunDirection), 0.0), 0.7, ocean.sunDirection.w);
        const vec3 foamColor = vec3(0.92, 0.95, 0.97) * foamLight * day;

        // the cloud cover cubed on the sun path: the glitter survives a light
        // cover but is properly gone by 6-7 oktas (keep in sync with the sky's
        // own disc and glow)
        color = mix(throughWater, reflectedSky, fresnel * skyWeight) + (glitter + glare) * sunColor * sunFade * pow(1.0 - ocean.sunDirection.w, 3.0) * ocean.amplitude[0].y * (1.0 - foam);
        color = mix(color, foamColor, foam);

        // alpha is the water column's opacity towards the scene behind the
        // surface: a DEEP default (15 m two-way at the mean attenuation), so a
        // deep wreck does not shine through the coastal murk while the
        // clearest oceanic types keep some see-through. In the pipeline the
        // composite blends the skin by this weight against the PRE-SURFACE
        // content transported through the true water column (the honest
        // through-view: the shallow shore reads through attenuated, a deep
        // wreck goes dark); standalone the surface renders opaquely
        const float clarity = exp(-ocean.waterColor.a * 15.0);
        alpha = mix(fresnel + (1.0 - fresnel) * (1.0 - clarity), 1.0, foam);
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

        // the transport to the submerged camera (Beer-Lambert over the TRUE view
        // path, with the in-scattered water light filling in) is the composite's
        // job: this pass must not pre-attenuate by the submersion depth on top
        // of it, or the surface from below is dimmed twice - the Snell window
        // sank to a flat veil, its ripples invisible past a few metres
        alpha = 1.0;
    }

    // dithering to avoid banding in the gradients on 8 bit framebuffers
    color += (ign(gl_FragCoord.xy) - 0.5) / 255.0;

    fragColor = vec4(color, alpha);
}
