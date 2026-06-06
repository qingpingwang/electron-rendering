R"(#version 300 es
    precision highp float;
    in vec2 vUV;
    out vec4 FragColor;

    uniform sampler2D uTex;
    uniform float uTime;
    uniform float uAlpha;

    void main()
    {
        FragColor = texture(uTex, vUV) * uAlpha;
    }
)"