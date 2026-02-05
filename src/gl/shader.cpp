#include "shader.h"
#include <fstream>
#include <iostream>
#include <sstream>

namespace vp {
namespace gl {

Shader::Shader(const char *vertex_source, const char *fragment_source) {
    // 编译顶点着色器
    GLuint vertex = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex, 1, &vertex_source, nullptr);
    glCompileShader(vertex);
    checkCompileErrors(vertex, "VERTEX");

    // 编译片段着色器
    GLuint fragment = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment, 1, &fragment_source, nullptr);
    glCompileShader(fragment);
    checkCompileErrors(fragment, "FRAGMENT");

    // 链接程序
    ID = glCreateProgram();
    glAttachShader(ID, vertex);
    glAttachShader(ID, fragment);
    glLinkProgram(ID);
    checkCompileErrors(ID, "PROGRAM");

    // 删除着色器
    glDeleteShader(vertex);
    glDeleteShader(fragment);
}

Shader Shader::fromFiles(const char *vertex_path, const char *fragment_path) {
    std::string vertex_code;
    std::string fragment_code;
    std::ifstream v_file;
    std::ifstream f_file;

    v_file.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    f_file.exceptions(std::ifstream::failbit | std::ifstream::badbit);

    try {
        v_file.open(vertex_path);
        f_file.open(fragment_path);
        std::stringstream v_stream, f_stream;

        v_stream << v_file.rdbuf();
        f_stream << f_file.rdbuf();

        v_file.close();
        f_file.close();

        vertex_code = v_stream.str();
        fragment_code = f_stream.str();
    } catch (std::ifstream::failure &e) {
        std::cerr << "ERROR::SHADER::FILE_NOT_SUCCESSFULLY_READ: " << e.what() << std::endl;
        return Shader("", ""); // 返回无效 shader
    }

    return Shader(vertex_code.c_str(), fragment_code.c_str());
}

Shader::~Shader() {
    if (ID)
        glDeleteProgram(ID);
}

Shader::Shader(Shader &&other) noexcept :
    ID(other.ID) {
    other.ID = 0;
}

Shader &Shader::operator=(Shader &&other) noexcept {
    if (this != &other) {
        if (ID)
            glDeleteProgram(ID);
        ID = other.ID;
        other.ID = 0;
    }
    return *this;
}

void Shader::use() const {
    glUseProgram(ID);
}

void Shader::unuse() const {
    glUseProgram(0);
}

bool Shader::isValid() const {
    return ID != 0;
}

void Shader::setInt(const std::string &name, int value) const {
    glUniform1i(glGetUniformLocation(ID, name.c_str()), value);
}

void Shader::setFloat(const std::string &name, float value) const {
    glUniform1f(glGetUniformLocation(ID, name.c_str()), value);
}

void Shader::setVec2(const std::string &name, float x, float y) const {
    glUniform2f(glGetUniformLocation(ID, name.c_str()), x, y);
}

void Shader::setVec3(const std::string &name, float x, float y, float z) const {
    glUniform3f(glGetUniformLocation(ID, name.c_str()), x, y, z);
}

void Shader::setVec4(const std::string &name, float x, float y, float z, float w) const {
    glUniform4f(glGetUniformLocation(ID, name.c_str()), x, y, z, w);
}

GLint Shader::getUniformLocation(const std::string &name) const {
    return glGetUniformLocation(ID, name.c_str());
}

void Shader::checkCompileErrors(GLuint shader, const std::string &type) {
    GLint success;
    GLchar info_log[1024];

    if (type != "PROGRAM") {
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(shader, 1024, nullptr, info_log);
            last_error_ += "ERROR::SHADER_COMPILATION_ERROR [" + type + "]\n" + std::string(info_log) + "\n";
        }
    } else {
        glGetProgramiv(shader, GL_LINK_STATUS, &success);
        if (!success) {
            glGetProgramInfoLog(shader, 1024, nullptr, info_log);
            last_error_ += "ERROR::PROGRAM_LINKING_ERROR\n" + std::string(info_log) + "\n";
        }
    }
}

}
} // namespace vp::gl
