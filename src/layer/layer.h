#pragma once

#include "../effect/effect.h"
#include "../gl/types.h"
#include <nlohmann/json.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace vp {

class RootNode;
class Material;

// 图层基类
class Layer {
public:
    Layer(RootNode *root);
    virtual ~Layer() = default;

    Layer(const Layer &) = delete;
    Layer &operator=(const Layer &) = delete;

    // 从 JSON 加载图层基础配置（解析通用属性）
    virtual bool load(const nlohmann::json &segment_json);

    // 绘制图层到 render_fbo_（基类实现，模板方法）
    // 流程：检查特效 → 选择目标 FBO → renderContent() → 应用特效 → blit 到 render_fbo_
    bool draw();

    const std::string &getName() const;
    int64_t getDurationMs() const;

    int64_t getStartTime() const;
    int64_t getEndTime() const;
    bool isActive() const; // 判断当前时间是否在图层的时间范围内

protected:
    // 渲染内容（子类实现具体绘制逻辑）
    // fbo: 当前要绘制到的目标 FBO
    virtual bool renderContent(const gl::FBO &fbo) = 0;

    // 应用特效链（基类实现）
    // 输入 FBO，返回特效处理后的 FBO
    // 注意：返回的 FBO 由特效内部管理，Layer 不负责释放
    gl::FBO applyEffects(const gl::FBO &input);

    // 检查是否有活跃的特效
    bool hasActiveEffects() const;

    RootNode *root_ = nullptr;
    Material *material_ = nullptr;             // 图层使用的素材
    std::vector<Material *> effect_materials_; // 图层使用的特效素材
    std::string name_;
    int64_t duration_ms_ = 0;
    int64_t start_time_ms_ = 0;
    int64_t end_time_ms_ = 0;

    // 特效支持
    std::vector<std::unique_ptr<Effect>> effects_; // 特效链
};

} // namespace vp
