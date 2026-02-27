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

    // 绘制图层到目标 FBO
    // 流程：检查特效 → 选择中间/直出 → renderContent() → 应用特效 → blit 到 target
    bool draw(const gl::FBO &target, TimeMs time_ms);

    const std::string &getName() const;
    TimeMs getDurationMs() const;

    TimeMs getStartTime() const;
    TimeMs getEndTime() const;
    bool isActive(TimeMs time_ms) const;

    // 预备（子类实现：如视频解码到起始帧）
    virtual void prepare() = 0;

    virtual MaterialType getMaterialType() const = 0;
    Material *getMaterial() const;
    bool hasTransition() const;

    // 转场
    Effect *getActiveTransition(TimeMs time_ms) const;

protected:
    // 渲染内容（子类实现具体绘制逻辑）
    // fbo: 当前要绘制到的目标 FBO
    virtual bool renderContent(const gl::FBO &fbo, TimeMs time_ms) = 0;

    // 应用特效链（基类实现）
    // 输入 FBO，返回特效处理后的 FBO
    // 注意：返回的 FBO 由特效内部管理，Layer 不负责释放
    gl::FBO applyEffects(const gl::FBO &input, TimeMs time_ms);

    bool hasActiveEffects(TimeMs time_ms) const;

    RootNode *root_ = nullptr;
    Material *material_ = nullptr;
    std::string name_;
    TimeMs duration_ms_ = 0;
    TimeMs start_time_ms_ = 0;
    TimeMs end_time_ms_ = 0;

    // 特效 & 转场
    std::vector<std::unique_ptr<Effect>> effects_;
    std::vector<std::unique_ptr<Effect>> transitions_;
};

} // namespace vp
