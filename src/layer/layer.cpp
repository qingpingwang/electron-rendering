#include "layer.h"
#include "../engine/root_node.h"

using json = nlohmann::json;

namespace vp {

Layer::Layer(RootNode *root) :
    root_(root) {
}

bool Layer::load(const json &segment_json) {
    if (!root_)
        return false;

    // 解析基础属性
    name_ = segment_json.value("id", "layer");

    // 解析时间范围
    if (segment_json.contains("target_timerange")) {
        start_time_ms_ = segment_json["target_timerange"].value("start", 0);
        int64_t duration = segment_json["target_timerange"].value("duration", 0);
        end_time_ms_ = start_time_ms_ + duration;
    }

    // 获取素材指针
    std::string material_id = segment_json.value("material_id", "");
    if (!material_id.empty()) {
        material_ = root_->getMaterial(material_id);
    }

    return true;
}

const std::string &Layer::getName() const {
    return name_;
}

int Layer::getWidth() const {
    return width_;
}

int Layer::getHeight() const {
    return height_;
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

bool Layer::draw() {
    if (!root_)
        return false;

    // 检查图层是否在活跃时间范围内
    if (!isActive())
        return true; // 不在时间范围内，跳过渲染（不是错误）

    // 检查是否有特效
    bool has_effects = !effects_.empty();

    if (!has_effects) {
        // 无特效：直接渲染到 render_fbo_
        return renderContent(root_->getRenderFBO());
    }

    // 有特效：需要中间 FBO
    gl::FBO temp_fbo = root_->getFBOPool()->acquire(width_, height_);
    if (!temp_fbo.isValid())
        return false;

    // 渲染内容到临时 FBO
    if (!renderContent(temp_fbo)) {
        root_->getFBOPool()->release(temp_fbo);
        return false;
    }

    gl::FBO final_effect_output = applyEffects(temp_fbo);
    if (!final_effect_output.isValid()) {
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

    // 依次应用特效链
    // 每个特效自己管理输出 FBO，Layer 不负责创建/释放
    for (auto &effect : effects_) {
        gl::FBO output = effect->apply(current, root_->getCurrentTime());
        if (!output.isValid()) {
            // 特效失败，返回无效 FBO
            return gl::FBO{};
        }
        current = output;
    }

    return current; // 返回最后一个特效的输出
}

} // namespace vp
