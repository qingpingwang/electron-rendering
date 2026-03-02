#include "layer.h"
#include "../core/root_node.h"
#include "effect.h"
#include "../gl/functions.h"
#include "../gl/shader.h"
#include "../resource/render_resource.h"
#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

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
    target_range_.start = config["target_timerange"].value("start", 0);
    target_range_.duration = config["target_timerange"].value("duration", 0);

    // 获取素材指针
    material_id_ = config.value("material_id", "");
    if (material_id_.empty()) {
        setError("material_id is required");
        return false;
    }
    material_ = root_->getMaterial(getMaterialType(), material_id_);

    // 解析音量（所有图层通用，默认 1.0）
    volume_ = config.value("volume", 1.0f);

    // 解析源时间范围（视频/音频用于裁剪映射）
    if (config.contains("source_timerange")) {
        source_range_.start = config["source_timerange"].value("start", 0);
        source_range_.duration = config["source_timerange"].value("duration", 0);
    }

    // 解析 clip 属性
    if (config.contains("clip")) {
        auto &c = config["clip"];
        clip_.alpha = c.value("alpha", 1.0f);
        clip_.rotation = c.value("rotation", 0.0f);
        if (c.contains("flip")) {
            clip_.flip_h = c["flip"].value("horizontal", false);
            clip_.flip_v = c["flip"].value("vertical", false);
        }
        if (c.contains("scale")) {
            clip_.scale_x = c["scale"].value("x", 1.0f);
            clip_.scale_y = c["scale"].value("y", 1.0f);
        }
        if (c.contains("transform")) {
            clip_.transform_x = c["transform"].value("x", 0.0f);
            clip_.transform_y = c["transform"].value("y", 0.0f);
        }
    }

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
    return target_range_.duration;
}

TimeMs Layer::getStartTime() const {
    return target_range_.start;
}

TimeMs Layer::getEndTime() const {
    return target_range_.end();
}

bool Layer::isActive(TimeMs time_ms) const {
    return time_ms >= target_range_.start && time_ms < target_range_.end();
}

float Layer::getVolume() const {
    return volume_;
}

const TimeRange &Layer::getTargetRange() const {
    return target_range_;
}

const TimeRange &Layer::getSourceRange() const {
    return source_range_;
}

bool Layer::hasActiveEffects(TimeMs time_ms) const {
    if (effects_.empty())
        return false;

    TimeMs offset = time_ms - target_range_.start;
    return std::any_of(effects_.begin(), effects_.end(),
                       [offset](const auto &effect) {
                           return effect->isActive(offset);
                       });
}

static glm::mat4 computeModelMatrix(const Clip &clip, float aspect) {
    // glm 左乘: m = m * Op, 对顶点实际执行顺序从下往上读:
    // 1. scale(user)  2. scale(1/aspect)  3. rotate  4. scale(aspect)  5. translate
    // 即: 先缩放 → 等比空间旋转 → 最后位移（位移不受旋转影响）
    float sx = clip.flip_h ? -clip.scale_x : clip.scale_x;
    float sy = clip.flip_v ? -clip.scale_y : clip.scale_y;
    glm::mat4 m(1.0f);
    m = glm::translate(m, glm::vec3(clip.transform_x, clip.transform_y, 0.0f));
    m = glm::scale(m, glm::vec3(1.0f, aspect, 1.0f));
    m = glm::rotate(m, glm::radians(clip.rotation), glm::vec3(0.0f, 0.0f, 1.0f));
    m = glm::scale(m, glm::vec3(1.0f, 1.0f / aspect, 1.0f));
    m = glm::scale(m, glm::vec3(sx, sy, 1.0f));
    return m;
}

static void setClipUniforms(gl::Shader *shader, const Clip &clip, float aspect) {
    glm::mat4 model = computeModelMatrix(clip, aspect);
    shader->use();
    shader->setMat4("uModel", glm::value_ptr(model));
    shader->setFloat("uAlpha", clip.alpha);
    shader->unuse();
}

static void resetClipUniforms(gl::Shader *shader) {
    static const glm::mat4 identity(1.0f);
    shader->use();
    shader->setMat4("uModel", glm::value_ptr(identity));
    shader->setFloat("uAlpha", 1.0f);
    shader->unuse();
}

bool Layer::draw(const gl::FBO &target, TimeMs time_ms) {
    if (getMaterialType() == MATERIAL_TYPE_AUDIO)
        return true;

    if (!root_ || !target.isValid())
        return false;

    if (!isActive(time_ms))
        return true;

    auto *shader = root_->getShader();
    float aspect = static_cast<float>(target.width) / target.height;
    setClipUniforms(shader, clip_, aspect);

    if (!hasActiveEffects(time_ms)) {
        bool ok = renderContent(target, time_ms);
        resetClipUniforms(shader);
        return ok;
    }

    gl::FBO temp_fbo = root_->getFBOPool()->acquire(target.width, target.height);
    if (!temp_fbo.isValid()) {
        resetClipUniforms(shader);
        return false;
    }

    if (!renderContent(temp_fbo, time_ms)) {
        setError("render content failed");
        root_->getFBOPool()->release(temp_fbo);
        resetClipUniforms(shader);
        return false;
    }

    gl::FBO effect_out = applyEffects(temp_fbo, time_ms);
    root_->getFBOPool()->release(temp_fbo);
    if (!effect_out.isValid()) {
        setError("apply effects failed");
        resetClipUniforms(shader);
        return false;
    }
    resetClipUniforms(shader);
    gl::drawTextureQuad(target, gl::Texture{effect_out.texture, effect_out.width, effect_out.height},
                        shader, 0, "uTex", root_->getQuad());
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

gl::QuadMesh Layer::createFitQuad(int layer_w, int layer_h, int canvas_w, int canvas_h) {
    float scale = std::min(static_cast<float>(canvas_w) / layer_w,
                           static_cast<float>(canvas_h) / layer_h);
    float nw = (layer_w * scale) / canvas_w;
    float nh = (layer_h * scale) / canvas_h;
    const float vertices[] = {
        -nw,
        nh,
        0.0f,
        1.0f,
        nw,
        nh,
        1.0f,
        1.0f,
        -nw,
        -nh,
        0.0f,
        0.0f,
        nw,
        -nh,
        1.0f,
        0.0f,
    };
    return gl::createQuadMesh(vertices, sizeof(vertices));
}

} // namespace vp
