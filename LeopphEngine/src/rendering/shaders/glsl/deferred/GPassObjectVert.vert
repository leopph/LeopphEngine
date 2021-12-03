#version 430 core

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec2 inTexCoords;

layout (location = 0) out vec3 outFragPos;
layout (location = 1) out vec3 outNormal;
layout (location = 2) out vec2 outTexCoords;
layout (location = 3) out float outClipSpaceZ;

layout (location = 0) uniform mat4 u_ViewProjMat;
uniform mat4 u_ModelMat;
uniform mat4 u_NormalMat;


void main()
{
    vec4 fragPosWorldSpace = vec4(inPos, 1.0) * u_ModelMat;

    outFragPos = fragPosWorldSpace.xyz;
    outNormal = inNormal * mat3(u_NormalMat);
    outTexCoords = inTexCoords;

    gl_Position = fragPosWorldSpace * u_ViewProjMat;
    outClipSpaceZ = gl_Position.z;
}