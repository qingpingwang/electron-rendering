#include "layer.h"
#include "../engine/root_node.h"
#include "../effect/effect.h"
#include "../resource/render_resource.h"

using json = nlohmann::json;

namespace vp {

Layer::Layer(RootNode *root) :
    root_(root) {
}

bool Layer::load(const json &config, const std::string &base_path) {
    clearError();
    if (!root_) {
        setError("root node is null");
        return false;
    }

    // 解析基础属性
    name_ = config.value("id", "layer");

    // 解析时间范围
    if (!config.contains("target_timerange")) {
        setError("target_timerange is required");
        return false;
    }
    start_time_ms_ = config["target_timerange"].value("start", 0);
    int64_t duration = config["target_timerange"].value("duration", 0);
    end_time_ms_ = start_time_ms_ + duration;

    // 获取素材指针
    std::string material_id = config.value("material_id", "");
    if (material_id.empty()) {
        setError("material_id is required");
        return false;
    }
    material_ = root_->getMaterial(MATERIAL_TYPE_VIDEO, material_id);

    std::vector<std::string> effect_ids = config.value("extra_material_refs", std::vector<std::string>{});
    effect_materials_.reserve(effect_ids.size());
    for (const auto &effect_id : effect_ids) {
        effect_materials_.emplace_back(root_->getMaterial(MATERIAL_TYPE_EFFECT, effect_id));
    }

    // 加载特效素材
    effects_.reserve(effect_materials_.size());
    for (const auto &effect_material : effect_materials_) {
        std::unique_ptr<ResourceEffect> resource_effect = std::make_unique<ResourceEffect>(root_);
        std::string resource_path = static_cast<EffectMaterial *>(effect_material)->getResourcePath();
        if (!resource_effect->loadFromFolder(resource_path)) {
            setError("load effect[" + resource_path + "] failed: " + resource_effect->getRenderResource()->getErrorMessage());
            return false;
        }
        effects_.emplace_back(std::move(resource_effect));
    }
    return true;
}

const std::string &Layer::getName() const {
    return name_;
}

int64_t Layer::getDurationMs() const {
    return end_time_ms_ - start_time_ms_;
}

int64_t Layer::getStartTime() const {
    return start_time_ms_;
}

int64_t Layer::getEndTime() const {
    return end_time_ms_;
}

bool Layer::isActive() const {
    if (!root_)
        return false;
    int64_t current = root_->getCurrentTime();
    return current >= start_time_ms_ && current < end_time_ms_;
}

bool Layer::hasActiveEffects() const {
    if (effects_.empty()) {
        return false;
    }

    // 计算图层的相对时间（从图层开始时间算起）
    int64_t offset_time = root_->getCurrentTime() - start_time_ms_;

    // 检查是否有任何特效在当前时间处于活跃状态
    return std::any_of(effects_.begin(), effects_.end(),
                       [offset_time](const auto &effect) {
                           return effect->isActive(offset_time);
                       });
}

bool Layer::draw() {
    if (!root_)
        return false;

    // 检查图层是否在活跃时间范围内
    if (!isActive())
        return true; // 不在时间范围内，跳过渲染（不是错误）

    // 检查是否有活跃的特效
    if (!hasActiveEffects()) {
        // 无特效：直接渲染到 render_fbo_
        return renderContent(root_->getRenderFBO());
    }

    // 有特效：需要中间 FBO
    gl::FBO temp_fbo = root_->getFBOPool()->acquire(root_->getWidth(), root_->getHeight());
    if (!temp_fbo.isValid())
        return false;

    // 渲染内容到临时 FBO
    if (!renderContent(temp_fbo)) {
        setError("render content failed");
        root_->getFBOPool()->release(temp_fbo);
        return false;
    }

    gl::FBO final_effect_output = applyEffects(temp_fbo);
    if (!final_effect_output.isValid()) {
        setError("apply effects failed");
        root_->getFBOPool()->release(temp_fbo);
        return false;
    }

    // 使用通用函数绘制特效输出到 render_fbo_
    gl::drawTextureQuad(
        root_->getRenderFBO(),
        gl::Texture{final_effect_output.texture, final_effect_output.width, final_effect_output.height},
        root_->getShader(),
        0,
        "uTex",
        root_->getQuad());

    // 释放临时 FBO
    root_->getFBOPool()->release(temp_fbo);
    root_->getFBOPool()->release(final_effect_output);
    return true;
}

gl::FBO Layer::applyEffects(const gl::FBO &input) {
    if (effects_.empty())
        return gl::FBO{}; // 返回无效 FBO

    if (!root_)
        return gl::FBO{};

    gl::FBO current = input;

    int64_t offset_time = root_->getCurrentTime() - getStartTime();
    // 依次应用特效链
    for (auto &effect : effects_) {
        if (!effect->isActive(offset_time)) {
            continue;
        }
        // 释放上一个 FBO
        if (current.isValid() && current.fbo != input.fbo) {
            root_->getFBOPool()->release(current);
        }
        gl::FBO output = effect->apply(current, offset_time);
        if (!output.isValid()) {
            // 特效失败，返回无效 FBO
            return gl::FBO{};
        }
        current = output;
    }

    return current; // 返回最后一个特效的输出
}

} // namespace vp
