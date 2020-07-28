#version 450

layout(triangles) in;
layout(triangle_strip, max_vertices = 18) out; // triangleを6面分、生成する

layout(location=0) in vec3 inColor[];
layout(location=0) out vec3 outColor;

layout(set=0, binding=0)
uniform CubemapEnvParameters
{
	mat4 world[6];
	vec4 colors[6];
};

layout(set=0, binding=1)
uniform ViewProjMatrices
{
	mat4 view[6];
	mat4 proj;
	vec4 lightDir;
};

in gl_PerVertex
{
	vec4 gl_Position;
} gl_in[];

// これは書いても書かなくてもよい
out gl_PerVertex
{
	vec4 gl_Position;
};

void main()
{
	for (int face = 0; face < 6; ++face)
	{
		// フェイスごとのトライアングル頂点を生成する
		mat4 pv = proj * view[face];
		for (int i = 0; i < gl_in.length(); ++i)
		{
			gl_Position	= pv * gl_in[i].gl_Position;
			gl_Layer = face;
			outColor = inColor[i];
			EmitVertex();
		}

		// trianglestripのストリップはここで分割される
		EndPrimitive();
	}
}

