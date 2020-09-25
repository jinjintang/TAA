#version 450

layout (binding = 0) uniform sampler2D _VelocityTex;

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outFragColor;



void main() 
{  
	vec2 du = vec2(1.0/textureSize(_VelocityTex, 0).x, 0.0);
	vec2 dv = vec2(0.0, 1.0/textureSize(_VelocityTex, 0).y);

	vec2 mv = vec2(0.0);
	float dmv = 0.0;

	
	for (int i = -1; i <= 1; i++)
	{
	
		for (int j = -1; j <= 1; j++)
		{
			vec2 v = texture(_VelocityTex, inUV + i * dv + j * du).xy;
			float dv = dot(v, v);
			if (dv > dmv)
			{
				mv = v;
				dmv = dv;
			}
		}
	}

	outFragColor =vec4(mv, 0.0, 1.0);

	
}