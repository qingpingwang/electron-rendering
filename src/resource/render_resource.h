#pragma once

#include "../gl/types.h"
#include "animation.h"
#include "render_pass.h"
#include "texture_player.h"
#include "uniform_param.h"
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <vector>

namespace vp {

// 前向声明
class RootNode;

// 用于加载和执行多 pass 渲染效果
class RenderResource {
public:
    RenderResource(RootNode *root);
    ~RenderResource();

    // 从文件夹加载资源
    bool loadFromFolder(const std::string &folder_path);

    // 渲染（输入 FBO → 输出 FBO）
    // 支持单输入（effect）和多输入（transition）
    gl::FBO render(const std::vector<gl::FBO> &inputs, int64_t time_ms);

    // 获取资源信息
    const std::string &getName() const;
    const std::string &getId() const;
    const std::string &getFormat() const; // "transition", "effect", "filter"

    // 更新外部参数
    bool setFloatParam(const std::string &name, float value);
    bool setVecParam(const std::string &name, const std::vector<float> &value);
    bool setBoolParam(const std::string &name, bool value);

    int64_t getResourceDuration() const;

    void setResourceDuration(int64_t duration_ms);

private:
    RootNode *root_;

    // 资源信息
    std::string name_;
    std::string id_;
    std::string desc_;
    std::string format_;
    int64_t resource_duration_ms_;
    std::string base_path_; // 资源文件夹路径

    // 渲染组件
    std::vector<std::unique_ptr<RenderPass>> render_passes_;
    std::vector<std::unique_ptr<ResourceAnimation>> animations_;
    std::vector<std::unique_ptr<UniformParam>> uniforms_;
    std::vector<std::unique_ptr<TexturePlayer>> textures_;

    // 加载配置文件
    bool loadConfig(const std::string &config_path);

    // 加载各个组件
    bool loadRenderPasses(const nlohmann::json &config);
    bool loadAnimations(const nlohmann::json &config);
    bool loadUniforms(const nlohmann::json &config);
    bool loadTextures(const nlohmann::json &config);

    // 更新 uniform 到受影响的 shader（复用 applyToShader）
    void updateUniformToShaders(UniformParam *uniform);
};

} // namespace vp
