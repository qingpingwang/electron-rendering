#version 330 core

uniform sampler2D inputTexture0;
uniform sampler2D inputTexture1;
uniform float uProgress;

in vec2 texCoord;
out vec4 FragColor;

void main() {
    vec4 fromColor = texture(inputTexture0, texCoord);
    vec4 toColor = texture(inputTexture1, texCoord);
    FragColor = mix(fromColor, toColor, uProgress);
}
