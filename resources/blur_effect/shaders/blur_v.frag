#version 300 es
precision highp float;

uniform sampler2D blurredH;  // 来自 pass[0] 的输出
uniform vec2 resolution;
uniform float blurRadius;

in vec2 vTexCoord;
out vec4 FragColor;

void main() {
    vec2 texelSize = 1.0 / resolution;
    vec4 color = vec4(0.0);
    float totalWeight = 0.0;
    
    // 垂直方向高斯模糊
    int radius = int(blurRadius);
    for (int y = -radius; y <= radius; y++) {
        vec2 offset = vec2(0.0, float(y) * texelSize.y);
        float weight = exp(-float(y * y) / (2.0 * blurRadius * blurRadius));
        color += texture(blurredH, vTexCoord + offset) * weight;
        totalWeight += weight;
    }
    
    FragColor = color / totalWeight;
}
