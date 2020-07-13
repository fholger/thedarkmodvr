#version 330 core

in vec4 attr_Position;
in vec2 attr_TexCoord;

out vec2 var_EyeTexCoord;
out vec2 var_UITexCoord;

uniform float u_aspectEye;
uniform float u_aspectMirror;

vec2 eyeTexCoords() {
    const vec2 bounds = vec2(0.7, 0.7);
    float scale = u_aspectEye / u_aspectMirror;

    vec2 size;
    if (scale < 1.0) {
        size.x = bounds.x;
        size.y = scale * bounds.y;
    } else {
        size.x * scale * bounds.x;
        size.y = bounds.y;
    }

    vec2 offset = vec2(1.0 - size.x, 1.0 - size.y) * 0.5;
    
    return vec2(offset.x + attr_TexCoord.x * size.x, offset.y + attr_TexCoord.y * size.y);    
}

void main() {
	var_EyeTexCoord = eyeTexCoords();
    var_UITexCoord = attr_TexCoord;
	gl_Position = attr_Position;
}