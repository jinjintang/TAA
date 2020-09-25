#version 450

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec2 inUV;
layout (location = 3) in vec3 inColor;

layout ( binding = 0) uniform UBO 
{
	vec4 _SinTime;
	vec4 _FeedbackMin_Max_Mscale;
	mat4 view;
	mat4 projection;
	mat4 model;

	mat4 _CurrM;
	mat4 _CurrVP;
	
} ubo;

layout (location = 0) out vec4 cs_pos;
layout (location = 1) out vec2 ss_txc;


out gl_PerVertex
{
	vec4 gl_Position;
};

void main() 
{

	cs_pos =ubo.projection*ubo.view*ubo.model*vec4(inPos,1);
	ss_txc = inUV;
		
}