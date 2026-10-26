// vsgOcean baseline sky: fullscreen pass, vertex stage
#version 450

layout(push_constant) uniform PushConstants
{
    mat4 projection; // offset 0, provided by the vsg record traversal (per view)
    mat4 modelview;  // offset 64, provided by the vsg record traversal (per view)
} pc;

layout(location = 0) in vec3 position; // clip space position of a fullscreen triangle
layout(location = 0) out vec2 ndc;
layout(location = 1) out vec3 nearPoint; // world position on the near plane
layout(location = 2) out vec3 farPoint;  // world position on the far plane
layout(location = 3) out vec3 cameraEye; // world position of the camera of this view

void main()
{
    ndc = position.xy;

    // the view ray endpoints, from the inverse view projection of this view: the
    // camera state comes through push constants, so one sky node renders correctly
    // from any number of cameras
    const mat4 inverseViewProjection = inverse(pc.projection * pc.modelview);
    const vec4 nearH = inverseViewProjection * vec4(position.xy, -1.0, 1.0);
    const vec4 farH = inverseViewProjection * vec4(position.xy, 1.0, 1.0);
    nearPoint = nearH.xyz / nearH.w;
    farPoint = farH.xyz / farH.w;
    cameraEye = vec3(inverse(pc.modelview) * vec4(0.0, 0.0, 0.0, 1.0));

    gl_Position = vec4(position.xy, 0.0, 1.0);
}
