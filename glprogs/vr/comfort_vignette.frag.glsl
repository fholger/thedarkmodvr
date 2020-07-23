#version 330 core

in vec2 var_TexCoord;
out vec4 FragColor;

uniform float u_radius;
uniform float u_corridor;
uniform vec2 u_center;

void main() {
    float dist = distance(u_center, var_TexCoord);
    float minRadius = 0.5 * (1.0 - u_radius - u_corridor);
    float maxRadius = 0.5 * (1.0 - u_radius + u_corridor);
    float alpha = clamp((dist - minRadius) / (maxRadius - minRadius), 0.0, 1.0);
    FragColor = vec4(0, 0, 0, alpha);
}
