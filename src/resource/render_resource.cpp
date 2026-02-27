#include "render_resource.h"
#include "../core/root_node.h"
#include "texture_player.h"
#include <algorithm>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace vp {

RenderResource::RenderResource(RootNode *root) :
    root_(root), resource_duration_ms_(1000) {
}

RenderResource::~RenderResource() {
}

bool RenderResource::load(const nlohmann::json &config, const std::string &base_path) {
    std::string folder_path = config.value("path", "");
    if (folder_path.empty()) {
        setError("path is required");
        return false;
    }

    // 如果有 base_path，拼接路径
    if (!base_path.empty()) {
        folder_path = base_path + "/" + folder_path;
    }

    return loadFromFolder(folder_path);
}

bool RenderResource::loadFromFolder(const std::string &folder_path) {
    base_path_ = folder_path;

    // 加载 config.json
    std::string config_path = base_path_ + "/config.json";
    return loadConfig(config_path);
}

bool RenderResource::loadConfig(const std::string &config_path) {
    // 读取 JSON 文件
    std::ifstream file(config_path);
    if (!file.is_open()) {
        setError("config file not found: " + config_path);
        return false;
    }

    nlohmann::json config;
    try {
        file >> config;
    } catch (...) {
        setError("load config failed: " + config_path);
        return false;
    }

    // 加载基本信息（使用 value 提供默认值）
    name_ = config.value("name", "");
    id_ = config.value("id", "");

    if (name_.empty() || id_.empty()) {
        setError("name and id are required");
        return false;
    }

    desc_ = config.value("desc", "");
    format_ = config.value("format", "effect");
    resource_duration_ms_ = config.value("suggestionDuration", 1000LL);
    // 加载各个组件
    return loadRenderPasses(config) && loadAnimations(config) && loadUniforms(config) && loadTextures(config);
}

bool RenderResource::loadRenderPasses(const nlohmann::json &config) {
    if (!config.contains("renderPass")) {
        setError("renderPass is required");
        return false;
    }

    const auto &pass_array = config["renderPass"];
    // 提前 reserve 避免动态扩容
    render_passes_.reserve(pass_array.size());

    for (size_t i = 0; i < pass_array.size(); ++i) {
        const auto &pass_config = pass_array[i];
        auto pass = std::make_unique<RenderPass>(root_, i);
        if (!pass->load(pass_config, base_path_)) {
            setError("renderPass[" + std::to_string(i) + "] load failed: " + pass->getErrorMessage());
            return false;
        }
        render_passes_.emplace_back(std::move(pass));
    }

    if (render_passes_.empty()) {
        setError("render resource get zero render passes");
        return false;
    }

    return true;
}

bool RenderResource::loadAnimations(const nlohmann::json &config) {
    if (!config.contains("animation")) {
        return true; // 没有动画也是正常的
    }

    const auto &anim_array = config["animation"];
    animations_.reserve(anim_array.size());

    for (size_t i = 0; i < anim_array.size(); ++i) {
        auto anim = std::make_unique<ResourceAnimation>();
        if (!anim->load(anim_array[i])) {
            setError("animation[" + std::to_string(i) + "] load failed: " + anim->getErrorMessage());
            return false;
        }
        animations_.emplace_back(std::move(anim));
    }

    return true;
}

bool RenderResource::loadUniforms(const nlohmann::json &config) {
    if (!config.contains("uniform")) {
        return true; // 没有 uniform 也是正常的
    }

    const auto &uniform_array = config["uniform"];
    uniforms_.reserve(uniform_array.size());

    for (size_t i = 0; i < uniform_array.size(); ++i) {
        const auto &uniform_config = uniform_array[i];
        auto uniform = std::make_unique<UniformParam>();
        if (!uniform->load(uniform_config)) {
            setError("uniform[" + std::to_string(i) + "] load failed: " + uniform->getErrorMessage());
            return false;
        }
        updateUniformToShaders(uniform.get());
        uniforms_.emplace_back(std::move(uniform));
    }

    return true;
}

bool RenderResource::loadTextures(const nlohmann::json &config) {
    if (!config.contains("texture")) {
        return true; // 没有纹理也是正常的
    }

    const auto &texture_array = config["texture"];
    for (size_t i = 0; i < texture_array.size(); ++i) {
        const auto &tex_config = texture_array[i];
        // 检测类型（通过文件扩展名）
        std::string url = tex_config.value("url", "");
        std::string ext = fs::path(url).extension().string();

        // 转小写
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        // 判断是否为视频格式并创建对应的播放器
        bool is_video = (ext == ".mp4" || ext == ".webm" || ext == ".mov" || ext == ".avi");

        std::unique_ptr<TexturePlayer> player = is_video ? static_cast<std::unique_ptr<TexturePlayer>>(std::make_unique<VideoTexture>(root_)) : static_cast<std::unique_ptr<TexturePlayer>>(std::make_unique<ImageTexture>(root_));

        if (!player->load(tex_config, base_path_)) {
            setError("texture[" + std::to_string(i) + "] load failed: " + player->getErrorMessage());
            return false;
        }
        textures_.emplace_back(std::move(player));
    }

    return true;
}

