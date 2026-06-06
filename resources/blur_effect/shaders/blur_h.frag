#version 300 es
precision highp float;

uniform sampler2D inputTexture;
uniform vec2 resolution;
uniform float blurRadius;

in vec2 vTexCoord;
out vec4 FragColor;

void main() {
    vec2 texelSize = 1.0 / resolution;
    vec4 color = vec4(0.0);
    float totalWeight = 0.0;
    
    // 水平方向高斯模糊
    int radius = int(blurRadius);
    for (int x = -radius; x <= radius; x++) {
        vec2 offset = vec2(float(x) * texelSize.x, 0.0);
        float weight = exp(-float(x * x) / (2.0 * blurRadius * blurRadius));
        color += texture(inputTexture, vTexCoord + offset) * weight;
        totalWeight += weight;
    }
    
    FragColor = color / totalWeight;
}
