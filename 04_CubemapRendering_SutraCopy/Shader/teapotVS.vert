#version 450

layout(location=0) in vec4 inPos;
layout(location=1) in vec3 inNormal;

layout(location=0) out vec4 outColor;

out gl_PerVertex
{
	vec4 gl_Position;
};

layout(set=0, binding=0)
uniform CubemapEnvParameters
{
	mat4 world[6];
	mat4 colors[6];
};

layout(set=0, binding=0)
uniform ViewProjMatrices
{
	mat4 view;
	mat4 proj;
	vec4 lightDir;
};

void main()
{
	gl_Position = proj * view * world[gl_InstanceIndex] * inPos;

	vec3 worldNormal = mat3(world[gl_InstanceIndex]) * inNormal;
	float l = dot(worldNormal, normalize(lightDir.xyz)) * 0.5 + 0.5;
	outColor = colors[gl_InstanceIndex] * vec4(l, l, l, 1.0f); // TODO:本だとlを直接乗算しているのでAチャンネルにも影響が出ている
}

