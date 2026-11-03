// vsgOcean semantic pass, fragment: the pixel carries the ID of the model it
// belongs to, unlit and untextured - the exact bytes survive the UNORM target,
// so the readback is the ID (see SemanticPass). The ID rides the push
// constants behind the camera block (projection, modelview) the record
// traversal provides.
#version 450

layout(push_constant) uniform Constants
{
    mat4 padProjection; // offset 0, the record traversal's camera block
    mat4 padModelview;  // offset 64
    vec4 id;            // offset 128, bound per model by the semantic pass
} pc;

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = pc.id;
}
