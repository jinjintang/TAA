#version 450
#define USE_MOTION_BLUR
layout (binding = 0) uniform UBO {
	
	vec4 _SinTime;
	vec4 _FeedbackMin_Max_Mscale;
	vec4 _JitterUV;
	
} ubo;
layout (binding = 1) uniform sampler2D _CameraDepthTexture;
layout (binding = 2) uniform sampler2D _MainTex;
layout (binding = 3) uniform sampler2D _PrevTex;
layout (binding = 4) uniform sampler2D _VelocityNeighborMax;
layout (binding = 5) uniform sampler2D _VelocityBuffer;



layout (location = 0) in vec2 ss_txc;
layout (location = 0) out vec4 outFragColor;


float LinearizeDepth(float depth)
{
  float n = 1.0; // camera z near
  float f = 128.0; // camera z far
  float z = depth;
  return ( n * f) / (-f+ depth * (f - n))/f;	

}
vec3 find_closest_fragment_3x3(vec2 uv)
{	
	vec2 dd =1.0 / textureSize(_CameraDepthTexture, 0);
	vec2 du = vec2(dd.x, 0.0);
	vec2 dv = vec2(0.0, dd.y);

	vec3 dtl = vec3(-1, -1, texture(_CameraDepthTexture, uv - dv - du).x);
	vec3 dtc = vec3( 0, -1, texture(_CameraDepthTexture, uv - dv).x);
	vec3 dtr = vec3( 1, -1, texture(_CameraDepthTexture, uv - dv + du).x);

	vec3 dml = vec3(-1, 0, texture(_CameraDepthTexture, uv - du).x);
	vec3 dmc = vec3( 0, 0, texture(_CameraDepthTexture, uv).x);
	vec3 dmr = vec3( 1, 0, texture(_CameraDepthTexture, uv + du).x);

	vec3 dbl = vec3(-1, 1, texture(_CameraDepthTexture, uv + dv - du).x);
	vec3 dbc = vec3( 0, 1, texture(_CameraDepthTexture, uv + dv).x);
	vec3 dbr = vec3( 1, 1, texture(_CameraDepthTexture, uv + dv + du).x);

	vec3 dmin = dtl;
	if (dmin.z>dtc.z) dmin = dtc;
	if (dmin.z> dtr.z) dmin = dtr;

	if (dmin.z> dml.z) dmin = dml;
	if (dmin.z> dmc.z) dmin = dmc;
	if (dmin.z>dmr.z) dmin = dmr;

	if (dmin.z>dbl.z) dmin = dbl;
	if (dmin.z> dbc.z) dmin = dbc;
	if (dmin.z>dbr.z) dmin = dbr;

	return vec3(uv + dd.xy * dmin.xy, dmin.z);
}
	
vec3 RGB_YCoCg(vec3 c)
{
	// Y = R/4 + G/2 + B/4
	// Co = R/2 - B/2
	// Cg = -R/4 + G/2 - B/4
	return vec3(
			c.x/4.0 + c.y/2.0 + c.z/4.0,
			c.x/2.0 - c.z/2.0,
		-c.x/4.0 + c.y/2.0 - c.z/4.0
	);
}
vec4 sample_color(sampler2D tex, vec2 uv)
{

	vec4 c = texture(tex, uv);
	return vec4(RGB_YCoCg(c.rgb), c.a);


}
vec4 clip_aabb(vec3 aabb_min, vec3 aabb_max, vec4 p, vec4 q)
{
	float FLT_EPS = 0.0001f;
#if USE_OPTIMIZATIONS
	// note: only clips towards aabb center (but fast!)
	vec3 p_clip = 0.5 * (aabb_max + aabb_min);
	vec3 e_clip = 0.5 * (aabb_max - aabb_min) + FLT_EPS;

	vec4 v_clip = q - vec4(p_clip, p.w);
	vec3 v_unit = v_clip.xyz / e_clip;
	vec3 a_unit = abs(v_unit);
	vec ma_unit = max(a_unit.x, max(a_unit.y, a_unit.z));

	if (ma_unit > 1.0)
		return vec4(p_clip, p.w) + v_clip / ma_unit;
	else
		return q;// point inside aabb
#else
	vec4 r = q - p;
	vec3 rmax = aabb_max - p.xyz;
	vec3 rmin = aabb_min - p.xyz;

	const float eps = FLT_EPS;

	if (r.x > rmax.x + eps)
		r *= (rmax.x / r.x);
	if (r.y > rmax.y + eps)
		r *= (rmax.y / r.y);
	if (r.z > rmax.z + eps)
		r *= (rmax.z / r.z);

	if (r.x < rmin.x - eps)
		r *= (rmin.x / r.x);
	if (r.y < rmin.y - eps)
		r *= (rmin.y / r.y);
	if (r.z < rmin.z - eps)
		r *= (rmin.z / r.z);

	return p + r;
#endif
}

