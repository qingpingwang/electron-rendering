#version 300 es
precision highp float;

// 系统自动传入
uniform sampler2D inputTexture;

// 外部参数（用户可调整）
uniform float brightness;

// 内部参数（pass 内部固定值，不对外暴露）
uniform float saturation;
uniform vec3 tint;

in vec2 vTexCoord;
out vec4 FragColor;

void main() {
    vec4 color = texture(inputTexture, vTexCoord);
    
    // 应用亮度（外部参数）
    color.rgb *= brightness;
    
    // 应用饱和度（内部参数）
    vec3 gray = vec3(dot(color.rgb, vec3(0.299, 0.587, 0.114)));
    color.rgb = mix(gray, color.rgb, saturation);
    
    // 应用色调（内部参数）
    color.rgb *= tint;
    
    FragColor = color;
}
