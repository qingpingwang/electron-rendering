#pragma once

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

namespace vp {
namespace gl {

// OpenGL 上下文（统一使用 ANGLE EGL，macOS Metal 后端 / Linux Mesa）
struct GLContext {
    EGLDisplay egl_display = EGL_NO_DISPLAY;
    EGLSurface egl_surface = EGL_NO_SURFACE;
    EGLContext egl_context = EGL_NO_CONTEXT;
    bool initialized = false;
};

// 纹理
struct Texture {
    GLuint id = 0;
    int width = 0;
    int height = 0;
    GLenum internal_format = GL_RGBA8; // 内部格式
    GLenum format = GL_RGBA;           // 像素格式
    GLenum type = GL_UNSIGNED_BYTE;    // 数据类型

    bool isValid() const;
};

// 帧缓冲对象
struct FBO {
    GLuint fbo = 0;
    GLuint texture = 0;
    int width = 0;
    int height = 0;
    GLenum internal_format = GL_RGBA8; // 纹理内部格式
    GLenum format = GL_RGBA;           // 像素格式
    GLenum type = GL_UNSIGNED_BYTE;    // 数据类型

    bool isValid() const;
};

// VAO/VBO
struct QuadMesh {
    GLuint vao = 0;
    GLuint vbo = 0;

    bool isValid() const;
};

}
} // namespace vp::gl
