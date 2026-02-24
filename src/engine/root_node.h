#pragma once

#include "../material/material.h"
#include "../gl/functions.h"
#include "../gl/fbo_pool.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class GrDirectContext;

namespace gl { class Shader; }

namespace vp {

class Layer;

// Canvas 配置结构体
struct CanvasConfig {
    int width = 0;
    int height = 0;
    std::string ratio;
};

class RootNode {
public:
    RootNode();
    ~RootNode();

    RootNode(const RootNode &) = delete;
    RootNode &operator=(const RootNode &) = delete;

    bool init();
    void cleanup();

    std::string loadFromJson(const std::string &json_str);
    void unload();

    void setCurrentTime(int64_t time_ms);
    bool draw(uint8_t *buffer, size_t buffer_size);

    int getWidth() const;
    int getHeight() const;
    int64_t getDurationMs() const;
    double getFrameRate() const;
    const std::string &getId() const;
    const CanvasConfig &getCanvas() const;
    bool isLoaded() const;
    std::string getGPUInfo() const;

    // 获取共享渲染资源
    gl::Shader *getShader() const;
    const gl::QuadMesh *getQuad() const;
    int64_t getCurrentTime() const;

    // Skia 上下文（文字渲染用，共享 CGL 上下文）
    GrDirectContext *getSkiaContext() const;

    // 获取 FBO 缓存池
    gl::FBOPool *getFBOPool();

    // 获取渲染目标 FBO
    const gl::FBO &getRenderFBO() const;

    // 获取素材指针（按类型和 ID 查找）
    Material *getMaterial(MaterialType type, const std::string &material_id) const;

    // 按类型获取素材列表（直接数组访问，零开销）
    const std::vector<std::unique_ptr<Material>> &getMaterialsByType(MaterialType type) const;

private:
    // 渲染一帧
    bool renderFrame(int64_t time_ms, uint8_t *out_buffer);
    // 异步准备
    void startPrepareNextFrame(int64_t next_time_ms);
    void cancelPrepare();
    // 缓存
    bool isCacheHit(int64_t time_ms) const;
    int64_t getHalfFrameMs() const;

    // OpenGL 资源
    gl::GLContext gl_ctx_;
    gl::FBOPool fbo_pool_; // FBO 缓存池
    gl::FBO render_fbo_;   // 主渲染 FBO（从池中获取，不释放）
    std::unique_ptr<gl::Shader> shader_;
    gl::QuadMesh quad_;

    // Skia（文字渲染，共享 CGL 上下文）
    GrDirectContext *skia_context_ = nullptr;

    // 图层
    std::vector<std::unique_ptr<Layer>> layers_;

    // 素材管理（固定数组，按类型索引）
    std::vector<std::unique_ptr<Material>> materials_[MATERIAL_TYPE_COUNT];

    // 项目配置
    std::string id_;
    CanvasConfig canvas_;
    int64_t duration_ms_ = 0;
    double frame_rate_ = 0.0;
    int64_t current_time_ms_ = 0;

    // 帧缓存
    std::vector<uint8_t> cache_data_;
    int64_t cache_time_ms_ = -1;
    mutable std::mutex cache_mutex_;

    // 异步线程
    std::thread prepare_thread_;
    std::atomic<bool> preparing_{false};
    std::atomic<bool> cancel_flag_{false};
};

} // namespace vp
