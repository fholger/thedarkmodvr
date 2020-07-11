#version 330 core

in vec2 var_TexCoord0;
in vec2 var_TexCoord1;
out vec4 FragColor;

uniform sampler2D u_vrEye;
uniform sampler2D u_ui;

void main() {
    vec4 vrEye = texture(u_vrEye, var_TexCoord0);
    vec4 ui = texture(u_ui, var_TexCoord1);
    FragColor.rgb = vrEye.rgb * (1 - ui.a) + ui.rgb * ui.a;
    FragColor.a = 1;
}
