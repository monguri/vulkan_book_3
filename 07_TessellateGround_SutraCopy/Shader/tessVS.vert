#version 450

layout(location=0) in vec4 inPos;

// –{‚¾‚Ævec4 inColor‚Åˆµ‚Á‚Ä‚¢‚é‚ªUV‚ª‚½‚Ü‚½‚Ü‚»‚ê‚Å“®‚¢‚Ä‚¢‚é‚¾‚¯‚Å‚ ‚é
layout(location=1) in vec2 inUV;
layout(location=0) out vec2 outUV;

layout(set=0, binding=0)
uniform TesseSceneParameters
{
	mat4 world;
	mat4 view;
	mat4 proj;
	vec4 lightDir;
	vec4 cameraPos;
};

out gl_PerVertex
{
	vec4 gl_Position;
};

void main()
{
	gl_Position = inPos;
	outUV = inUV;
}

