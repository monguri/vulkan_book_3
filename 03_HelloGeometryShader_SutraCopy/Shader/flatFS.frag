#version 450

layout(location=0) in vec3 inColor;

layout(location=0) out vec4 outColor;

layout(set=0, binding=0)
uniform SceneParameters
{
	mat4 world;
	mat4 view;
	mat4 proj;
	vec4 lightDir;
};

void main()
{
	outColor = vec4(inColor, 1.0f);
}

