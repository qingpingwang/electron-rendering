#include "functions.h"
#include "shader.h"

#include "include/codec/SkCodec.h"
#include "include/core/SkBitmap.h"
#include "include/core/SkData.h"

#if defined(__APPLE__)
#include <EGL/eglext_angle.h>
#include <CoreVideo/CoreVideo.h>
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

static EGLDisplay createEGLDisplay() {
    auto getPlatformDisplay = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
        eglGetProcAddress("eglGetPlatformDisplayEXT"));

#if defined(__APPLE__)
    if (getPlatformDisplay) {
        static const EGLint attribs[] = {
            EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE, EGL_NONE};
        EGLDisplay display = getPlatformDisplay(EGL_PLATFORM_ANGLE_ANGLE, nullptr, attribs);
        if (display != EGL_NO_DISPLAY)
            return display;
    }
#else
    if (getPlatformDisplay) {
        EGLDisplay display =
            getPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
        if (display != EGL_NO_DISPLAY)
            return display;
    }
#endif
    return eglGetDisplay(EGL_DEFAULT_DISPLAY);
}

bool initContext(GLContext &ctx) {
    if (ctx.initialized)
        return true;

    ctx.egl_display = createEGLDisplay();
    if (ctx.egl_display == EGL_NO_DISPLAY)
        return false;

    if (!eglInitialize(ctx.egl_display, nullptr, nullptr))
        return false;

    if (!eglBindAPI(EGL_OPENGL_ES_API))
        return false;

    const EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE};

    EGLConfig egl_config = nullptr;
    EGLint num_configs = 0;
    if (!eglChooseConfig(ctx.egl_display, config_attribs, &egl_config, 1, &num_configs) ||
        num_configs == 0) {
        eglTerminate(ctx.egl_display);
        ctx.egl_display = EGL_NO_DISPLAY;
        return false;
    }

    const EGLint surface_attribs[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    ctx.egl_surface = eglCreatePbufferSurface(ctx.egl_display, egl_config, surface_attribs);
    if (ctx.egl_surface == EGL_NO_SURFACE) {
        eglTerminate(ctx.egl_display);
        ctx.egl_display = EGL_NO_DISPLAY;
        return false;
    }

    const EGLint ctx_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    ctx.egl_context =
        eglCreateContext(ctx.egl_display, egl_config, EGL_NO_CONTEXT, ctx_attribs);
    if (ctx.egl_context == EGL_NO_CONTEXT) {
        eglDestroySurface(ctx.egl_display, ctx.egl_surface);
        eglTerminate(ctx.egl_display);
        ctx.egl_surface = EGL_NO_SURFACE;
        ctx.egl_display = EGL_NO_DISPLAY;
        return false;
    }

    if (!eglMakeCurrent(ctx.egl_display, ctx.egl_surface, ctx.egl_surface, ctx.egl_context)) {
        eglDestroyContext(ctx.egl_display, ctx.egl_context);
        eglDestroySurface(ctx.egl_display, ctx.egl_surface);
        eglTerminate(ctx.egl_display);
        ctx.egl_context = EGL_NO_CONTEXT;
        ctx.egl_surface = EGL_NO_SURFACE;
        ctx.egl_display = EGL_NO_DISPLAY;
        return false;
    }

    ctx.initialized = true;
    return true;
}

