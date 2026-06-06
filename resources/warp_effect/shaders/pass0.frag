#version 300 es
precision highp float;
in vec2 vTexCoord;
out vec4 FragColor;

// 内置uniform（引擎自动设置）
uniform float uTime;          // 当前时间（毫秒）
uniform float uProgress;      // 特效进度（0.0-1.0）
uniform sampler2D inputTexture0;  // 输入画面

// 需要声明的uniform（在config.json中配置）
uniform vec2 resolution;      // 画布尺寸
uniform float intensity;      // 扭曲强度
uniform float frequency;      // 波浪频率
uniform float speed;          // 波浪速度

void main() {
    // 归一化坐标（-0.5到0.5）
    vec2 uv = vTexCoord - 0.5;
    
    // 计算距离中心的半径
    float dist = length(uv);
    
    // 计算sin波浪扭曲
    float wave = sin(dist * frequency - uTime * 0.001 * speed) * intensity * 0.1;
    
    // 应用扭曲：根据角度偏移UV
    float angle = atan(uv.y, uv.x);
    vec2 offset = vec2(cos(angle), sin(angle)) * wave;
    
    // 应用扭曲后的UV坐标
    vec2 warpedUV = vTexCoord + offset;
    
    // 确保UV在有效范围内
    warpedUV = clamp(warpedUV, 0.0, 1.0);
    
    // 采样纹理
    FragColor = texture(inputTexture0, warpedUV);
    
    // 可选的边缘淡出效果
    float edgeFade = 1.0 - smoothstep(0.4, 0.5, dist * 2.0);
    FragColor.a *= edgeFade;
}