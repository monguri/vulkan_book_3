#version 450

layout(quads, fractional_even_spacing, ccw) in;
layout(location=0) in vec2 inUV[];

layout(location=0) out vec3 outNormal;

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

out gl_PerVertex
{
	vec4 gl_Position;
};

void main()
{
	vec4 p0 = mix(gl_in[0].gl_Position, gl_in[1].gl_Position, gl_TessCoord.x);
	vec4 p1 = mix(gl_in[2].gl_Position, gl_in[3].gl_Position, gl_TessCoord.x);
	vec4 pos = mix(p0, p1, gl_TessCoord.y);

	vec2 uv0 = mix(inUV[0], inUV[1], gl_TessCoord.x);
	vec2 uv1 = mix(inUV[2], inUV[3], gl_TessCoord.x);
	vec2 uv = mix(uv0, uv1, gl_TessCoord.y);

	// ハイトマップを参照して頂点位置をバイアス
	float height = texture(texSampler, uv).x;
	pos.y += height * 25;

	gl_Position = proj * view * world * pos;

	// テクスチャだと[0,1]で入っているので[-0.5,0.5]にしてさらに正規化する
	vec3 normal = normalize(texture(normalSampler, uv).xyz - 0.5f);
	outNormal = mat3(world) * normal;
}


