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

// ニア、ファーの基準距離とそこでのテッセレーション係数を固定で決めておいて、現在の距離からLerpでテッセレーション係数を決定する
float CalcTessFactor(vec4 v)
{
	const float tessNear = 2.0f;
	const float tessFar = 150.0f;
	const float MaxTessFactor = 32.0f;

	float dist = length((world * v).xyz - cameraPos.xyz);

	// [tessNear, tessFar]を[1, MaxTessFactor]にLerpする
	float val = MaxTessFactor - (MaxTessFactor - 1) * (dist - tessNear) / (tessFar - tessNear);

	// 距離がtessNear以下、tessFar以上のときもあるのでclamp
	val = clamp(val, 1, MaxTessFactor);
	return val;
}

float CalcNormalBias(vec4 p, vec3 n)
{
	// 法線とカメラベクトルの角度からsin^2を計算し、
	// [閾値,1]との間でLerpする
	const float normalThreshold = 0.85f;

	vec3 fromCamera = normalize(p.xyz - cameraPos.xyz);
	float cos2 = dot(n, fromCamera);
	cos2 *= cos2;

	float normalFactor = 1.0f - cos2;
	float bias = max(normalFactor - normalThreshold, 0.0f) / (1.0f - normalThreshold);
	return bias * 32.0f;
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
	vec3 n[4];
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

		vec2 uv = 0.5f * (inUV[idx0] + inUV[idx1]);
		n[i] = texture(normalSampler, uv).xyz;
		// テクスチャだと[0,1]で入っているので[-0.5,0.5]にしてさらに正規化する
		n[i] = normalize(n[i] - 0.5f);
	}

	gl_TessLevelOuter[0] = CalcTessFactor(v[0]) + CalcNormalBias(v[0], n[0]);
	gl_TessLevelOuter[2] = CalcTessFactor(v[2]) + CalcNormalBias(v[2], n[2]);
	gl_TessLevelInner[0] = 0.5f * (gl_TessLevelOuter[0] + gl_TessLevelOuter[2]);

	gl_TessLevelOuter[1] = CalcTessFactor(v[1]) + CalcNormalBias(v[1], n[1]);
	gl_TessLevelOuter[3] = CalcTessFactor(v[3]) + CalcNormalBias(v[3], n[3]);
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
