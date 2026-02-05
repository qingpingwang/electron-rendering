#version 330 core

uniform sampler2D inputTexture;
uniform vec2 resolution;
uniform float brightness;

in vec2 vTexCoord;
out vec4 FragColor;

void main() {
    vec4 color = texture(inputTexture, vTexCoord);
    FragColor = vec4(color.rgb * brightness, color.a);
}
