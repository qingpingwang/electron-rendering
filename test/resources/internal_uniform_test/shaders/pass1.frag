#version 330 core

// 系统自动传入
uniform sampler2D inputTexture;

// 来自 pass0 的输出
uniform sampler2D pass0Tex;

// 外部参数（用户可调整）
uniform float brightness;

// 内部参数（pass 内部固定值）
uniform float vignetteStrength;
uniform vec3 vignetteColor;

in vec2 vTexCoord;
out vec4 FragColor;

void main() {
    vec4 color = texture(pass0Tex, vTexCoord);
    
    // 应用暗角效果（内部参数）
    vec2 center = vec2(0.5, 0.5);
    float dist = distance(vTexCoord, center);
    float vignette = smoothstep(0.8, 0.3, dist);
    color.rgb = mix(vignetteColor, color.rgb, vignette * (1.0 - vignetteStrength) + vignetteStrength);
    
    FragColor = color;
}
