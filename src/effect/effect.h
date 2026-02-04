#pragma once

#include "../gl/types.h"
#include <nlohmann/json.hpp>
#include <memory>
#include <string>

namespace vp {

// 前向声明
namespace gl {
class Shader;
struct QuadMesh;
} // namespace gl

// 特效基类 - 处理器模式
// 输入 FBO，输出到另一个 FBO
class Effect {
public:
    Effect() = default;
    virtual ~Effect() = default;

    Effect(const Effect &) = delete;
    Effect &operator=(const Effect &) = delete;

    // 从 JSON 加载特效配置
    virtual bool load(const nlohmann::json &config) = 0;

    // 应用特效：输入 FBO → 返回输出 FBO
    // input: 输入 FBO（纹理）
    // time_ms: 当前时间（用于动画特效）
    // 返回: 输出 FBO（由特效内部创建/管理）
    // 
    // 注意：
    // - 特效内部自己管理 FBO（可以从 pool 获取、可以维护成员变量复用）
    // - Layer 不管理特效返回的 FBO
    // - 特效可以在析构或下次 apply 时清理 FBO
    virtual gl::FBO apply(const gl::FBO &input, int64_t time_ms = 0) = 0;

    // 获取特效类型名称
    virtual const char *getType() const = 0;

protected:
    std::string name_;
};

} // namespace vp
