#version 450

layout(quads, fractional_even_spacing, ccw) in;
layout(location=0) out vec4 outColor;

layout(set=0, binding=0)
uniform TesseSceneParameters
{
	mat4 world;
	mat4 view;
	mat4 proj;
	vec4 lightDir;
	vec4 cameraPos;
	float tessOuterLevel;
	float tessInnerLevel;
};

out gl_PerVertex
{
	vec4 gl_Position;
};

// UV�̒l����A�L���[�r�b�N��Ԃ̊e���̌W�����v�Z���܂Ƃ߂�vec4�ŕԂ�
vec4 bernsteinBasis(float t)
{
	float invT = 1.0f - t;
	return vec4(
		invT * invT * invT, 
		3.0f * t * invT * invT,
		3.0f * t * t * invT,
		t * t * t
	);
}

vec3 CubicInterpolate(vec3 p0, vec3 p1, vec3 p2, vec3 p3, vec4 t)
{
	return p0 * t.x + p1 * t.y + p2 * t.z + p3 * t.w;
}

void main()
{
	vec4 basisU = bernsteinBasis(gl_TessCoord.x);
	vec4 basisV = bernsteinBasis(gl_TessCoord.y);

	vec3 bezpatch[16];

	// �������B���̃V�F�[�_���Ăяo���ꂽ�|�C���g�Ɋ֌W����16�_��gl_in�ɓ����Ă���
	for (int i = 0; i < 16; ++i)
	{
		bezpatch[i] = gl_in[i].gl_Position.xyz;
	}

	vec3 q1 = CubicInterpolate(bezpatch[0], bezpatch[1], bezpatch[2], bezpatch[3], basisU);
	vec3 q2 = CubicInterpolate(bezpatch[4], bezpatch[5], bezpatch[6], bezpatch[7], basisU);
	vec3 q3 = CubicInterpolate(bezpatch[8], bezpatch[9], bezpatch[10], bezpatch[11], basisU);
	vec3 q4 = CubicInterpolate(bezpatch[12], bezpatch[13], bezpatch[14], bezpatch[15], basisU);

	vec3 localPos = CubicInterpolate(q1, q2, q3, q4, basisV);
	gl_Position = proj * view * world * vec4(localPos, 1.0f);

	outColor = vec4(1.0f);
}

