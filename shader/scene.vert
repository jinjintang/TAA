#version 450

layout (location = 0) in vec4 inPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec2 inUV;


layout (binding = 0) uniform UBO 
{
	mat4 projection;
	mat4 model;
	mat4 view;
} ubo;

layout (location = 0) out vec3 outNormal;
layout (location = 1) out vec2 outUV;
layout (location = 2) out vec3 outLightVec;


out gl_PerVertex
{
	vec4 gl_Position;
};

void main() 
{
	vec4 lightPos=vec4(0,0,0,1);
	// Normal in view space
	mat3 normalMatrix = transpose(inverse(mat3(ubo.view * ubo.model)));
	outNormal = normalMatrix * inNormal;

	
	outUV = inUV;

	mat4 modelView = ubo.view * ubo.model;

	

	
	vec3 lPos =  lightPos.xyz;
	outLightVec = lPos -  vec3(ubo.view * ubo.model * inPos).xyz;
	
	gl_Position = ubo.projection * modelView * vec4(inPos.xyz, 1.0);
	
		
}