gl::FBO RenderResource::render(const std::vector<gl::FBO> &inputs, TimeMs time_ms) {
    if (render_passes_.empty() || !root_ || inputs.empty()) {
        setError("render resource is not valid");
        return gl::FBO{};
    }

    // 获取所有动画的值，构造uniform_param对象
    std::vector<std::unique_ptr<UniformParam>> uniforms;
    uniforms.reserve(animations_.size() + 2);
    for (auto &anim : animations_) {
        uniforms.emplace_back(anim->convertToUniformParam(time_ms));
    }
    uniforms.emplace_back(std::make_unique<UniformParam>("time", UniformType::Float, std::vector<float>{static_cast<float>(time_ms)}, "uTime", true));
    uniforms.emplace_back(std::make_unique<UniformParam>("progress", UniformType::Float, std::vector<float>{static_cast<float>(time_ms / resource_duration_ms_)}, "uProgress", true));

    // 播放外部纹理
    for (auto &texture : textures_) {
        texture->play(time_ms);
    }
    std::vector<gl::FBO> outputs;
    outputs.reserve(render_passes_.size());
    for (size_t i = 0; i < render_passes_.size(); ++i) {
        // 纹理输入
        std::vector<TextureInput> tex_inputs;
        // 当前 pass 之前的 pass 的输出作为纹理输入
        for (size_t j = 0; j < i; j++) {
            const auto *input_tex_def = render_passes_[j]->getAsInputTexDefFor(i);
            if (input_tex_def) {
                tex_inputs.emplace_back(TextureInput{input_tex_def->name, input_tex_def->pipe, outputs[j].texture});
                continue;
            }

            // 如果 outputs[j] 对后续所有 pass 都无影响，提前释放
            bool needed_by_later = false;
            for (size_t k = i + 1; k < render_passes_.size(); k++) {
                if (render_passes_[k]->getAsInputTexDefFor(j)) {
                    needed_by_later = true;
                    break;
                }
            }
            if (!needed_by_later && outputs[j].isValid()) {
                root_->getFBOPool()->release(outputs[j]);
            }
        }
        // 外部纹理输入
        for (auto &texture : textures_) {
            int pipe = texture->getPipeForPass(i);
            if (pipe < 0) {
                continue;
            }
            tex_inputs.emplace_back(TextureInput{texture->getName(), pipe, texture->getTextureId()});
        }
        outputs.emplace_back(render_passes_[i]->execute(inputs, uniforms, tex_inputs));
    }
    // 释放除最后一个之外的 FBO
    for (size_t i = 0; i < outputs.size() - 1; ++i) {
        if (!outputs[i].isValid()) {
            continue;
        }
        root_->getFBOPool()->release(outputs[i]);
    }
    return outputs.back();
}

const std::string &RenderResource::getName() const {
    return name_;
}

const std::string &RenderResource::getId() const {
    return id_;
}

const std::string &RenderResource::getFormat() const {
    return format_;
}

void RenderResource::updateUniformToShaders(UniformParam *uniform) {
    // 遍历所有 pass，复用 applyToShader 更新受影响的 shader
    for (size_t pass_idx = 0; pass_idx < render_passes_.size(); ++pass_idx) {
        if (!uniform->affectsPass(pass_idx)) {
            continue;
        }
        uniform->applyToShader(render_passes_[pass_idx]->getShader());
    }
}

bool RenderResource::setFloatParam(const std::string &name, float value) {
    for (auto &uniform : uniforms_) {
        if (uniform->getName() != name || uniform->getType() != UniformType::Float) {
            continue;
        }

        uniform->setValue(value);
        updateUniformToShaders(uniform.get());
        return true;
    }
    setError("set float param failed: " + name);
    return false;
}

bool RenderResource::setVecParam(const std::string &name, const std::vector<float> &value) {
    for (auto &uniform : uniforms_) {
        if (uniform->getName() != name) {
            continue;
        }

        auto type = uniform->getType();
        if (type != UniformType::Vec2 && type != UniformType::Vec3 && type != UniformType::Vec4) {
            continue;
        }

        uniform->setValue(value);
        updateUniformToShaders(uniform.get());
        return true;
    }
    setError("set vec param failed: " + name);
    return false;
}

bool RenderResource::setBoolParam(const std::string &name, bool value) {
    for (auto &uniform : uniforms_) {
        if (uniform->getName() != name || uniform->getType() != UniformType::Boolean) {
            continue;
        }

        uniform->setValue(value);
        updateUniformToShaders(uniform.get());
        return true;
    }
    setError("set bool param failed: " + name);
    return false;
}

TimeMs RenderResource::getResourceDuration() const {
    return resource_duration_ms_;
}

void RenderResource::setResourceDuration(TimeMs duration_ms) {
    resource_duration_ms_ = duration_ms;
}

} // namespace vp
