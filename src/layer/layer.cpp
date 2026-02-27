#include "layer.h"
#include "../core/root_node.h"
#include "effect.h"
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
    TimeMs duration = config["target_timerange"].value("duration", 0);
    end_time_ms_ = start_time_ms_ + duration;

    // 获取素材指针
    std::string material_id = config.value("material_id", "");
    if (material_id.empty()) {
        setError("material_id is required");
        return false;
    }
    material_ = root_->getMaterial(getMaterialType(), material_id);

    auto ref_ids = config.value("extra_material_refs", std::vector<std::string>{});
    for (const auto &ref_id : ref_ids) {
        static const MaterialType kRefTypes[] = {MATERIAL_TYPE_EFFECT, MATERIAL_TYPE_TRANSITION};
        Material *mat = nullptr;
        MaterialType mat_type = MATERIAL_TYPE_EFFECT;
        for (auto type : kRefTypes) {
            mat = root_->getMaterial(type, ref_id);
            if (mat) {
                mat_type = type;
                break;
            }
        }
        if (!mat) {
            setError("extra material not found: " + ref_id);
            return false;
        }

        auto *effect_mat = static_cast<EffectMaterial *>(mat);
        auto effect = std::make_unique<ResourceEffect>(root_);
        if (!effect->loadFromFolder(effect_mat->getResourcePath())) {
            setError("load extra[" + ref_id + "] failed: " + effect->getRenderResource()->getErrorMessage());
            return false;
        }

        if (mat_type == MATERIAL_TYPE_TRANSITION) {
            auto *trans_mat = static_cast<TransitionMaterial *>(mat);
            effect->getRenderResource()->setResourceDuration(trans_mat->getDuration());
            transitions_.emplace_back(std::move(effect));
        } else {
            effects_.emplace_back(std::move(effect));
        }
    }
    return true;
}

Material *Layer::getMaterial() const {
    return material_;
}

bool Layer::hasTransition() const {
    return !transitions_.empty();
}

Effect *Layer::getActiveTransition(TimeMs time_ms) const {
    if (!hasTransition())
        return nullptr;
    auto &transition = transitions_.front();
    TimeMs halfTransitionDuration = transition->getRenderResource()->getResourceDuration() / 2;
    TimeMs relativeTime = time_ms - getStartTime();
    // 时间在图层结束前[getDurationMs - transitionDuration/2, getDurationMs + transitionDuration/2]
    if (relativeTime >= getDurationMs() - halfTransitionDuration && relativeTime < getDurationMs() + halfTransitionDuration)
        return transition.get();
    return nullptr;
}

const std::string &Layer::getName() const {
    return name_;
}

TimeMs Layer::getDurationMs() const {
    return end_time_ms_ - start_time_ms_;
}

TimeMs Layer::getStartTime() const {
    return start_time_ms_;
}

TimeMs Layer::getEndTime() const {
    return end_time_ms_;
}

bool Layer::isActive(TimeMs time_ms) const {
    return time_ms >= start_time_ms_ && time_ms < end_time_ms_;
}

bool Layer::hasActiveEffects(TimeMs time_ms) const {
    if (effects_.empty())
        return false;

    TimeMs offset = time_ms - start_time_ms_;
    return std::any_of(effects_.begin(), effects_.end(),
                       [offset](const auto &effect) {
                           return effect->isActive(offset);
                       });
}

bool Layer::draw(const gl::FBO &target, TimeMs time_ms) {
    if (!root_ || !target.isValid())
        return false;

    if (!isActive(time_ms))
        return true;

    if (!hasActiveEffects(time_ms))
        return renderContent(target, time_ms);

    gl::FBO temp_fbo = root_->getFBOPool()->acquire(target.width, target.height);
    if (!temp_fbo.isValid())
        return false;

    if (!renderContent(temp_fbo, time_ms)) {
        setError("render content failed");
        root_->getFBOPool()->release(temp_fbo);
        return false;
    }

    gl::FBO effect_out = applyEffects(temp_fbo, time_ms);
    if (!effect_out.isValid()) {
        setError("apply effects failed");
        root_->getFBOPool()->release(temp_fbo);
        return false;
    }

    gl::drawTextureQuad(
        target,
        gl::Texture{effect_out.texture, effect_out.width, effect_out.height},
        root_->getShader(),
        0,
        "uTex",
        root_->getQuad());

    root_->getFBOPool()->release(temp_fbo);
    root_->getFBOPool()->release(effect_out);
    return true;
}

gl::FBO Layer::applyEffects(const gl::FBO &input, TimeMs time_ms) {
    if (effects_.empty() || !root_)
        return gl::FBO{};

    gl::FBO current = input;
    TimeMs offset = time_ms - getStartTime();

    for (auto &effect : effects_) {
        if (!effect->isActive(offset))
            continue;
        if (current.isValid() && current.fbo != input.fbo)
            root_->getFBOPool()->release(current);
        gl::FBO output = effect->apply({current}, offset);
        if (!output.isValid())
            return gl::FBO{};
        current = output;
    }

    return current;
}

} // namespace vp
