#pragma once

#include "../core/types.h"
#include "../layer/material/material.h"
#include "../gl/functions.h"
#include "../gl/fbo_pool.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class GrDirectContext;

namespace gl {
class Shader;
}

namespace vp {

class Effect;
class GroupLayer;
class Layer;

// Canvas 配置结构体
struct CanvasConfig {
    int width = 0;
    int height = 0;
    std::string ratio;
};

class RootNode : public Loadable {
public:
    RootNode();
    ~RootNode();

    RootNode(const RootNode &) = delete;
    RootNode &operator=(const RootNode &) = delete;

    bool init();
    void cleanup();

    bool load(const nlohmann::json &config, const std::string &base_path = "") override;
    nlohmann::json dump() const override;
    void unload();

    void setCurrentTime(TimeMs time_ms);
    bool isSameFrame(TimeMs time_ms) const;
    int draw(uint8_t *buffer, size_t buffer_size, bool force = false, bool prepare_next = true);

    int getWidth() const;
    int getHeight() const;
    TimeMs getDurationMs() const;
    double getFrameRate() const;
    const std::string &getId() const;
    const CanvasConfig &getCanvas() const;
    bool isLoaded() const;
    std::string getGPUInfo() const;

    // 获取共享渲染资源
    gl::Shader *getShader() const;
    const gl::QuadMesh *getQuad() const;
    TimeMs getCurrentTime() const;

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

    // 获取轨道组列表
    const std::vector<std::unique_ptr<GroupLayer>> &getGroups() const;

    // 按协议 segment 的 id 在所有轨道中查找图层；未找到返回 nullptr
    Layer *findLayerById(const std::string &layer_id) const;

    // 获取所有含音频的图层信息（layerId → {path, volume, layerType, sourceRange, targetRange}）
    nlohmann::json getAudioInfos() const;

private:
    // 渲染一帧
    bool renderFrame(TimeMs time_ms, uint8_t *out_buffer);
    // 异步准备
    void startPrepareNextFrame(TimeMs next_time_ms);
    void cancelPrepare();
    // 缓存
    bool isCacheHit(TimeMs time_ms) const;
    TimeMs getHalfFrameMs() const;

    // OpenGL 资源
    gl::GLContext gl_ctx_;
    gl::FBOPool fbo_pool_; // FBO 缓存池
    gl::FBO render_fbo_;   // 主渲染 FBO（从池中获取，不释放）
    std::unique_ptr<gl::Shader> shader_;
    gl::QuadMesh quad_;

    // Skia（文字渲染，共享 CGL 上下文）
    GrDirectContext *skia_context_ = nullptr;

    // 轨道组（每组包含若干图层）
    std::vector<std::unique_ptr<GroupLayer>> groups_;

    // 素材管理（固定数组，按类型索引）
    std::vector<std::unique_ptr<Material>> materials_[MATERIAL_TYPE_COUNT];

    // 项目配置
    std::string id_;
    CanvasConfig canvas_;
    TimeMs duration_ms_ = 0;
    double frame_rate_ = 0.0;
    TimeMs current_time_ms_ = 0;

    // 帧缓存
    std::vector<uint8_t> cache_data_;
    TimeMs cache_time_ms_ = kInvalidTime;
    mutable std::mutex cache_mutex_;

    // 异步线程
    std::thread prepare_thread_;
    std::atomic<bool> preparing_{false};
    std::atomic<bool> cancel_flag_{false};
};

} // namespace vp
