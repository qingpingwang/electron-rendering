#pragma once

#include "types.h"
#include <cstdint>
#include <string>

namespace vp {
namespace gl {

// ========== 上下文管理 ==========

// 初始化 OpenGL 上下文
bool initContext(GLContext &ctx);

// 销毁上下文
void destroyContext(GLContext &ctx);

// 设置当前上下文
void makeCurrent(const GLContext &ctx);

// 获取 GPU 信息
std::string getGPUInfo(const GLContext &ctx);

// ========== 纹理管理 ==========

// 创建纹理（可指定格式）
Texture createTexture(int width, int height, GLenum internal_format = GL_RGBA8, GLenum format = GL_RGBA,
                      GLenum type = GL_UNSIGNED_BYTE);

// 从图片文件创建纹理（内部调用 createTexture + updateTexture）
Texture createTextureFromFile(const char *path, bool flip_y = true);

// 更新纹理数据（使用纹理自身的格式）
bool updateTexture(Texture &tex, const uint8_t *data, int width, int height);

// 绑定纹理到指定纹理单元
bool bindTexture(const Texture &tex, int unit = 0, GLenum target = GL_TEXTURE_2D);

// 解绑纹理
bool unbindTexture(int unit = 0, GLenum target = GL_TEXTURE_2D);

// 销毁纹理
void destroyTexture(Texture &tex);

// ========== FBO 管理 ==========

// 创建 FBO（可指定格式）
FBO createFBO(int width, int height, GLenum internal_format = GL_RGBA8, GLenum format = GL_RGBA,
              GLenum type = GL_UNSIGNED_BYTE);

// 绑定 FBO
bool bindFBO(const FBO &fbo);

// 解绑 FBO
bool unbindFBO();

// 销毁 FBO
void destroyFBO(FBO &fbo);

// ========== 网格管理 ==========

// 创建全屏四边形
QuadMesh createQuadMesh();

// 销毁网格
void destroyQuadMesh(QuadMesh &mesh);

// ========== 渲染操作 ==========

// 清屏并设置背景色
void cleanColor(float r = 0.0f, float g = 0.0f, float b = 0.0f, float a = 1.0f);

// 绘制全屏四边形（已绑定 FBO 和纹理的情况下）
bool drawQuad(const QuadMesh &mesh);

// 从 FBO 读取像素
bool readPixels(const FBO &fbo, uint8_t *out_buffer, int buffer_size);

}
} // namespace vp::gl
