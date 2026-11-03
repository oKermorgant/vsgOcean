// vsgOcean semantic pass: per-model IDs for annotation (see SemanticPass).
// The camera state comes through the push constants, so one shadow scene
// renders from any number of cameras.
#version 450

layout(push_constant) uniform PushConstants
{
    mat4 projection; // offset 0, provided by the vsg record traversal
    mat4 modelview;  // offset 64, provided by the vsg record traversal
} pc;

layout(location = 0) in vec3 vsg_Vertex;

void main()
{
    gl_Position = pc.projection * pc.modelview * vec4(vsg_Vertex, 1.0);
}
