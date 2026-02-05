#pragma once

#include "../gl/types.h"
#include <nlohmann/json.hpp>
#include <memory>
#include <string>

namespace vp {

// 前向声明
class RootNode;
class RenderResource;

namespace gl {
class Shader;
struct QuadMesh;
} // namespace gl

// 特效基类 - 处理器模式
// 输入 FBO，输出到另一个 FBO
class Effect {
public:
    Effect(RootNode *root);
    virtual ~Effect();

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

    // 获取内部 RenderResource（如果有）
    // 返回 nullptr 表示该特效不使用 RenderResource
    virtual RenderResource *getRenderResource();
    virtual const RenderResource *getRenderResource() const;

    bool isActive(int64_t time_ms);

protected:
    RootNode *root_; // 访问 FBOPool、Shader、Quad 等共享资源
    std::string name_;
};

// 基于 RenderResource 的通用特效实现
// 直接从文件夹加载 RenderResource 配置
class ResourceEffect : public Effect {
public:
    ResourceEffect(RootNode *root);
    ~ResourceEffect() override;

    // 从 JSON 加载（支持 "resourcePath" 字段）
    bool load(const nlohmann::json &config) override;

    // 从文件夹路径直接加载
    bool loadFromFolder(const std::string &folder_path);

    // 应用特效
    gl::FBO apply(const gl::FBO &input, int64_t time_ms = 0) override;

    const char *getType() const override;

    // 获取内部 RenderResource 指针（用于参数控制）
    RenderResource *getRenderResource() override;
    const RenderResource *getRenderResource() const override;

private:
    std::unique_ptr<RenderResource> resource_;
};

} // namespace vp
