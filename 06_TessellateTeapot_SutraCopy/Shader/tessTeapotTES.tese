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

// UVの値から、キュービック補間の各項の係数を計算しまとめてvec4で返す
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

// UVの値から、キュービック補間の接線の各項の係数を計算しまとめてvec4で返す
vec4 bernsteinBasisDifferential(float t)
{
	float invT = 1.0f - t;
	return vec4(
		-3.0f * invT * invT, 
		3.0f * (3.0f * t * t  - 4.0f * t + 1.0f),
		3.0f * (-3.0f * t * t + 2.0f * t),
		3.0f * t * t
	);
}

void main()
{
	vec4 basisU = bernsteinBasis(gl_TessCoord.x);
	vec4 basisV = bernsteinBasis(gl_TessCoord.y);

	vec3 bezpatch[16];

	// 初期化。このシェーダが呼び出されたポイントに関係する16点がgl_inに入っている
	for (int i = 0; i < 16; ++i)
	{
		bezpatch[i] = gl_in[i].gl_Position.xyz;
	}

	// U方向とV方向、2つの方向のキュービック補間からの位置の決定
	vec3 q1 = CubicInterpolate(bezpatch[0], bezpatch[1], bezpatch[2], bezpatch[3], basisU);
	vec3 q2 = CubicInterpolate(bezpatch[4], bezpatch[5], bezpatch[6], bezpatch[7], basisU);
	vec3 q3 = CubicInterpolate(bezpatch[8], bezpatch[9], bezpatch[10], bezpatch[11], basisU);
	vec3 q4 = CubicInterpolate(bezpatch[12], bezpatch[13], bezpatch[14], bezpatch[15], basisU);

	vec3 localPos = CubicInterpolate(q1, q2, q3, q4, basisV);
	gl_Position = proj * view * world * vec4(localPos, 1.0f);

	vec3 r1 = CubicInterpolate(bezpatch[0], bezpatch[4], bezpatch[8], bezpatch[12], basisV);
	vec3 r2 = CubicInterpolate(bezpatch[1], bezpatch[5], bezpatch[9], bezpatch[13], basisV);
	vec3 r3 = CubicInterpolate(bezpatch[2], bezpatch[6], bezpatch[10], bezpatch[14], basisV);
	vec3 r4 = CubicInterpolate(bezpatch[3], bezpatch[7], bezpatch[11], bezpatch[15], basisV);

	vec4 basisDiffU = bernsteinBasisDifferential(gl_TessCoord.x);
	vec4 basisDiffV = bernsteinBasisDifferential(gl_TessCoord.y);

	vec3 tangentV = CubicInterpolate(q1, q2, q3, q4, basisDiffV);
	vec3 tangentU = CubicInterpolate(r1, r2, r3, r4, basisDiffU);

	vec3 normal = cross(tangentV, tangentU);
	if (length(normal) > 0.000001f)
	{
		normal = normalize(normal);
	}
	else
	{
		// 蓋の中心と底の中心はコントロールポイントが位置重複していて正しく計算できないので法線は真上と真下にする
		if (localPos.y < 0.0f)
		{
			normal = vec3(0.0f, -1.0f, 0.0f);
		}
		else
		{
			normal = vec3(0.0f, 1.0f, 0.0f);
		}
	}

	outColor.xyz = normal.xyz * 0.5f + 0.5f;
	outColor.a = 1.0f;
}

