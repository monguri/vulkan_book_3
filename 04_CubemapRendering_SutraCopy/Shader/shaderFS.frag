#version 450

layout(location=0) in vec3 inColor;
layout(location=1) in vec3 inNormal;
layout(location=2) in vec3 inWorldPos;

layout(location=0) out vec4 outColor;

layout(set=0, binding=0)
uniform SceneParameters
{
	mat4 world;
	mat4 view;
	mat4 proj;
	vec4 lightDir;
	vec4 cameraPos;
};

layout(set=0, binding=1)
uniform samplerCube samplerColor;

void main()
{
	vec3 incident = normalize(inWorldPos.xyz - cameraPos.xyz);
	vec3 r = reflect(incident, inNormal); //TODO: “üË•ûŒü‚Ì”½Ë‚Å‚æ‚¢H
	outColor = texture(samplerColor, r) * vec4(inColor, 1.0f);
}

