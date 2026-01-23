/**
 * skia_render.cc - C++ OpenGL 着色器渲染
 * 流程：C++ GPU 渲染 → glReadPixels → 像素数据 → 前端 Canvas
 */

#include <napi.h>
#include <cstring>
#include <string>
#include <vector>
#include <filesystem>
#include <cstdlib>

#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
static CGLContextObj g_ctx = nullptr;
static CGLPixelFormatObj g_pf = nullptr;
#elif defined(_WIN32)
#include <windows.h>
#include <GL/gl.h>
#else
#include <GL/gl.h>
#include <EGL/egl.h>
#endif

// GPU 资源
static GLuint g_program = 0;
static GLuint g_vao = 0;
static GLuint g_vbo = 0;
static GLuint g_fbo = 0;
static GLuint g_tex = 0;
static GLint g_colorLoc = -1;
static int g_w = 0, g_h = 0;
static bool g_init = false;
static size_t g_pixelSize = 0;

const char *g_vs = R"(
#version 150 core
in vec2 pos;
void main() { gl_Position = vec4(pos, 0.0, 1.0); }
)";

const char *g_fs = R"(
#version 150 core
uniform vec4 u_color;
out vec4 fragColor;
void main() { fragColor = u_color; }
)";

GLuint compileShader(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok)
        return 0;
    return s;
}

bool createContext()
{
#ifdef __APPLE__
    if (g_ctx)
        return true;

    CGLPixelFormatAttribute attrs[] = {
        kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core,
        kCGLPFAColorSize, (CGLPixelFormatAttribute)24,
        kCGLPFAAlphaSize, (CGLPixelFormatAttribute)8,
        kCGLPFAAccelerated,
        (CGLPixelFormatAttribute)0};

    GLint n;
    CGLError err = CGLChoosePixelFormat(attrs, &g_pf, &n);
    if (err != kCGLNoError || n == 0)
        return false;

    err = CGLCreateContext(g_pf, nullptr, &g_ctx);
    if (err != kCGLNoError)
        return false;

    CGLSetCurrentContext(g_ctx);
    return true;
#else
    return false;
#endif
}

bool createShader()
{
    if (g_program)
        return true;

    GLuint vs = compileShader(GL_VERTEX_SHADER, g_vs);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, g_fs);
    if (!vs || !fs)
        return false;

    g_program = glCreateProgram();
    glAttachShader(g_program, vs);
    glAttachShader(g_program, fs);
    glLinkProgram(g_program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok;
    glGetProgramiv(g_program, GL_LINK_STATUS, &ok);
    if (!ok)
        return false;

    g_colorLoc = glGetUniformLocation(g_program, "u_color");

    float verts[] = {-1, -1, 1, -1, -1, 1, 1, 1};
    glGenVertexArrays(1, &g_vao);
    glGenBuffers(1, &g_vbo);
    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindVertexArray(0);

    return true;
}