vec4 temporal_reprojection(vec2 ss_txc, vec2 ss_vel, float vs_dist)
	{
	
		vec4 texel0 = sample_color(_MainTex, ss_txc-ubo._JitterUV.xy);
	
		vec4 texel1 = sample_color(_PrevTex, ss_txc - ss_vel);
	
		vec2 uv = ss_txc-ubo._JitterUV.xy;

		vec2 du =vec2( 1.0/textureSize(_MainTex, 0).x, 0.0);
		vec2 dv =vec2(0.0,1.0/  textureSize(_MainTex, 0).y);

		vec4 ctl = sample_color(_MainTex, uv - dv - du);
		vec4 ctc = sample_color(_MainTex, uv - dv);
		vec4 ctr = sample_color(_MainTex, uv - dv + du);
		vec4 cml = sample_color(_MainTex, uv - du);
		vec4 cmc = sample_color(_MainTex, uv);
		vec4 cmr = sample_color(_MainTex, uv + du);
		vec4 cbl = sample_color(_MainTex, uv + dv - du);
		vec4 cbc = sample_color(_MainTex, uv + dv);
		vec4 cbr = sample_color(_MainTex, uv + dv + du);

		vec4 cmin = min(ctl, min(ctc, min(ctr, min(cml, min(cmc, min(cmr, min(cbl, min(cbc, cbr))))))));
		vec4 cmax = max(ctl, max(ctc, max(ctr, max(cml, max(cmc, max(cmr, max(cbl, max(cbc, cbr))))))));

		vec4 cavg = (ctl + ctc + ctr + cml + cmc + cmr + cbl + cbc + cbr) / 9.0;
		

		
		vec4 cmin5 = min(ctc, min(cml, min(cmc, min(cmr, cbc))));
		vec4 cmax5 = max(ctc, max(cml, max(cmc, max(cmr, cbc))));
		vec4 cavg5 = (ctc + cml + cmc + cmr + cbc) / 5.0;
		cmin = 0.5 * (cmin + cmin5);
		cmax = 0.5 * (cmax + cmax5);
		cavg = 0.5 * (cavg + cavg5);
	
	/*#elif MINMAX_4TAP_VARYING// this is the method used in v2 (PDTemporalReprojection2)
		float FLT_EPS = 0.0001f;
		const float _SubpixelThreshold = 0.5;
		const float _GatherBase = 0.5;
		const float _GatherSubpixelMotion = 0.1666;

		vec2 texel_vel = ss_vel *textureSize(_MainTex, 0);
		float texel_vel_mag = length(texel_vel) * vs_dist;
		float k_subpixel_motion = clamp(_SubpixelThreshold / (FLT_EPS + texel_vel_mag),0.0,1.0);
		float k_min_max_support = _GatherBase + _GatherSubpixelMotion * k_subpixel_motion;

		vec2 ss_offset01 = k_min_max_support * vec2(-1.0/textureSize(_MainTex, 0).x,1.0/ textureSize(_MainTex, 0).y);
		vec2 ss_offset11 = k_min_max_support * vec2(1.0/textureSize(_MainTex, 0).x, 1.0/textureSize(_MainTex, 0).y);
		vec4 c00 = sample_color(_MainTex, uv - ss_offset11);
		vec4 c10 = sample_color(_MainTex, uv - ss_offset01);
		vec4 c01 = sample_color(_MainTex, uv + ss_offset01);
		vec4 c11 = sample_color(_MainTex, uv + ss_offset11);

		vec4 cmin = min(c00, min(c10, min(c01, c11)));
		vec4 cmax = max(c00, max(c10, max(c01, c11)));

	//	#if USE_YCOCG || USE_CLIPPING
			vec4 cavg = (c00 + c10 + c01 + c11) / 4.0;
	#endif*/
	

		vec2 chroma_extent = vec2(0.25 * 0.5 * (cmax.r - cmin.r));
		vec2 chroma_center = vec2(texel0.gb);
		cmin.yz = chroma_center - chroma_extent;
		cmax.yz = chroma_center + chroma_extent;
		cavg.yz = chroma_center;

		
	
		texel1 = clip_aabb(cmin.xyz, cmax.xyz, clamp(cavg, cmin, cmax), texel1);
		
		float lum0 = texel0.r;
		float lum1 = texel1.r;
	
		float unbiased_diff = abs(lum0 - lum1) / max(lum0, max(lum1, 0.2));
		float unbiased_weight = 1.0 - unbiased_diff;
		float unbiased_weight_sqr = unbiased_weight * unbiased_weight;
		float k_feedback = mix(ubo._FeedbackMin_Max_Mscale.x, ubo._FeedbackMin_Max_Mscale.y, unbiased_weight_sqr);

		// output
		return texel0+(texel1-texel0)*k_feedback;
	}

