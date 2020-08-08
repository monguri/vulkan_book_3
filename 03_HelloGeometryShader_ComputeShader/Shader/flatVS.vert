#version 450

layout(location=0) in vec4 inPos;

out gl_PerVertex
{
	vec4 gl_Position;
};

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
	gl_Position = inPos;
}