bool ensureFBO(int w, int h)
{
    if (g_fbo && g_w == w && g_h == h)
        return true;

    if (g_fbo)
    {
        glDeleteFramebuffers(1, &g_fbo);
        g_fbo = 0;
    }
    if (g_tex)
    {
        glDeleteTextures(1, &g_tex);
        g_tex = 0;
    }

    g_w = w;
    g_h = h;
    g_pixelSize = (size_t)w * h * 4;

    glGenTextures(1, &g_tex);
    glBindTexture(GL_TEXTURE_2D, g_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glGenFramebuffers(1, &g_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_tex, 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    return status == GL_FRAMEBUFFER_COMPLETE;
}

// ========== N-API ==========

Napi::Value Init(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    if (g_init)
        return Napi::Boolean::New(env, true);

    if (!createContext())
    {
        Napi::Error::New(env, "Failed to create OpenGL context").ThrowAsJavaScriptException();
        return Napi::Boolean::New(env, false);
    }

    if (!createShader())
    {
        Napi::Error::New(env, "Failed to create shader").ThrowAsJavaScriptException();
        return Napi::Boolean::New(env, false);
    }

    g_init = true;
    return Napi::Boolean::New(env, true);
}

Napi::Value Render(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();

    if (!g_init)
    {
        Napi::Error::New(env, "Not initialized").ThrowAsJavaScriptException();
        return env.Null();
    }

    if (info.Length() < 3)
    {
        Napi::TypeError::New(env, "Need: width, height, {r,g,b,a}").ThrowAsJavaScriptException();
        return env.Null();
    }

    int w = info[0].As<Napi::Number>().Int32Value();
    int h = info[1].As<Napi::Number>().Int32Value();
    Napi::Object c = info[2].As<Napi::Object>();

    float r = c.Get("r").As<Napi::Number>().FloatValue();
    float g = c.Get("g").As<Napi::Number>().FloatValue();
    float b = c.Get("b").As<Napi::Number>().FloatValue();
    float a = c.Get("a").As<Napi::Number>().FloatValue();

#ifdef __APPLE__
    CGLSetCurrentContext(g_ctx);
#endif

    if (!ensureFBO(w, h))
    {
        Napi::Error::New(env, "FBO failed").ThrowAsJavaScriptException();
        return env.Null();
    }

    // GPU 渲染
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glViewport(0, 0, w, h);
    glUseProgram(g_program);
    glUniform4f(g_colorLoc, r, g, b, a);
    glBindVertexArray(g_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);

    // 读取像素
    Napi::ArrayBuffer ab = Napi::ArrayBuffer::New(env, g_pixelSize);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, ab.Data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    return Napi::Uint8Array::New(env, g_pixelSize, ab, 0);
}

Napi::Value GetInfo(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    Napi::Object result = Napi::Object::New(env);

    if (!g_init)
    {
        result.Set("ready", false);
        return result;
    }

#ifdef __APPLE__
    CGLSetCurrentContext(g_ctx);
#endif

    result.Set("ready", true);
    result.Set("version", Napi::String::New(env, (const char *)glGetString(GL_VERSION)));
    result.Set("renderer", Napi::String::New(env, (const char *)glGetString(GL_RENDERER)));

    // 获取系统下载目录并列出文件
    std::string downloadPath;
#ifdef __APPLE__
    const char *home = std::getenv("HOME");
    if (home)
    {
        downloadPath = std::string(home) + "/Downloads";
    }
#elif defined(_WIN32)
    const char *userProfile = std::getenv("USERPROFILE");
    if (userProfile)
    {
        downloadPath = std::string(userProfile) + "\\Downloads";
    }
#else
    const char *home = std::getenv("HOME");
    if (home)
    {
        downloadPath = std::string(home) + "/Downloads";
    }
#endif

    result.Set("downloadPath", Napi::String::New(env, downloadPath));

    Napi::Array files = Napi::Array::New(env);
    uint32_t idx = 0;

    try
    {
        if (std::filesystem::exists(downloadPath))
        {
            for (const auto &entry : std::filesystem::directory_iterator(downloadPath))
            {
                Napi::Object fileInfo = Napi::Object::New(env);
                fileInfo.Set("name", Napi::String::New(env, entry.path().filename().string()));
                fileInfo.Set("isDir", Napi::Boolean::New(env, entry.is_directory()));

                if (entry.is_regular_file())
                {
                    fileInfo.Set("size", Napi::Number::New(env, (double)entry.file_size()));
                }

                files.Set(idx++, fileInfo);

                // 限制最多返回 50 个文件
                if (idx >= 50)
                    break;
            }
        }
    }
    catch (...)
    {
        // 忽略权限错误等
    }

    result.Set("files", files);
    result.Set("fileCount", Napi::Number::New(env, idx));

    return result;
}

Napi::Value Cleanup(const Napi::CallbackInfo &info)
{
#ifdef __APPLE__
    if (g_ctx)
        CGLSetCurrentContext(g_ctx);
#endif

    if (g_fbo)
        glDeleteFramebuffers(1, &g_fbo);
    if (g_tex)
        glDeleteTextures(1, &g_tex);
    if (g_vao)
        glDeleteVertexArrays(1, &g_vao);
    if (g_vbo)
        glDeleteBuffers(1, &g_vbo);
    if (g_program)
        glDeleteProgram(g_program);

#ifdef __APPLE__
    if (g_ctx)
        CGLDestroyContext(g_ctx);
    if (g_pf)
        CGLDestroyPixelFormat(g_pf);
    g_ctx = nullptr;
    g_pf = nullptr;
#endif

    g_fbo = g_tex = g_vao = g_vbo = g_program = 0;
    g_init = false;
    g_w = g_h = 0;

    return info.Env().Undefined();
}

Napi::Object InitModule(Napi::Env env, Napi::Object exports)
{
    exports.Set("init", Napi::Function::New(env, Init));
    exports.Set("render", Napi::Function::New(env, Render));
    exports.Set("getInfo", Napi::Function::New(env, GetInfo));
    exports.Set("cleanup", Napi::Function::New(env, Cleanup));
    return exports;
}

NODE_API_MODULE(skia_render, InitModule)
