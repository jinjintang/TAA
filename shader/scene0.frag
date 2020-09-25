#version 450


layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec3 inColor;

layout (location = 2) in vec3 inLightVec;

#define ambient 0.1

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




layout (location = 0) out vec4 outFragColor;


vec3 Tonemap_ACES(vec3 x) {
    // Narkowicz 2015, "ACES Filmic Tone Mapping Curve"
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return (x * (a * x + b)) / (x * (c * x + d) + e);
}
vec3 RRTAndODTFit(vec3 x)
{
    vec3 a = x * (x + vec3(0.0245786f)) - vec3(0.000090537f);
  	vec3 b = vec3(0.983729f) * x  +0.671032;
    return a / b;
}
void main() 
{
	
	vec3 N = normalize(inNormal);
	vec3 L = normalize(inLightVec);
	
	vec3 R = normalize(-reflect(L, N));
	vec3 diffuse = max(dot(N, L), ambient) * inColor;

	outFragColor = vec4(diffuse , 1.0);
}