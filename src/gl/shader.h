#pragma once

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif

#include <string>

namespace vp {
namespace gl {

// Shader 类 - 参考 LearnOpenGL Shader 封装
class Shader {
public:
    GLuint ID = 0;

    // 从源码字符串构造
    Shader(const char *vertex_source, const char *fragment_source);

    // 从文件路径构造
    static Shader fromFiles(const char *vertex_path, const char *fragment_path);

    ~Shader();

    // 禁止拷贝
    Shader(const Shader &) = delete;
    Shader &operator=(const Shader &) = delete;

    // 移动语义
    Shader(Shader &&other) noexcept;
    Shader &operator=(Shader &&other) noexcept;

    // 激活着色器
    void use() const;
    void unuse() const;

    bool isValid() const;

    // Uniform 设置
    void setInt(const std::string &name, int value) const;
    void setFloat(const std::string &name, float value) const;
    void setVec2(const std::string &name, float x, float y) const;
    void setVec3(const std::string &name, float x, float y, float z) const;
    void setVec4(const std::string &name, float x, float y, float z, float w) const;

    // 获取 uniform 位置
    GLint getUniformLocation(const std::string &name) const;

    // 获取错误信息
    std::string getError() const { return last_error_; }

private:
    std::string last_error_;
    void checkCompileErrors(GLuint shader, const std::string &type);
};

}
} // namespace vp::gl
