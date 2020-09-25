#version 450
layout (location = 0) in vec4 inPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec2 inUV;
layout (binding = 0) uniform UBO 
{
	mat4 _CurrVP;
	mat4 _CurrM;
	mat4 _PrevVP;
	mat4 _PrevM;
} ubo;
layout (location = 0) out vec4 cs_pos;
layout (location = 1) out vec4 ss_pos;
layout (location = 2) out vec3 cs_xy_curr;
layout (location = 3) out vec3 cs_xy_prev;

out gl_PerVertex 
{
	vec4 gl_Position;
};



void main() 
{
	const float occlusion_bias = 0.03;
	vec4 ws_pos_curr = inPos;
	vec4 ws_pos_prev = inPos;
	cs_pos = ubo._CurrVP*ubo._CurrM*ws_pos_curr;
	ss_pos = cs_pos/cs_pos.w;
	ss_pos.z = -(ubo._CurrVP*ubo._CurrM*ws_pos_curr).z - occlusion_bias;// COMPUTE_EYEDEPTH
	cs_xy_curr = cs_pos.xyw;
	cs_xy_prev =(ubo._PrevVP*ubo._PrevM*ws_pos_prev).xyw;
	gl_Position = cs_pos;

}
