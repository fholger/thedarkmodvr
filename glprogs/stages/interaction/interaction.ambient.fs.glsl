#version 140

#pragma tdm_include "stages/interaction/interaction.common.fs.glsl"
     
uniform samplerCube	u_lightFalloffCubemap;
         
uniform float u_gamma, u_minLevel;
uniform vec4 u_rimColor;   
      
out vec4 FragColor;

void main() {         
	calcNormals();
	//stgatilov: without normalization |N| > 1 is possible, which leads to |spec| > 1,
	//which causes white sparklies when antialiasing is enabled (http://forums.thedarkmod.com/topic/19134-aliasing-artefact-white-pixels-near-edges/)
	N = normalize(N);

	// compute the diffuse term     
	vec4 matDiffuse = texture( u_diffuseTexture, var_TexDiffuse );
	vec3 matSpecular = texture( u_specularTexture, var_TexSpecular ).rgb;

	vec3 nViewDir = normalize(var_ViewDirLocal);
	vec3 reflect = - (nViewDir - 2*N*dot(N, nViewDir));

	// compute lighting model     
	vec4 color = params[u_idx].diffuseColor * var_Color, light;
	if (u_cubic == 1.0) {
		//color.rgb = vec3(var_TexLight.z);
		vec3 tl = vec3(var_TexLight.xy/var_TexLight.w, var_TexLight.z) - .5;
		float a = .25 - tl.x*tl.x - tl.y*tl.y - tl.z*tl.z;
		light = vec4(vec3(a*2), 1); // FIXME pass r_lightScale as uniform
	} else {
		vec3 lightProjection = textureProj( u_lightProjectionTexture, var_TexLight.xyw ).rgb; 
		vec3 lightFalloff = texture( u_lightFalloffTexture, vec2( var_TexLight.z, 0.5 ) ).rgb;
		light = vec4(lightProjection * lightFalloff, 1);
	} 

	if (u_cubic == 1.0) {
		vec4 worldN = params[u_idx].modelMatrix * vec4(N, 0); // rotation only
		vec3 cubeTC = var_TexLight.xyz * 2.0 - 1.0;
		// diffuse
		vec4 light1 = texture(u_lightProjectionCubemap, worldN.xyz) * matDiffuse;
		// specualr
		light1.rgb += texture( u_lightFalloffCubemap, reflect, 2 ).rgb * matSpecular;
		light.rgb *= color.rgb * light1.rgb;
		light.a = light1.a;
	} else {
		vec3 light1 = vec3(.5); // directionless half
		light1 += max(dot(N, params[u_idx].lightOrigin.xyz) * (1. - matSpecular) * .5, 0);
		float spec = max(dot(reflect, params[u_idx].lightOrigin.xyz), 0);
		float specPow = clamp((spec*spec), 0.0, 1.1);
		light1 += vec3(spec*specPow*specPow) * matSpecular * 1.0;
		light.a = matDiffuse.a;

		light1.rgb *= color.rgb;
		if (u_minLevel != 0) // home-brewed "pretty" linear
			light1.rgb = light1.rgb * (1.0 - u_minLevel) + vec3(u_minLevel);
		light.rgb *= matDiffuse.rgb * light1;
	}

	if(u_gamma != 1 ) // old-school exponential
		light.rgb = pow(light.rgb, vec3(1.0 / u_gamma));

	if(u_rimColor.a != 0) { // produces no visible speed difference on nVidia 1060, but maybe on some other hardware?..
		float NV = 1-abs(dot(N, nViewDir));
		NV *= NV;
		light.rgb += u_rimColor.rgb * NV * NV;
	}

	FragColor = light;
}
