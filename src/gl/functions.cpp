#include "functions.h"
#include <stb_image/stb_image.h>

namespace vp {
namespace gl {

// 全屏四边形顶点数据（UV Y 轴已翻转以适配 Canvas）
static const float QUAD_VERTICES[] = {
    -1.0f,  1.0f,  0.0f, 1.0f,  // 左上: pos(-1, 1), UV(0, 1)
     1.0f,  1.0f,  1.0f, 1.0f,  // 右上: pos( 1, 1), UV(1, 1)
    -1.0f, -1.0f,  0.0f, 0.0f,  // 左下: pos(-1,-1), UV(0, 0)
     1.0f, -1.0f,  1.0f, 0.0f,  // 右下: pos( 1,-1), UV(1, 0)
};

// ========== 上下文 ==========

bool initContext(GLContext &ctx) {
    if (ctx.initialized)
        return true;

#ifdef __APPLE__
    CGLPixelFormatAttribute attrs[] = {
        kCGLPFAOpenGLProfile,
        (CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core,
        kCGLPFAColorSize,
        (CGLPixelFormatAttribute)24,
        kCGLPFAAlphaSize,
        (CGLPixelFormatAttribute)8,
        kCGLPFAAccelerated,
        kCGLPFAAllowOfflineRenderers,
        (CGLPixelFormatAttribute)0};

    GLint num = 0;
    if (CGLChoosePixelFormat(attrs, &ctx.cgl_pixel_format, &num) != kCGLNoError || num == 0)
        return false;

    if (CGLCreateContext(ctx.cgl_pixel_format, nullptr, &ctx.cgl_context) != kCGLNoError) {
        CGLDestroyPixelFormat(ctx.cgl_pixel_format);
        ctx.cgl_pixel_format = nullptr;
        return false;
    }

    CGLSetCurrentContext(ctx.cgl_context);
    ctx.initialized = true;
    return true;
#else
    return false;
#endif
}

void destroyContext(GLContext &ctx) {
    if (!ctx.initialized)
        return;
#ifdef __APPLE__
    if (ctx.cgl_context) {
        CGLSetCurrentContext(nullptr);
        CGLDestroyContext(ctx.cgl_context);
        ctx.cgl_context = nullptr;
    }
    if (ctx.cgl_pixel_format) {
        CGLDestroyPixelFormat(ctx.cgl_pixel_format);
        ctx.cgl_pixel_format = nullptr;
    }
#endif
    ctx.initialized = false;
}

void makeCurrent(const GLContext &ctx) {
#ifdef __APPLE__
    if (ctx.cgl_context)
        CGLSetCurrentContext(ctx.cgl_context);
#endif
}

std::string getGPUInfo(const GLContext &ctx) {
    if (!ctx.initialized)
        return "N/A";
    makeCurrent(ctx);
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

// ========== 纹理 ==========

Texture createTexture(int width, int height, GLenum internal_format, GLenum format, GLenum type) {
    Texture tex;
    tex.width = width;
    tex.height = height;
    tex.internal_format = internal_format;
    tex.format = format;
    tex.type = type;

    glGenTextures(1, &tex.id);
    glBindTexture(GL_TEXTURE_2D, tex.id);
    glTexImage2D(GL_TEXTURE_2D, 0, internal_format, width, height, 0, format, type, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    return tex;
}

bool updateTexture(Texture &tex, const uint8_t *data, int width, int height) {
    if (!tex.isValid() || !data)
        return false;

    tex.width = width;
    tex.height = height;

    glBindTexture(GL_TEXTURE_2D, tex.id);
    glTexImage2D(GL_TEXTURE_2D, 0, tex.internal_format, width, height, 0, tex.format, tex.type, data);
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

Texture createTextureFromFile(const char *path, bool flip_y) {
    stbi_set_flip_vertically_on_load(flip_y ? 1 : 0);

    int width, height, channels;
    unsigned char *data = stbi_load(path, &width, &height, &channels, 4); // 强制 RGBA

    Texture tex;
    if (!data)
        return tex;

    tex = createTexture(width, height);
    updateTexture(tex, data, width, height);

    stbi_image_free(data);
    return tex;
}

bool bindTexture(const Texture &tex, int unit, GLenum target) {
    if (!tex.isValid())
        return false;
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(target, tex.id);
    return true;
}

bool unbindTexture(int unit, GLenum target) {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(target, 0);
    return true;
}

void destroyTexture(Texture &tex) {
    if (tex.id) {
        glDeleteTextures(1, &tex.id);
        tex.id = 0;
    }
    tex.width = tex.height = 0;
}

// ========== FBO ==========

FBO createFBO(int width, int height, GLenum internal_format, GLenum format, GLenum type) {
    // 复用 createTexture 创建纹理
    Texture tex = createTexture(width, height, internal_format, format, type);
    if (!tex.isValid())
        return FBO{};

    FBO fbo;
    fbo.width = width;
    fbo.height = height;
    fbo.internal_format = internal_format;
    fbo.format = format;
    fbo.type = type;
    fbo.texture = tex.id;

    // 创建 FBO 并附加纹理
    glGenFramebuffers(1, &fbo.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fbo.texture, 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        destroyFBO(fbo);
        return FBO{};
    }

    return fbo;
}

bool bindFBO(const FBO &fbo) {
    if (!fbo.isValid())
        return false;
    glBindFramebuffer(GL_FRAMEBUFFER, fbo.fbo);
    glViewport(0, 0, fbo.width, fbo.height);
    return true;
}

bool unbindFBO() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

void destroyFBO(FBO &fbo) {
    if (fbo.fbo) {
        glDeleteFramebuffers(1, &fbo.fbo);
        fbo.fbo = 0;
    }
    if (fbo.texture) {
        glDeleteTextures(1, &fbo.texture);
        fbo.texture = 0;
    }
    fbo.width = fbo.height = 0;
}

// ========== 网格 ==========

QuadMesh createQuadMesh() {
    QuadMesh mesh;

    glGenVertexArrays(1, &mesh.vao);
    glGenBuffers(1, &mesh.vbo);

    glBindVertexArray(mesh.vao);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(QUAD_VERTICES), QUAD_VERTICES, GL_STATIC_DRAW);

    // pos
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);
    // uv
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
    return mesh;
}

void destroyQuadMesh(QuadMesh &mesh) {
    if (mesh.vao) {
        glDeleteVertexArrays(1, &mesh.vao);
        mesh.vao = 0;
    }
    if (mesh.vbo) {
        glDeleteBuffers(1, &mesh.vbo);
        mesh.vbo = 0;
    }
}

// ========== 渲染 ==========

void cleanColor(float r, float g, float b, float a) {
    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT);
}

bool drawQuad(const QuadMesh &mesh) {
    if (!mesh.isValid())
        return false;
    glBindVertexArray(mesh.vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    return true;
}

bool readPixels(const FBO &fbo, uint8_t *out_buffer, int buffer_size) {
    if (!fbo.isValid() || !out_buffer)
        return false;
    if (buffer_size < fbo.width * fbo.height * 4)
        return false;

    glBindFramebuffer(GL_FRAMEBUFFER, fbo.fbo);
    glReadPixels(0, 0, fbo.width, fbo.height, GL_RGBA, GL_UNSIGNED_BYTE, out_buffer);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

}
} // namespace vp::gl
