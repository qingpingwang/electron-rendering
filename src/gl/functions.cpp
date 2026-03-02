#include "functions.h"
#include "shader.h"
#include <stb_image/stb_image.h>

#ifdef __APPLE__
#include <CoreVideo/CoreVideo.h>
#include <OpenGL/CGLIOSurface.h>
#include <IOSurface/IOSurface.h>
#endif

namespace vp {
namespace gl {

static const float QUAD_VERTICES[] = {
    -1.0f, 1.0f, 0.0f, 1.0f,  // 左上: pos(-1, 1), UV(0, 1)
    1.0f, 1.0f, 1.0f, 1.0f,   // 右上: pos( 1, 1), UV(1, 1)
    -1.0f, -1.0f, 0.0f, 0.0f, // 左下: pos(-1,-1), UV(0, 0)
    1.0f, -1.0f, 1.0f, 0.0f,  // 右下: pos( 1,-1), UV(1, 0)
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
    ctx.egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (ctx.egl_display == EGL_NO_DISPLAY)
        return false;

    if (!eglInitialize(ctx.egl_display, nullptr, nullptr))
        return false;

    EGLint config_attribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE};

    EGLint num_configs = 0;
    if (!eglChooseConfig(ctx.egl_display, config_attribs, &ctx.egl_config, 1, &num_configs) || num_configs == 0) {
        eglTerminate(ctx.egl_display);
        ctx.egl_display = EGL_NO_DISPLAY;
        return false;
    }

    if (!eglBindAPI(EGL_OPENGL_API)) {
        eglTerminate(ctx.egl_display);
        ctx.egl_display = EGL_NO_DISPLAY;
        return false;
    }

    EGLint ctx_attribs[] = {
        EGL_CONTEXT_MAJOR_VERSION, 3,
        EGL_CONTEXT_MINOR_VERSION, 2,
        EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
        EGL_NONE};

    ctx.egl_context = eglCreateContext(ctx.egl_display, ctx.egl_config, EGL_NO_CONTEXT, ctx_attribs);
    if (ctx.egl_context == EGL_NO_CONTEXT) {
        eglTerminate(ctx.egl_display);
        ctx.egl_display = EGL_NO_DISPLAY;
        return false;
    }

    if (!eglMakeCurrent(ctx.egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx.egl_context)) {
        eglDestroyContext(ctx.egl_display, ctx.egl_context);
        eglTerminate(ctx.egl_display);
        ctx.egl_context = EGL_NO_CONTEXT;
        ctx.egl_display = EGL_NO_DISPLAY;
        return false;
    }

    if (!gladLoadGLLoader((GLADloadproc)eglGetProcAddress)) {
        eglDestroyContext(ctx.egl_display, ctx.egl_context);
        eglTerminate(ctx.egl_display);
        ctx.egl_context = EGL_NO_CONTEXT;
        ctx.egl_display = EGL_NO_DISPLAY;
        return false;
    }