vec3 YCoCg_RGB(vec3 c)
{
	// R = Y + Co - Cg
	// G = Y + Cg
	// B = Y - Co - Cg
	
	return clamp(vec3(
		c.x + c.y - c.z,
		c.x + c.z,
		c.x - c.y - c.z
	),0.0, 1.0);
}
vec4 resolve_color(vec4 c)
{

	return vec4(YCoCg_RGB(c.rgb).rgb, c.a);

}	
vec4 PDnrand4( vec2 n ) {
	return fract( sin(dot(n.xy, vec2(12.9898f, 78.233f)))* vec4(43758.5453f, 28001.8384f, 50849.4141f, 12996.89f) );
}
vec4 PDsrand4( vec2 n ) {
	return PDnrand4( n ) * 2 - 1;
}

float PDnrand( vec2 n ) {
	return fract( sin(dot(n.xy,vec2(12.9898f, 78.233f)))* 43758.5453f );
}
float PDsrand( vec2 n ) {
	return PDnrand( n ) * 2 - 1;
}
vec4 sample_color_motion( vec2 uv, vec2 ss_vel)
	{
		const vec2 v = 0.5 * ss_vel;
		const int taps = 3;// on either side!

		float srand = PDsrand(uv + ubo._SinTime.xx);
		vec2 vtap = v / taps;
		vec2 pos0 = uv + vtap * (0.5 * srand);
		vec4 accu = vec4(0.0);
		float wsum = 0.0;

		
		for (int i = -taps; i <= taps; i++)
		{
			float w = 1.0;// box
			//float w = taps - abs(i) + 1;// triangle
			//float w = 1.0 / (1 + abs(i));// pointy triangle
			accu += w * sample_color(_MainTex, pos0 + i * vtap);
			wsum += w;
		}

		return accu / wsum;
	}
void main() 
{  
	
	vec2 uv = ss_txc-ubo._JitterUV.xy;
	
	vec3 c_frag = find_closest_fragment_3x3(uv);
	vec2 ss_vel =100.0*texture(_VelocityBuffer,uv).xy;
	vec2 ss_vel_max = texture(_VelocityNeighborMax,uv).xy;


	float vs_dist = LinearizeDepth(c_frag.z);
	
	// temporal resolve
	vec4 color_temporal = temporal_reprojection(ss_txc, ss_vel, vs_dist);

	// prepare outputs
	vec4 to_buffer = resolve_color(color_temporal);
	float _MotionScale=1.0;
		
	ss_vel = _MotionScale *  (2.0*texture(_VelocityNeighborMax,uv).xy-1.0);
		
	float vel_mag = length(ss_vel * vec2(0.0,0.0));
	const float vel_trust_full = 2.0;
	const float vel_trust_none = 15.0;
	const float vel_trust_span = vel_trust_none - vel_trust_full;
	float trust = 1.0 - clamp(vel_mag - vel_trust_full, 0.0, vel_trust_span) / vel_trust_span;

		
	vec4 color_motion = sample_color_motion( ss_txc - ubo._JitterUV.xy, ss_vel);
		

	vec4 to_screen = resolve_color(mix(color_motion, color_temporal, trust));

	
	
	vec4 noise4 = PDsrand4(ss_txc + ubo._SinTime.x + 0.6959174) / 510.0;
	
	outFragColor=clamp((to_buffer + noise4),0.0,1.0);
	
//	outFragColor=texture(_MainTex,uv);
}