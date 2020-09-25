#version 450
layout (binding = 1) uniform sampler2D depthMap;

layout (location = 0) in vec4 cs_pos;
layout (location = 1) in vec4 ss_pos;
layout (location = 2) in vec3 cs_xy_curr;
layout (location = 3) in vec3 cs_xy_prev;

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
	vec2 ss_txc = ss_pos.xy / ss_pos.w;
	float depth=texture(depthMap, ss_txc).r;
	float scene_d=LinearizeDepth(depth);

	// discard if occluded
	
	if (scene_d < ss_pos.z) {
		discard;
	}
	// compute velocity in ndc
	vec2 ndc_curr = cs_xy_curr.xy / cs_xy_curr.z;
	vec2 ndc_prev = cs_xy_prev.xy / cs_xy_prev.z;

	
	outFragColor = vec4( 0.01*0.5*(ndc_curr - ndc_prev), 0.0, 0.0);
	
	
}