void destroyContext(GLContext &ctx) {
    if (!ctx.initialized)
        return;
    if (ctx.egl_display != EGL_NO_DISPLAY) {
        eglMakeCurrent(ctx.egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (ctx.egl_context != EGL_NO_CONTEXT) {
            eglDestroyContext(ctx.egl_display, ctx.egl_context);
            ctx.egl_context = EGL_NO_CONTEXT;
        }
        if (ctx.egl_surface != EGL_NO_SURFACE) {
            eglDestroySurface(ctx.egl_display, ctx.egl_surface);
            ctx.egl_surface = EGL_NO_SURFACE;
        }
        eglTerminate(ctx.egl_display);
        ctx.egl_display = EGL_NO_DISPLAY;
    }
    ctx.initialized = false;
}

bool makeCurrent(const GLContext &ctx) {
    if (ctx.egl_display == EGL_NO_DISPLAY || ctx.egl_context == EGL_NO_CONTEXT)
        return false;
    return eglMakeCurrent(ctx.egl_display, ctx.egl_surface, ctx.egl_surface, ctx.egl_context) == EGL_TRUE;
}

void releaseCurrent(const GLContext &ctx) {
    if (ctx.egl_display != EGL_NO_DISPLAY)
        eglMakeCurrent(ctx.egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
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
    glTexImage2D(GL_TEXTURE_2D, 0, tex.internal_format, width, height, 0, tex.format, tex.type,
                 data);
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

Texture createTextureFromFile(const char *path, bool flip_y) {
    auto sk_data = SkData::MakeFromFileName(path);
    if (!sk_data)
        return Texture{};

    auto codec = SkCodec::MakeFromData(sk_data);
    if (!codec)
        return Texture{};

    SkImageInfo info = codec->getInfo()
                           .makeColorType(kRGBA_8888_SkColorType)
                           .makeAlphaType(kUnpremul_SkAlphaType);
    SkBitmap bitmap;
    bitmap.allocPixels(info);
    if (!bitmap.getPixels())
        return Texture{};

    if (codec->getPixels(info, bitmap.getPixels(), bitmap.rowBytes()) != SkCodec::kSuccess)
        return Texture{};

    if (flip_y) {
        int h = info.height();
        int row_bytes = static_cast<int>(bitmap.rowBytes());
        std::vector<uint8_t> tmp(row_bytes);
        auto *pixels = static_cast<uint8_t *>(bitmap.getPixels());
        for (int i = 0; i < h / 2; ++i) {
            memcpy(tmp.data(), pixels + i * row_bytes, row_bytes);
            memcpy(pixels + i * row_bytes, pixels + (h - 1 - i) * row_bytes, row_bytes);
            memcpy(pixels + (h - 1 - i) * row_bytes, tmp.data(), row_bytes);
        }
    }

    Texture tex = createTexture(info.width(), info.height());
    updateTexture(tex, static_cast<const uint8_t *>(bitmap.getPixels()), info.width(),
                  info.height());
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

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          (void *)(2 * sizeof(float)));
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

// ========== 原生缓冲区 → 纹理（macOS CVPixelBuffer NV12）==========

#if defined(__APPLE__)

// NV12 → RGBA 转换 shader（GLES3，使用 GL_TEXTURE_2D）
static const char *s_nv12_vert = R"(#version 300 es
    layout(location = 0) in vec2 aPos;
    layout(location = 1) in vec2 aUV;
    out vec2 vUV;
    void main() {
        gl_Position = vec4(aPos, 0.0, 1.0);
        vUV = aUV;
    }
)";

static const char *s_nv12_frag = R"(#version 300 es
    precision highp float;
    in vec2 vUV;
    out vec4 FragColor;
    uniform sampler2D uTexY;
    uniform sampler2D uTexUV;
    void main() {
        float y = texture(uTexY, vUV).r;
        vec2 uv = texture(uTexUV, vUV).rg;
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
    GLuint y_tex = 0;
    GLuint uv_tex = 0;
    GLuint fbo = 0;
    std::unique_ptr<Shader> nv12_shader;
    QuadMesh quad;
    bool ready = false;
} s_ntx;

static void ensureNativeResources() {
    if (s_ntx.ready)
        return;
    glGenTextures(1, &s_ntx.y_tex);
    glGenTextures(1, &s_ntx.uv_tex);
    glGenFramebuffers(1, &s_ntx.fbo);
    s_ntx.nv12_shader = std::make_unique<Shader>(s_nv12_vert, s_nv12_frag);
    s_ntx.quad = createQuadMesh();
    s_ntx.ready = true;
}

static void uploadPlane(GLuint tex, GLenum internal_fmt, GLenum fmt, int w, int h,
                        const void *data, size_t stride) {
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(stride / (fmt == GL_RG ? 2 : 1)));
    glTexImage2D(GL_TEXTURE_2D, 0, internal_fmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, data);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
}

#endif // __APPLE__

bool updateTextureFromNativeBuffer(Texture &tex, void *native_buf) {
#if defined(__APPLE__)
    if (!native_buf || !tex.isValid())
        return false;

    auto pixbuf = static_cast<CVPixelBufferRef>(native_buf);
    int w = static_cast<int>(CVPixelBufferGetWidth(pixbuf));
    int h = static_cast<int>(CVPixelBufferGetHeight(pixbuf));
    OSType fmt = CVPixelBufferGetPixelFormatType(pixbuf);

    bool nv12 = (fmt == kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange ||
                 fmt == kCVPixelFormatType_420YpCbCr8BiPlanarFullRange);

    CVPixelBufferLockBaseAddress(pixbuf, kCVPixelBufferLock_ReadOnly);

    if (nv12) {
        ensureNativeResources();
        if (!s_ntx.nv12_shader || !s_ntx.nv12_shader->isValid()) {
            CVPixelBufferUnlockBaseAddress(pixbuf, kCVPixelBufferLock_ReadOnly);
            return false;
        }

        void *y_ptr = CVPixelBufferGetBaseAddressOfPlane(pixbuf, 0);
        size_t y_stride = CVPixelBufferGetBytesPerRowOfPlane(pixbuf, 0);
        void *uv_ptr = CVPixelBufferGetBaseAddressOfPlane(pixbuf, 1);
        size_t uv_stride = CVPixelBufferGetBytesPerRowOfPlane(pixbuf, 1);

        uploadPlane(s_ntx.y_tex, GL_R8, GL_RED, w, h, y_ptr, y_stride);
        uploadPlane(s_ntx.uv_tex, GL_RG8, GL_RG, w / 2, h / 2, uv_ptr, uv_stride);

        // 渲染 NV12 → RGBA 到目标纹理
        glBindFramebuffer(GL_FRAMEBUFFER, s_ntx.fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex.id, 0);
        glViewport(0, 0, w, h);

        s_ntx.nv12_shader->use();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, s_ntx.y_tex);
        s_ntx.nv12_shader->setInt("uTexY", 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, s_ntx.uv_tex);
        s_ntx.nv12_shader->setInt("uTexUV", 1);
        drawQuad(s_ntx.quad);
        s_ntx.nv12_shader->unuse();

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    } else {
        // BGRA：软件 byte-swap → RGBA 再上传
        const uint8_t *src = static_cast<const uint8_t *>(CVPixelBufferGetBaseAddress(pixbuf));
        size_t stride = CVPixelBufferGetBytesPerRow(pixbuf);
        std::vector<uint8_t> rgba(static_cast<size_t>(w * h * 4));
        for (int row = 0; row < h; ++row) {
            const uint8_t *s = src + row * stride;
            uint8_t *d = rgba.data() + row * w * 4;
            for (int col = 0; col < w; ++col, s += 4, d += 4) {
                d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = s[3]; // BGRA → RGBA
            }
        }
        glBindTexture(GL_TEXTURE_2D, tex.id);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    CVPixelBufferUnlockBaseAddress(pixbuf, kCVPixelBufferLock_ReadOnly);
    tex.width = w;
    tex.height = h;
    return true;
#else
    (void)tex;
    (void)native_buf;
    return false;
#endif
}

void cleanupNativeTexture() {
#if defined(__APPLE__)
    if (!s_ntx.ready)
        return;
    glDeleteTextures(1, &s_ntx.y_tex);
    glDeleteTextures(1, &s_ntx.uv_tex);
    glDeleteFramebuffers(1, &s_ntx.fbo);
    s_ntx.nv12_shader.reset();
    destroyQuadMesh(s_ntx.quad);
    s_ntx = {};
#endif
}

}
} // namespace vp::gl
