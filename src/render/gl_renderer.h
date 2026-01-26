#pragma once

#include <cstdint>
#include <string>

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#include <OpenGL/OpenGL.h>
#else
#include <GL/gl.h>
#include <GL/glext.h>
#endif

namespace vp
{

    // OpenGL 渲染器 - 负责将 RGBA 数据渲染到 FBO 并读取像素
    class GLRenderer
    {
    public:
        GLRenderer();
        ~GLRenderer();

        GLRenderer(const GLRenderer &) = delete;
        GLRenderer &operator=(const GLRenderer &) = delete;

        bool init();
        void cleanup();

        bool setSize(int width, int height);

        // 上传 RGBA 数据到纹理
        void uploadTexture(const uint8_t *data, int width, int height);

        // 渲染纹理到 FBO
        void render();

        // 读取 FBO 像素
        void readPixels(uint8_t *out, int size);

        int getWidth() const { return width_; }
        int getHeight() const { return height_; }
        bool isInitialized() const { return initialized_; }

        std::string getGPUInfo() const;

    private:
        bool createShaders();
        bool createFBO();
        void destroyFBO();

#ifdef __APPLE__
        CGLContextObj cgl_ctx_ = nullptr;
        CGLPixelFormatObj cgl_pf_ = nullptr;
#endif

        GLuint program_ = 0;
        GLuint flip_program_ = 0; // 翻转着色器
        GLuint vao_ = 0;
        GLuint vbo_ = 0;
        GLuint texture_ = 0;
        GLuint fbo_ = 0;
        GLuint fbo_tex_ = 0;
        GLuint flip_fbo_ = 0; // 翻转输出 FBO
        GLuint flip_fbo_tex_ = 0;
        GLint tex_loc_ = -1;
        GLint flip_tex_loc_ = -1;

        int width_ = 0;
        int height_ = 0;
        bool initialized_ = false;
    };

} // namespace vp
