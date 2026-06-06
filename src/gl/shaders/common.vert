R"(#version 300 es
    layout(location = 0) in vec2 aPos;
    layout(location = 1) in vec2 aUV;

    uniform mat4 uModel;

    out vec2 vUV;

    void main()
    {
        gl_Position = uModel * vec4(aPos, 0.0, 1.0);
        vUV = aUV;
    }
)"