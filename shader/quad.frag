#version 450

layout (binding = 0) uniform sampler2D colorMap;

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outFragColor;
//#define USE_MIXED_TONE_MAP
#define MIXED_TONE_MAP_LINEAR_UPPER_BOUND 0.4f


vec3 ToneMapping(vec3 color,float a,float b){

 float luma=0.2126*color.r+0.7152*color.g+0.0722*color.b;
 if (luma<=a)
 return color;
 return color/luma*((a*a-b*luma)/(2*a-b-luma));



}

float Luminance(in vec3 color)
{
#ifdef USE_TONEMAP
	return color.r;
#else
	return dot(color,vec3(0.25f, 0.50f, 0.25f));
#endif
}

vec3 ToneMap(vec3 color)
{
#ifdef USE_MIXED_TONE_MAP
	float luma = Luminance(color);
	if (luma <= MIXED_TONE_MAP_LINEAR_UPPER_BOUND)
	{
		return color;
	}
	else
	{
		return color * (MIXED_TONE_MAP_LINEAR_UPPER_BOUND * MIXED_TONE_MAP_LINEAR_UPPER_BOUND - luma) / (luma * (2 * MIXED_TONE_MAP_LINEAR_UPPER_BOUND - 1 - luma));
	}
#else
	return color / (1 + Luminance(color));
#endif
}

vec3 UnToneMap(vec3 color)
{
#ifdef USE_MIXED_TONE_MAP
	float luma = Luminance(color);
	if (luma <= MIXED_TONE_MAP_LINEAR_UPPER_BOUND)
	{
		return color;
	}
	else
	{
		return color * (MIXED_TONE_MAP_LINEAR_UPPER_BOUND * MIXED_TONE_MAP_LINEAR_UPPER_BOUND - (2 * MIXED_TONE_MAP_LINEAR_UPPER_BOUND - 1) * luma) / (luma * (1 - luma));
	}
#else
	return color / (1 - Luminance(color));
#endif
}

vec3 inverseToneMapping(vec3 color, float a,float b) 
{
 float luma=0.2126*color.r+0.7152*color.g+0.0722*color.b;
   if(luma<=a)
   return color;
   return color/luma*((a*a-(2*a-b)*luma)/(b-luma));
}
vec3 inv_Tonemap_ACES(vec3 x) {
    // Narkowicz 2015, "ACES Filmic Tone Mapping Curve"
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
   return (-0.59 * x + 0.03 - sqrt(-1.0127 * x*x + 1.3702 * x + 0.0009)) / (2.0 * (2.43*x - 2.51));
}
vec3 i_Tonemap_ACES(vec3 x) {
    // Narkowicz 2015, "ACES Filmic Tone Mapping Curve"
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
     return min(vec3(1.0), max(vec3(0.0), ((x / (a / x - b)) * max(x / (c / x - d) - e, vec3(0.001)))));
}
vec3 inv_RRTAndODTFit(vec3 x)
{
    vec3 a = vec3(-0.000090537f)+ x*(vec3(0.0245786f)+x);
    vec3 b = vec3(0.983729f) * x  +0.671032;
    return a / b;
}
void main() 
{  
	
	vec4 color = texture(colorMap,inUV);
	

	outFragColor =color;//vec4(UnToneMap(color.xyz),1.0);//vec4( inverseToneMapping(color.xyz, 0.4,1.0) ,1.0);

	
}