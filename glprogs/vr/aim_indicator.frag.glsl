#version 330 core

in vec2 var_TexCoord;
out vec4 FragColor;

uniform vec4 u_color;

void main() {
    float distFromCenter = distance(var_TexCoord, vec2(0.5, 0.5));
    float attenuation = 1 / (1 + 32*distFromCenter*distFromCenter);
    attenuation *= step(0.2, attenuation);
    FragColor = vec4(1, 1, 1, attenuation) * u_color;
}
