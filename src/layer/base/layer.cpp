#include "layer.h"
#include "../../core/root_node.h"
#include "../../gl/functions.h"
#include "../../gl/shader.h"
#include "../../resource/render_resource.h"
#include <algorithm>
#include "include/core/SkM44.h"

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

    id_ = config.value("id", "layer");

    // 解析时间范围
    if (!config.contains("target_timerange")) {
        setError("target_timerange is required");
        return false;
    }
    target_range_.start = config["target_timerange"].value("start", 0);
    target_range_.duration = config["target_timerange"].value("duration", 0);

    // 获取素材指针
    std::string material_id = config.value("material_id", "");
    if (material_id.empty()) {
        setError("material_id is required");
        return false;
    }
    material_ = root_->getMaterial(getMaterialType(), material_id);

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

    // extra_material_refs：解析材质 ID 引用，存指针（材质由 RootNode 持有和加载）
    auto ref_ids = config.value("extra_material_refs", std::vector<std::string>{});
    for (const auto &ref_id : ref_ids) {
        static const MaterialType kRefTypes[] = {MATERIAL_TYPE_EFFECT, MATERIAL_TYPE_TRANSITION};
        Material *mat = nullptr;
        MaterialType mat_type = MATERIAL_TYPE_EFFECT;
        for (auto type : kRefTypes) {
            mat = root_->getMaterial(type, ref_id);
            if (mat) { mat_type = type; break; }
        }
        if (!mat) {
            setError("extra material not found: " + ref_id);
            return false;
        }

        auto *effect_mat = static_cast<EffectMaterial *>(mat);
        if (mat_type == MATERIAL_TYPE_TRANSITION)
            transitions_.emplace_back(EffectInfo{static_cast<TransitionMaterial *>(mat)});
        else
            effects_.emplace_back(EffectInfo{effect_mat});
    }

    visible_ = config.value("visible", true);
    muted_ = config.value("muted", false);
    volume_ = config.value("volume", 1.0f);
    return true;
}

json Layer::dump() const {
    json j;
    j["id"] = id_;
    j["material_id"] = material_->getId();

    j["target_timerange"] = {
        {"start", target_range_.start},
        {"duration", target_range_.duration},
    };

    if (!effects_.empty() || !transitions_.empty()) {
        auto &refs = j["extra_material_refs"] = json::array();
        for (const auto &e : effects_) {
            refs.push_back(e.material->getId());
        }
        for (const auto &t : transitions_) {
            refs.push_back(t.material->getId());
        }
    }

    return j;
}

Material *Layer::getMaterial() const {
    return material_;
}

bool Layer::hasTransition() const {
    return !transitions_.empty();
}

Effect *Layer::getActiveTransition(TimeMs time_ms) const {
    if (!hasTransition()) return nullptr;
    auto *mat = transitions_.front().material;
    TimeMs halfDur = mat->getDurationMs() / 2;
    TimeMs relTime = time_ms - getStartTime();
    if (relTime >= getDurationMs() - halfDur && relTime < getDurationMs() + halfDur)
        return mat->getEffect();
    return nullptr;
}

bool Layer::isVisible() const {
    return visible_;
}

void Layer::setVisible(bool v) {
    visible_ = v;
}

bool Layer::isMuted() const {
    return muted_;
}

void Layer::setMuted(bool m) {
    muted_ = m;
}

