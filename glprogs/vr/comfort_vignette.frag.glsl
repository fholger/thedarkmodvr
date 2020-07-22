#version 330 core

in vec2 var_TexCoord;
out vec4 FragColor;

uniform float u_strength;
uniform vec2 u_center;

void main() {
    float distFromCenter = distance(u_center, var_TexCoord);
    float attenuation = 1 / (1 + 32*u_strength*distFromCenter*distFromCenter);
    attenuation = step(0.5, attenuation);
    FragColor = vec4(0, 0, 0, 1-attenuation);
}