    ctx.initialized = true;
    return true;
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
#else
    if (ctx.egl_display != EGL_NO_DISPLAY) {
        eglMakeCurrent(ctx.egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (ctx.egl_context != EGL_NO_CONTEXT) {
            eglDestroyContext(ctx.egl_display, ctx.egl_context);
            ctx.egl_context = EGL_NO_CONTEXT;
        }
        eglTerminate(ctx.egl_display);
        ctx.egl_display = EGL_NO_DISPLAY;
    }
#endif
    ctx.initialized = false;
}

void makeCurrent(const GLContext &ctx) {
#ifdef __APPLE__
    if (ctx.cgl_context)
        CGLSetCurrentContext(ctx.cgl_context);
#else
    if (ctx.egl_display != EGL_NO_DISPLAY && ctx.egl_context != EGL_NO_CONTEXT)
        eglMakeCurrent(ctx.egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx.egl_context);
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

void bindTexture(const Texture &tex, int unit, GLenum target) {
    if (!tex.isValid())
        return;
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(target, tex.id);
}

void unbindTexture(int unit, GLenum target) {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(target, 0);
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

void bindFBO(const FBO &fbo) {
    if (!fbo.isValid())
        return;
    glBindFramebuffer(GL_FRAMEBUFFER, fbo.fbo);
    glViewport(0, 0, fbo.width, fbo.height);
}

void unbindFBO() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
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
    return createQuadMesh(QUAD_VERTICES, sizeof(QUAD_VERTICES));
}

QuadMesh createQuadMesh(const float *vertices, size_t size_bytes) {
    QuadMesh mesh;

    glGenVertexArrays(1, &mesh.vao);
    glGenBuffers(1, &mesh.vbo);

    glBindVertexArray(mesh.vao);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(size_bytes), vertices, GL_STATIC_DRAW);

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

void drawQuad(const QuadMesh &mesh) {
    if (!mesh.isValid())
        return;
    glBindVertexArray(mesh.vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
}

void drawTextureQuad(const FBO &fbo, const Texture &texture, Shader *shader, int unit,
                     const char *uniform_name, const QuadMesh *quad) {
    if (!fbo.isValid() || !texture.isValid() || !shader || !quad)
        return;

    bindFBO(fbo);
    shader->use();
    bindTexture(texture, unit);
    shader->setInt(uniform_name, unit);
    drawQuad(*quad);
    unbindTexture(unit);
    shader->unuse();
    unbindFBO();
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

// ========== 原生缓冲区 → 纹理（零拷贝）==========

#ifdef __APPLE__

static const char *s_nv12_vert = R"(
    #version 330 core
    layout(location = 0) in vec2 aPos;
    layout(location = 1) in vec2 aUV;
    out vec2 vUV;
    void main() {
        gl_Position = vec4(aPos, 0.0, 1.0);
        vUV = aUV;
    }
)";

static const char *s_nv12_frag = R"(
    #version 330 core
    in vec2 vUV;
    out vec4 FragColor;
    uniform sampler2DRect uTexY;
    uniform sampler2DRect uTexUV;
    uniform vec2 uTexSize;
    void main() {
        vec2 coord = vUV * uTexSize;
        float y = texture(uTexY, coord).r;
        vec2 uv = texture(uTexUV, coord * 0.5).rg;
        y = 1.1644 * (y - 0.0625);
        float cb = uv.r - 0.5;
        float cr = uv.g - 0.5;
        FragColor = vec4(
            clamp(y + 1.7928 * cr, 0.0, 1.0),
            clamp(y - 0.2133 * cb - 0.5329 * cr, 0.0, 1.0),
            clamp(y + 2.1124 * cb, 0.0, 1.0),
            1.0);
    }
)";

static struct {
    GLuint rect[2] = {};
    GLuint fbo[2] = {};
    std::unique_ptr<Shader> nv12_shader;
    QuadMesh quad;
    bool ready = false;
} s_ntx;

static void ensureNativeResources() {
    if (s_ntx.ready) return;
    glGenTextures(2, s_ntx.rect);
    glGenFramebuffers(2, s_ntx.fbo);
    s_ntx.nv12_shader = std::make_unique<Shader>(s_nv12_vert, s_nv12_frag);
    s_ntx.quad = createQuadMesh();
    s_ntx.ready = true;
}

static void bindIOSurface(GLuint tex, IOSurfaceRef surface, int plane,
                          GLenum internal_fmt, GLenum fmt, GLenum type, int w, int h) {
    glBindTexture(GL_TEXTURE_RECTANGLE, tex);
    CGLTexImageIOSurface2D(CGLGetCurrentContext(),
                           GL_TEXTURE_RECTANGLE, internal_fmt, w, h, fmt, type, surface, plane);
}

static void attachDrawTarget(Texture &tex, int w, int h) {
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s_ntx.fbo[0]);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex.id, 0);
    glViewport(0, 0, w, h);
}

#endif // __APPLE__

bool updateTextureFromNativeBuffer(Texture &tex, void *native_buf) {
#ifdef __APPLE__
    if (!native_buf || !tex.isValid()) return false;

    auto pixbuf = static_cast<CVPixelBufferRef>(native_buf);
    IOSurfaceRef surface = CVPixelBufferGetIOSurface(pixbuf);
    if (!surface) return false;

    ensureNativeResources();

    int w = static_cast<int>(IOSurfaceGetWidth(surface));
    int h = static_cast<int>(IOSurfaceGetHeight(surface));
    OSType fmt = CVPixelBufferGetPixelFormatType(pixbuf);

    bool nv12 = (fmt == kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange ||
                 fmt == kCVPixelFormatType_420YpCbCr8BiPlanarFullRange);

    if (nv12) {
        if (!s_ntx.nv12_shader || !s_ntx.nv12_shader->isValid()) return false;

        bindIOSurface(s_ntx.rect[0], surface, 0, GL_R8, GL_RED, GL_UNSIGNED_BYTE, w, h);
        bindIOSurface(s_ntx.rect[1], surface, 1, GL_RG8, GL_RG, GL_UNSIGNED_BYTE, w / 2, h / 2);

        attachDrawTarget(tex, w, h);

        s_ntx.nv12_shader->use();
        bindTexture({s_ntx.rect[0], w, h}, 0, GL_TEXTURE_RECTANGLE);
        s_ntx.nv12_shader->setInt("uTexY", 0);
        bindTexture({s_ntx.rect[1], w / 2, h / 2}, 1, GL_TEXTURE_RECTANGLE);
        s_ntx.nv12_shader->setInt("uTexUV", 1);
        s_ntx.nv12_shader->setVec2("uTexSize", static_cast<float>(w), static_cast<float>(h));
        drawQuad(s_ntx.quad);
        s_ntx.nv12_shader->unuse();
    } else {
        bindIOSurface(s_ntx.rect[0], surface, 0, GL_RGBA8, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, w, h);

        glBindFramebuffer(GL_READ_FRAMEBUFFER, s_ntx.fbo[1]);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_RECTANGLE, s_ntx.rect[0], 0);
        attachDrawTarget(tex, w, h);
        glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    tex.width = w;
    tex.height = h;
    return true;
#else
    (void)tex; (void)native_buf;
    return false;
#endif
}

void cleanupNativeTexture() {
#ifdef __APPLE__
    if (!s_ntx.ready) return;
    glDeleteTextures(2, s_ntx.rect);
    glDeleteFramebuffers(2, s_ntx.fbo);
    s_ntx.nv12_shader.reset();
    destroyQuadMesh(s_ntx.quad);
    s_ntx = {};
#endif
}

}
} // namespace vp::gl
