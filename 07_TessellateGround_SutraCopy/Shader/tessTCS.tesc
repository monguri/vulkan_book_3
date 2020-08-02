#version 450

layout(vertices=4) out;

layout(location=0) in vec2 inUV[];
layout(location=0) out vec2 outUV[];

in gl_PerVertex
{
	vec4 gl_Position;
} gl_in[gl_MaxPatchVertices];

layout(set=0, binding=0)
uniform TesseSceneParameters
{
	mat4 world;
	mat4 view;
	mat4 proj;
	vec4 lightDir;
	vec4 cameraPos;
};

layout(set=0, binding=1)
uniform sampler2D texSampler;
layout(set=0, binding=2)
uniform sampler2D normalSampler;

float CalcTessFactor(vec4 v)
{
	return 1.0f;
}

void ComputeTessLevel()
{
	// パッチのgl_inのインデックス
	// 0     1
	// |-----|
	// |     |
	// |     |
	// |-----|
	// 2     3
	//
	vec4 v[4];
	int indices[][2] = {
		{2, 0},
		{0, 1},
		{1, 3},
		{2, 3}
	};

	for (int i = 0; i < 4; ++i)
	{
		int idx0 = indices[i][0];
		int idx1 = indices[i][1];
		v[i] = 0.5f * (gl_in[idx0].gl_Position + gl_in[idx1].gl_Position);
	}

	gl_TessLevelOuter[0] = CalcTessFactor(v[0]);
	gl_TessLevelOuter[2] = CalcTessFactor(v[2]);
	gl_TessLevelInner[0] = 0.5f * (gl_TessLevelOuter[0] + gl_TessLevelOuter[2]);

	gl_TessLevelOuter[1] = CalcTessFactor(v[1]);
	gl_TessLevelOuter[3] = CalcTessFactor(v[3]);
	gl_TessLevelInner[1] = 0.5f * (gl_TessLevelOuter[1] + gl_TessLevelOuter[3]);
}

void main()
{
	if (gl_InvocationID == 0)
	{
		ComputeTessLevel();
	}

	gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;
	outUV[gl_InvocationID] = inUV[gl_InvocationID];
}
