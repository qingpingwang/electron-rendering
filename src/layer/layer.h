#pragma once

#include "../core/loadable.h"
#include "../core/types.h"
#include "effect.h"
#include "../gl/types.h"
#include "material.h"
#include <memory>
#include <string>
#include <vector>

namespace vp {

class RootNode;

// 图层基类
class Layer : public Loadable {
public:
    Layer(RootNode *root);
    virtual ~Layer() = default;

    Layer(const Layer &) = delete;
    Layer &operator=(const Layer &) = delete;

    // 从 JSON 加载图层基础配置（解析通用属性）
    bool load(const nlohmann::json &config, const std::string &base_path = "") override;

    // 绘制图层到 render_fbo_（基类实现，模板方法）
    // 流程：检查特效 → 选择目标 FBO → renderContent() → 应用特效 → blit 到 render_fbo_
    bool draw();

    const std::string &getName() const;
    TimeMs getDurationMs() const;

    TimeMs getStartTime() const;
    TimeMs getEndTime() const;
    bool isActive() const; // 判断当前时间是否在图层的时间范围内

    virtual MaterialType getMaterialType() const = 0;
    Material *getMaterial() const { return material_; }

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
    Material *material_ = nullptr;
    std::string name_;
    TimeMs duration_ms_ = 0;
    TimeMs start_time_ms_ = 0;
    TimeMs end_time_ms_ = 0;

    // 特效支持
    std::vector<std::unique_ptr<Effect>> effects_; // 特效链
};

} // namespace vp
