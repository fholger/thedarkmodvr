#version 330 core

in vec2 var_EyeTexCoord;
in vec2 var_UITexCoord;
out vec4 FragColor;

uniform sampler2D u_vrEye;
uniform sampler2D u_ui;

void main() {
    vec4 vrEye = texture(u_vrEye, var_EyeTexCoord);
    vec4 ui = texture(u_ui, var_UITexCoord);
    FragColor.rgb = vrEye.rgb * (1 - ui.a) + ui.rgb * ui.a;
    FragColor.a = 1;
}
