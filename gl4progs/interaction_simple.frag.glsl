#version 430

layout (early_fragment_tests) in;

in GeomOut{
	vec3 position;
	vec2 uvDiffuse;
	vec2 uvNormal;
	vec2 uvSpecular;
	vec4 uvLight;
	mat3 tangentSpace;
	vec4 color;
	vec4 lightOrigin;
	vec4 viewOrigin;
	vec4 diffuseColor;
	vec4 specularColor;
	float cubic;
} IN;
     
layout (binding = 0) uniform sampler2D texNormal;
layout (binding = 1) uniform sampler2D texLightFalloff;         
layout (binding = 2) uniform sampler2D texLightProjection;         
layout (binding = 3) uniform samplerCube cubeTexLightProjection;
layout (binding = 4) uniform sampler2D texDiffuse;         
layout (binding = 5) uniform sampler2D texSpecular;        
         
layout (location = 0) out vec4 fragColor;

vec3 lightColor() {
	// compute light projection and falloff 
	vec3 lightColor;
	if (IN.cubic == 1.0) {
		vec3 cubeTC = IN.uvLight.xyz * 2.0 - 1.0;
		lightColor = texture(cubeTexLightProjection, cubeTC).rgb;
		float att = clamp(1.0-length(cubeTC), 0.0, 1.0);
		lightColor *= att*att;
	} else {
		vec3 lightProjection = texture2DProj( texLightProjection, IN.uvLight.xyw ).rgb; 
		vec3 lightFalloff = texture2D( texLightFalloff, vec2( IN.uvLight.z, 0.5 ) ).rgb;
		lightColor = lightProjection * lightFalloff;
	}
	return lightColor;
}

void main() {
    vec3 lightDir	= IN.lightOrigin.xyz - IN.position;
	vec3 viewDir = IN.viewOrigin.xyz - IN.position;

    // compute normalized light, view and half angle vectors 
    vec3 L = normalize( lightDir ); 
    vec3 V = normalize( viewDir ); 
    vec3 H = normalize( L + V );

    // compute normal from normal map, move from [0, 1] to [-1, 1] range, normalize 
	vec3 RawN = normalize( ( 2. * texture2D ( texNormal, IN.uvNormal.st ).wyz ) - 1. );
	vec3 N = IN.tangentSpace * RawN;
    float NdotH = clamp( dot( N, H ), 0.0, 1.0 );
    float NdotL = dot( N, L );

	// compute the diffuse term    
	vec3 diffuse = texture2D( texDiffuse, IN.uvDiffuse ).rgb * IN.diffuseColor.rgb;

	// compute the specular term
    float specularPower = 10.0;
    float specularContribution = pow( NdotH, specularPower );
	vec3 specular = texture2D( texSpecular, IN.uvSpecular ).rgb * specularContribution * IN.specularColor.rgb;

	// compute lighting model
	fragColor = vec4( ( diffuse + specular ) * NdotL * lightColor() * IN.color.rgb, 1 );
} 