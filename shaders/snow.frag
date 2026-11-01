// vsgOcean marine snow: a soft round flake, fragment stage. The colour and alpha
// (the light reflected by the particle, faded by the submersion and the surface
// depth) come from the vertex stage; only the round mask is computed here.
#version 450

layout(location = 0) in vec4 particleColor; // rgb: reflected light, a: fade alpha
layout(location = 1) in vec2 corner;
layout(location = 0) out vec4 fragColor;

void main()
{
    const float d = length(corner);
    const float mask = 1.0 - smoothstep(0.7, 1.0, d); // a soft-edged round flake
    if (mask <= 0.0) discard;
    fragColor = vec4(particleColor.rgb, particleColor.a * mask * 0.85);
}
