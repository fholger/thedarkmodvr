#version 430

layout (early_fragment_tests) in;

layout (location = 0) in vec3 position;
layout (location = 1) in vec2 uvDiffuse;        
layout (location = 2) in vec2 uvNormal;        
layout (location = 3) in vec2 uvSpecular;       
layout (location = 4) in vec4 uvLight; 
layout (location = 5) in mat3 tangentSpace; 
layout (location = 8) in vec4 color;        
layout (location = 9) in vec4 lightOrigin;
layout (location = 10) in vec4 viewOrigin;
layout (location = 11) in vec4 diffuseColor;
layout (location = 12) in vec4 specularColor;
layout (location = 13) in float cubic;
     
layout (binding = 0) uniform sampler2D texNormal;
layout (binding = 1) uniform sampler2D texLightFalloff;         
layout (binding = 2) uniform sampler2D texLightProjection;         
//layout (binding = 3) uniform samplerCube cubeTexLightProjection;
layout (binding = 4) uniform sampler2D texDiffuse;         
layout (binding = 5) uniform sampler2D texSpecular;        
         
layout (location = 0) out vec4 fragColor;

vec3 lightColor() {
	// compute light projection and falloff 
	vec3 lightColor;
	/*if (cubic == 1.0) {
		vec3 cubeTC = uvLight.xyz * 2.0 - 1.0;
		lightColor = texture(cubeTexLightProjection, cubeTC).rgb;
		float att = clamp(1.0-length(cubeTC), 0.0, 1.0);
		lightColor *= att*att;
	} else*/ {
		vec3 lightProjection = texture2DProj( texLightProjection, uvLight.xyw ).rgb; 
		vec3 lightFalloff = texture2D( texLightFalloff, vec2( uvLight.z, 0.5 ) ).rgb;
		lightColor = lightProjection * lightFalloff;
	}
	return lightColor;
    //return vec3(1,1,1);
}

void main() {
    vec3 lightDir	= lightOrigin.xyz - position;
    vec3 viewDir	= viewOrigin.xyz - position;

    // compute normalized light, view and half angle vectors 
    vec3 L = normalize( lightDir ); 
    vec3 V = normalize( viewDir ); 
    vec3 H = normalize( L + V );

    // compute normal from normal map, move from [0, 1] to [-1, 1] range, normalize 
    vec3 RawN = normalize( ( 2. * texture2D ( texNormal, uvNormal.st ).wyz ) - 1. ); 
    vec3 N = tangentSpace * RawN; 
    float NdotH = clamp( dot( N, H ), 0.0, 1.0 );
    float NdotL = dot( N, L );

	// compute the diffuse term    
	vec3 diffuse = texture2D( texDiffuse, uvDiffuse ).rgb * diffuseColor.rgb;

	// compute the specular term
    float specularPower = 10.0;
    float specularContribution = pow( NdotH, specularPower );
	vec3 specular = texture2D( texSpecular, uvSpecular ).rgb * specularContribution * specularColor.rgb;

	// compute lighting model
    fragColor =  vec4(( diffuse + specular ) * NdotL * lightColor() * color.rgb, 1);
    //fragColor = vec4(diffuse, 1);
} 