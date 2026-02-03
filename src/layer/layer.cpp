#include "layer.h"
#include "../core/root_node.h"

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

} // namespace vp