const std::string &Layer::getId() const {
    return id_;
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

json Layer::dumpClip() const {
    json j;
    j["alpha"] = clip_.alpha;
    j["rotation"] = clip_.rotation;
    j["flip"] = {{"horizontal", clip_.flip_h}, {"vertical", clip_.flip_v}};
    j["scale"] = {{"x", clip_.scale_x}, {"y", clip_.scale_y}};
    j["transform"] = {{"x", clip_.transform_x}, {"y", clip_.transform_y}};
    return j;
}

json Layer::dumpSourceRange() const {
    json j;
    j["start"] = source_range_.start;
    j["duration"] = source_range_.duration;
    return j;
}

bool Layer::hasActiveEffects(TimeMs time_ms) const {
    if (effects_.empty()) return false;
    TimeMs offset = time_ms - target_range_.start;
    return std::any_of(effects_.begin(), effects_.end(),
                       [offset](const auto &ei) { return ei.material->isActive(offset); });
}

// 顶点执行顺序（右乘）：scale(user) → scale(1/aspect) → rotate → scale(aspect) → translate
static SkM44 computeModelMatrix(const Clip &clip, float aspect) {
    float sx = clip.flip_h ? -clip.scale_x : clip.scale_x;
    float sy = clip.flip_v ? -clip.scale_y : clip.scale_y;
    SkM44 m;
    m = SkM44::Translate(clip.transform_x, clip.transform_y, 0.0f) * m;
    m = SkM44::Scale(1.0f, aspect, 1.0f) * m;
    m = SkM44::Rotate({0, 0, 1}, clip.rotation * (SK_ScalarPI / 180.0f)) * m;
    m = SkM44::Scale(1.0f, 1.0f / aspect, 1.0f) * m;
    m = SkM44::Scale(sx, sy, 1.0f) * m;
    return m;
}

static void setClipUniforms(gl::Shader *shader, const Clip &clip, float aspect) {
    SkM44 model = computeModelMatrix(clip, aspect);
    float col_major[16];
    model.getColMajor(col_major);
    shader->use();
    shader->setMat4("uModel", col_major);
    shader->setFloat("uAlpha", clip.alpha);
    shader->unuse();
}

static void resetClipUniforms(gl::Shader *shader) {
    static const SkM44 identity;
    float col_major[16];
    identity.getColMajor(col_major);
    shader->use();
    shader->setMat4("uModel", col_major);
    shader->setFloat("uAlpha", 1.0f);
    shader->unuse();
}

// 图层内容合成到 target 时启用/关闭混合。target 在一帧内会被多个 group/layer
// 依次绘制，若不开混合，后绘制的图层会直接覆盖像素而非按 alpha 叠加，
// 半透明图层、带透明通道的内容会出现"发黑/穿帮"。仅在合成瞬间开启，
// 用完立即关闭，不污染其他绘制路径（如 Skia 文字自己管理混合状态）的 GL 状态。
static void setBlendEnabled(bool enabled) {
    if (enabled) {
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        glDisable(GL_BLEND);
    }
}

bool Layer::draw(const gl::FBO &target, TimeMs time_ms) {
    if (!isVisible())
        return true;

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
        setBlendEnabled(true);
        bool ok = renderContent(target, time_ms);
        setBlendEnabled(false);
        resetClipUniforms(shader);
        return ok;
    }

    gl::FBO temp_fbo = root_->getFBOPool()->acquire(target.width, target.height);
    if (!temp_fbo.isValid()) {
        resetClipUniforms(shader);
        return false;
    }
    // FBO 池中的 FBO 首次分配时纹理数据未初始化，显式清透明，避免脏数据参与后续混合
    gl::bindFBO(temp_fbo);
    gl::cleanColor(0.0f, 0.0f, 0.0f, 0.0f);
    gl::unbindFBO();

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
    setBlendEnabled(true);
    gl::drawTextureQuad(target, gl::Texture{effect_out.texture, effect_out.width, effect_out.height},
                        shader, 0, "uTex", root_->getQuad());
    setBlendEnabled(false);
    root_->getFBOPool()->release(effect_out);
    return true;
}

gl::FBO Layer::applyEffects(const gl::FBO &input, TimeMs time_ms) {
    if (effects_.empty() || !root_) return gl::FBO{};

    gl::FBO current = input;
    TimeMs offset = time_ms - getStartTime();

    for (auto &ei : effects_) {
        if (!ei.material->isActive(offset)) continue;
        if (current.isValid() && current.fbo != input.fbo)
            root_->getFBOPool()->release(current);
        gl::FBO output = ei.material->apply({current}, offset);
        if (!output.isValid()) return gl::FBO{};
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
