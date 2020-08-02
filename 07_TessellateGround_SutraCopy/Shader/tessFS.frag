#version 450

layout(location=0) in vec3 inNormal;
layout(location=0) out vec4 outColor;

void main()
{
	// 定数バッファのlightDirは使わない
	vec3 lightDir = normalize(vec3(1.0f, 1.0f, 1.0f));
	float lmb = clamp(dot(normalize(inNormal), lightDir), 0.0f, 1.0f);
	outColor = vec4(lmb, lmb, lmb, 1.0f);
}
