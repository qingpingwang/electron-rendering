#version 300 es
precision highp float;

// 系统自动传入
uniform sampler2D inputTexture;
uniform vec2 resolution;

// 对外参数（从配置加载）
uniform float brightness;      // float (vec1)
uniform vec2 offset;           // vec2
uniform vec3 colorAdjust;      // vec3
uniform vec4 borderColor;      // vec4

in vec2 vTexCoord;
out vec4 FragColor;

void main() {
    // 应用偏移
    vec2 uv = vTexCoord + offset;
    
    // 采样
    vec4 color = texture(inputTexture, uv);
    
    // 应用亮度
    color.rgb *= brightness;
    
    // 应用颜色调整
    color.rgb *= colorAdjust;
    
    // 简单边框效果（演示 vec4）
    float border = 0.05;
    if (uv.x < border || uv.x > 1.0 - border || 
        uv.y < border || uv.y > 1.0 - border) {
        color = borderColor;
    }
    
    FragColor = color;
}
