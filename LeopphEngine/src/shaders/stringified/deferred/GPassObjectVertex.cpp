#include "C:\Dev\LeopphEngine\LeopphEngine\src/rendering/Shader.hpp"
#include <string>
std::string leopph::impl::Shader::s_GPassObjectVertexSource{ R"#fileContents#(#version 460 core

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec2 inTexCoords;
layout (location = 3) in mat4 inModelMatrix;
layout (location = 7) in mat4 inNormalMatrix;

layout (location = 0) out vec3 outFragPos;
layout (location = 1) out vec3 outNormal;
layout (location = 2) out vec2 outTexCoords;

uniform mat4 viewProjectionMatrix;


void main()
{
    outFragPos = vec3(vec4(inPos, 1.0) * inModelMatrix);
    outNormal = inNormal * mat3(inNormalMatrix);
    outTexCoords = inTexCoords;
    gl_Position = vec4(outFragPos, 1.0) * viewProjectionMatrix;
})#fileContents#" };