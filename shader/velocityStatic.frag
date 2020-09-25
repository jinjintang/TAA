#version 450
layout (binding = 0) uniform UBO {
	
	
	vec4 _ProjectionExtents;
	mat4 ViewMat;
	mat4 _PrevVP_NoFlip;
	
} ubo;

layout (binding = 1) uniform sampler2D depthMap;

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outFragColor;

float LinearizeDepth(float depth)
{
  float n =0.1; // camera z near
  float f = 64.0; // camera z far
  float z = depth;
  //return (2.0 * n) / (f + n - z * (f - n));	
 return  n*f/ (f- depth * (f - n));	

}

void main() 
{ 

	vec2 vs_ray=(2.0 * inUV - 1.0) * ubo._ProjectionExtents.xy + ubo._ProjectionExtents.zw;

	float depth=texture(depthMap, inUV).r;
	float vs_dist=LinearizeDepth(depth);

	vec3 vs_pos = vec3(vs_ray, -1.0)* vs_dist;

	vec4 ws_pos =inverse(ubo.ViewMat)*vec4(vs_pos, 1.0);

	vec4 rp_cs_pos = ubo._PrevVP_NoFlip*ws_pos;
	vec2 rp_ss_ndc = rp_cs_pos.xy / rp_cs_pos.w;
	vec2 rp_ss_txc = 0.5*rp_ss_ndc + 0.5;

	
	vec2 ss_vel = inUV - rp_ss_txc;
	
	outFragColor = vec4(0.5*ss_vel+0.5,0.0,1.0);
	
	//outFragColor = vec4(vec3(1.0-LinearizeDepth(depth)), 1.0);
	
}