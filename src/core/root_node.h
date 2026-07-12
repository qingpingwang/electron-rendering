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
    // 返回值：0=命中缓存，1=实际渲染成功，-1=参数错误，-2=渲染失败（buffer 内容无效，
    // 原因见 getErrorMessage()）。
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

    // 通过素材 ID 设置 RenderResource 外部参数（特效/转场均可）
    bool setMaterialFloatParam(const std::string &materialId, const std::string &name, float value);
    bool setMaterialVecParam(const std::string &materialId, const std::string &name, const std::vector<float> &value);
    bool setMaterialBoolParam(const std::string &materialId, const std::string &name, bool value);

private:
    // 渲染一帧的结果：kCancelled 是正常控制流（被主动打断），不是错误；
    // kFailed 才是真的渲染失败，此时 getErrorMessage() 里有原因。
    enum class RenderStatus { kOk, kFailed, kCancelled };
    RenderStatus renderFrame(TimeMs time_ms, uint8_t *out_buffer);

    // 异步准备：后台线程入口，渲染到 cache_data_，结果记录到 last_prepare_
    void renderIntoCache(TimeMs time_ms);
    void startPrepareNextFrame(TimeMs next_time_ms);
    void cancelPrepare();     // 请求取消并等待后台线程结束
    void joinPrepareThread(); // 只等待，不请求取消
    void preemptPrepare();    // 取消旧的准备线程 + 清掉遗留错误，为新一轮渲染让路

    // 缓存查询结果：kHit 时数据已拷进 out_buffer；kFailed 时该时间点此前真的
    // 渲染失败过（非取消），错误信息见 getErrorMessage()，不必再同步渲染一遍。
    enum class CacheLookup { kHit, kMiss, kFailed };
    CacheLookup lookupCache(TimeMs time_ms, uint8_t *out_buffer, size_t size);
    CacheLookup classifyLocked(TimeMs time_ms) const; // 调用者必须已持有 cache_mutex_
    void invalidateCache();                           // 清空 last_prepare_（内部自行加锁）
    bool isWithinHalfFrame(TimeMs a, TimeMs b) const;
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

    // 帧缓存：cache_data_ 是像素数据，last_prepare_ 是最近一次准备的结果
    std::vector<uint8_t> cache_data_;
    struct PrepareResult {
        TimeMs time_ms = kInvalidTime;
        bool ok = false;
    };
    PrepareResult last_prepare_;
    mutable std::mutex cache_mutex_;

    // 异步线程
    std::thread prepare_thread_;
    std::atomic<bool> cancel_flag_{false};
};

} // namespace vp
