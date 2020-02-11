uniform block {
	uniform mat4 u_projectionMatrix;
};

struct ShaderParams {
    mat4 modelMatrix;
    mat4 modelViewMatrix;
    vec4 bumpMatrix[2];
    vec4 diffuseMatrix[2];
    vec4 specularMatrix[2];
    mat4 lightProjectionFalloff;
    vec4 colorModulate;
    vec4 colorAdd;
    vec4 lightOrigin;
    vec4 viewOrigin;
    vec4 diffuseColor;
    vec4 specularColor;
    vec4 hasTextureDNS;
    vec4 ambientRimColor;
};

layout (std140) uniform ShaderParamsBlock {
    ShaderParams params[32];
};
uniform int u_idx;

