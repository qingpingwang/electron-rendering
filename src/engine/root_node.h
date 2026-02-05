#pragma once

#include "../layer/layer.h"
#include "../layer/video_layer.h"
#include "../material/material.h"
#include "../gl/functions.h"
#include "../gl/shader.h"
#include "../gl/fbo_pool.h"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace vp {

// 素材类型索引（固定数组下标）
enum MaterialType {
    MATERIAL_TYPE_VIDEO = 0,
    MATERIAL_TYPE_EFFECT = 1,
    MATERIAL_TYPE_COUNT = 2 // 总数
};

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

    bool loadFromJson(const std::string &json_str);
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
