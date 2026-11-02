// vsgOcean rain: the shared fragment stage. The streaks are thin vertical
// soft lines (the horizontal profile of the billboard); the splash rings are
// expanding annuli. The vertex stage carries the veiling tone and every
// fade, so only the shape mask lives here.
#version 450

layout(location = 0) in vec4 particleColor; // rgb: the veiling tone, a: alpha
layout(location = 1) in vec2 corner;         // the quad corner, [-1,1]
layout(location = 2) flat in float mode;     // 0: a streak, 1: a splash ring
layout(location = 3) flat in float ringPhase; // the ring's cycle phase (rings)

layout(location = 0) out vec4 fragColor;

void main()
{
    float mask;
    if (mode < 0.5)
    {
        // a streak: thin across, softening at both ends (the length ends are
        // the vertical falloff, the width ends already pinch to nothing)
        mask = (1.0 - smoothstep(0.2, 1.0, abs(corner.x)))
             * (1.0 - smoothstep(0.4, 1.0, abs(corner.y)));
    }
    else
    {
        // a splash ring: an expanding annulus of constant relative thickness
        const float d = length(corner);
        const float ring = 0.7 - 0.35 * ringPhase; // the ring radius shrinks relative as the splash grows
        mask = (1.0 - smoothstep(0.08, 0.18, abs(d - ring))) * smoothstep(1.0, 0.9, d);
    }
    if (mask <= 0.0) discard;

    fragColor = vec4(particleColor.rgb, particleColor.a * mask * 0.85);
}
