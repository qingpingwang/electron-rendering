#include "gl_renderer.h"

namespace vp
{

    static const char *VS = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
out vec2 vUV;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    vUV = aUV;
}
)";

    // 翻转 Y 的顶点着色器
    static const char *VS_FLIP = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
out vec2 vUV;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    vUV = vec2(aUV.x, 1.0 - aUV.y);
}
)";

    static const char *FS = R"(
#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uTex;
void main() {
    FragColor = texture(uTex, vUV);
}
)";

    // 全屏四边形
    static const float QUAD[] = {
        -1.0f,
        1.0f,
        0.0f,
        0.0f,
        1.0f,
        1.0f,
        1.0f,
        0.0f,
        -1.0f,
        -1.0f,
        0.0f,
        1.0f,
        1.0f,
        -1.0f,
        1.0f,
        1.0f,
    };

    GLRenderer::GLRenderer() = default;
    GLRenderer::~GLRenderer() { cleanup(); }

    bool GLRenderer::init()
    {
        if (initialized_)
            return true;

#ifdef __APPLE__
        CGLPixelFormatAttribute attrs[] = {
            kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core,
            kCGLPFAColorSize, (CGLPixelFormatAttribute)24,
            kCGLPFAAlphaSize, (CGLPixelFormatAttribute)8,
            kCGLPFAAccelerated,
            kCGLPFAAllowOfflineRenderers,
            (CGLPixelFormatAttribute)0};

        GLint num = 0;
        if (CGLChoosePixelFormat(attrs, &cgl_pf_, &num) != kCGLNoError || num == 0)
        {
            return false;
        }
        if (CGLCreateContext(cgl_pf_, nullptr, &cgl_ctx_) != kCGLNoError)
        {
            CGLDestroyPixelFormat(cgl_pf_);
            cgl_pf_ = nullptr;
            return false;
        }
        CGLSetCurrentContext(cgl_ctx_);
#endif

        if (!createShaders())
        {
            cleanup();
            return false;
        }

        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);

        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(QUAD), QUAD, GL_STATIC_DRAW);

        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glBindVertexArray(0);

        glGenTextures(1, &texture_);
        glBindTexture(GL_TEXTURE_2D, texture_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);

        initialized_ = true;
        return true;
    }

    void GLRenderer::cleanup()
    {
#ifdef __APPLE__
        if (cgl_ctx_)
            CGLSetCurrentContext(cgl_ctx_);
#endif

        destroyFBO();
        if (texture_)
        {
            glDeleteTextures(1, &texture_);
            texture_ = 0;
        }
        if (vbo_)
        {
            glDeleteBuffers(1, &vbo_);
            vbo_ = 0;
        }
        if (vao_)
        {
            glDeleteVertexArrays(1, &vao_);
            vao_ = 0;
        }
        if (program_)
        {
            glDeleteProgram(program_);
            program_ = 0;
        }
        if (flip_program_)
        {
            glDeleteProgram(flip_program_);
            flip_program_ = 0;
        }

#ifdef __APPLE__
        if (cgl_ctx_)
        {
            CGLSetCurrentContext(nullptr);
            CGLDestroyContext(cgl_ctx_);
            cgl_ctx_ = nullptr;
        }
        if (cgl_pf_)
        {
            CGLDestroyPixelFormat(cgl_pf_);
            cgl_pf_ = nullptr;
        }
#endif

        width_ = height_ = 0;
        initialized_ = false;
    }

    bool GLRenderer::setSize(int w, int h)
    {
        if (w == width_ && h == height_ && fbo_ != 0)
            return true;
        width_ = w;
        height_ = h;
        return createFBO();
    }

    void GLRenderer::uploadTexture(const uint8_t *data, int w, int h)
    {
        if (!initialized_ || !data)
            return;
#ifdef __APPLE__
        CGLSetCurrentContext(cgl_ctx_);
#endif
        glBindTexture(GL_TEXTURE_2D, texture_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    void GLRenderer::render()
    {
        if (!initialized_ || fbo_ == 0)
            return;
#ifdef __APPLE__
        CGLSetCurrentContext(cgl_ctx_);
#endif
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        glViewport(0, 0, width_, height_);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(program_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture_);
        glUniform1i(tex_loc_, 0);

        glBindVertexArray(vao_);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glBindVertexArray(0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void GLRenderer::readPixels(uint8_t *out, int size)
    {
        if (!initialized_ || fbo_ == 0 || !out)
            return;
        if (size < width_ * height_ * 4)
            return;
#ifdef __APPLE__
        CGLSetCurrentContext(cgl_ctx_);
#endif

        // 第二次 draw：将 fbo_ 翻转渲染到 flip_fbo_
        // 注意：因为 QUAD 的 UV 已经有 Y 翻转（屏幕顶部对应 UV.y=0），
        // 使用普通着色器（不翻转 UV）反而会产生翻转效果
        glBindFramebuffer(GL_FRAMEBUFFER, flip_fbo_);
        glViewport(0, 0, width_, height_);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(program_); // 使用普通着色器实现翻转
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, fbo_tex_); // 读取第一个 FBO 的纹理
        glUniform1i(tex_loc_, 0);

        glBindVertexArray(vao_);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glBindVertexArray(0);

        // 从翻转后的 FBO 读取像素
        glReadPixels(0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE, out);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    std::string GLRenderer::getGPUInfo() const
    {
        if (!initialized_)
            return "N/A";
#ifdef __APPLE__
        CGLSetCurrentContext(cgl_ctx_);
#endif
        const char *vendor = (const char *)glGetString(GL_VENDOR);
        const char *renderer = (const char *)glGetString(GL_RENDERER);
        std::string info;
        if (vendor)
            info += vendor;
        if (vendor && renderer)
            info += " - ";
        if (renderer)
            info += renderer;
        return info.empty() ? "N/A" : info;
    }

    bool GLRenderer::createShaders()
    {
        // 主渲染着色器
        GLuint vs = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vs, 1, &VS, nullptr);
        glCompileShader(vs);

        GLint ok;
        glGetShaderiv(vs, GL_COMPILE_STATUS, &ok);
        if (!ok)
        {
            glDeleteShader(vs);
            return false;
        }

        GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fs, 1, &FS, nullptr);
        glCompileShader(fs);

        glGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
        if (!ok)
        {
            glDeleteShader(vs);
            glDeleteShader(fs);
            return false;
        }

        program_ = glCreateProgram();
        glAttachShader(program_, vs);
        glAttachShader(program_, fs);
        glLinkProgram(program_);
        glDeleteShader(vs);
        glDeleteShader(fs);

        glGetProgramiv(program_, GL_LINK_STATUS, &ok);
        if (!ok)
        {
            glDeleteProgram(program_);
            program_ = 0;
            return false;
        }

        tex_loc_ = glGetUniformLocation(program_, "uTex");

        // 翻转着色器
        GLuint vs_flip = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vs_flip, 1, &VS_FLIP, nullptr);
        glCompileShader(vs_flip);

        glGetShaderiv(vs_flip, GL_COMPILE_STATUS, &ok);
        if (!ok)
        {
            glDeleteShader(vs_flip);
            return false;
        }

        GLuint fs_flip = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fs_flip, 1, &FS, nullptr); // 复用片段着色器
        glCompileShader(fs_flip);

        glGetShaderiv(fs_flip, GL_COMPILE_STATUS, &ok);
        if (!ok)
        {
            glDeleteShader(vs_flip);
            glDeleteShader(fs_flip);
            return false;
        }

        flip_program_ = glCreateProgram();
        glAttachShader(flip_program_, vs_flip);
        glAttachShader(flip_program_, fs_flip);
        glLinkProgram(flip_program_);
        glDeleteShader(vs_flip);
        glDeleteShader(fs_flip);

        glGetProgramiv(flip_program_, GL_LINK_STATUS, &ok);
        if (!ok)
        {
            glDeleteProgram(flip_program_);
            flip_program_ = 0;
            return false;
        }

        flip_tex_loc_ = glGetUniformLocation(flip_program_, "uTex");

        return true;
    }

    bool GLRenderer::createFBO()
    {
        if (!initialized_)
            return false;
#ifdef __APPLE__
        CGLSetCurrentContext(cgl_ctx_);
#endif
        destroyFBO();

        // FBO 1: 主渲染目标
        glGenTextures(1, &fbo_tex_);
        glBindTexture(GL_TEXTURE_2D, fbo_tex_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, 0);

        glGenFramebuffers(1, &fbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fbo_tex_, 0);

        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE)
        {
            destroyFBO();
            return false;
        }

        // FBO 2: 翻转后的输出目标
        glGenTextures(1, &flip_fbo_tex_);
        glBindTexture(GL_TEXTURE_2D, flip_fbo_tex_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, 0);

        glGenFramebuffers(1, &flip_fbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, flip_fbo_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, flip_fbo_tex_, 0);

        status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        if (status != GL_FRAMEBUFFER_COMPLETE)
        {
            destroyFBO();
            return false;
        }

        return true;
    }

    void GLRenderer::destroyFBO()
    {
        if (fbo_)
        {
            glDeleteFramebuffers(1, &fbo_);
            fbo_ = 0;
        }
        if (fbo_tex_)
        {
            glDeleteTextures(1, &fbo_tex_);
            fbo_tex_ = 0;
        }
        if (flip_fbo_)
        {
            glDeleteFramebuffers(1, &flip_fbo_);
            flip_fbo_ = 0;
        }
        if (flip_fbo_tex_)
        {
            glDeleteTextures(1, &flip_fbo_tex_);
            flip_fbo_tex_ = 0;
        }
    }

} // namespace vp
