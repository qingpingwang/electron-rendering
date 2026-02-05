#include "animation.h"
#include "../gl/shader.h"
#include <algorithm>
#include <cmath>

namespace vp {

ResourceAnimation::ResourceAnimation() :
    channel_num_(1), repeat_mode_(0) {
}

ResourceAnimation::~ResourceAnimation() {
}

bool ResourceAnimation::load(const nlohmann::json &config) {
    // 加载基本信息（使用 value 提供默认值）
    name_ = config.value("name", "");
    channel_num_ = config.value("channelNum", 1);
    strength_ = config.value("strength", 1.0f);
    speed_ = config.value("speed", 1.0f);
    repeat_mode_ = config.value("repeatMode", 0);
    interpolation_type_ = config.value("interpolationType", "linear");

    // 加载影响的 pass 列表
    if (config.contains("renderPassIndex")) {
        const auto &idx_array = config["renderPassIndex"];
        render_pass_index_list_.reserve(idx_array.size());
        for (const auto &idx : idx_array) {
            render_pass_index_list_.emplace_back(idx.get<int>());
        }
    }

    // 加载关键帧
    if (config.contains("animationInfo")) {
        const auto &kf_array = config["animationInfo"];
        keyframes_.reserve(kf_array.size());

        for (const auto &keyframe_config : kf_array) {
            Keyframe keyframe;
            keyframe.time_ms = keyframe_config["time"].get<int64_t>();

            if (keyframe_config.contains("data")) {
                const auto &data_array = keyframe_config["data"];
                keyframe.data.reserve(data_array.size());
                for (const auto &val : data_array) {
                    keyframe.data.emplace_back(val.get<float>());
                }
            }

            keyframes_.emplace_back(std::move(keyframe));
        }
    }

    return !keyframes_.empty();
}

int64_t ResourceAnimation::adjustTime(int64_t time_ms) const {
    if (keyframes_.empty())
        return 0;

    // 应用速度
    time_ms = static_cast<int64_t>(time_ms * speed_);

    int64_t max_time = keyframes_.back().time_ms;

    if (repeat_mode_ == 0) {
        // 停止在最后一帧
        return std::min(time_ms, max_time);
    } else if (repeat_mode_ == 2) {
        // 循环
        if (max_time > 0) {
            return time_ms % max_time;
        }
    }

    return time_ms;
}

float ResourceAnimation::interpolate(int64_t time_ms, int channel) const {
    if (keyframes_.empty() || channel >= channel_num_)
        return 0.0f;

    time_ms = adjustTime(time_ms);

    // 查找关键帧
    if (time_ms <= keyframes_[0].time_ms) {
        return keyframes_[0].data[channel] * strength_;
    }
    if (time_ms >= keyframes_.back().time_ms) {
        return keyframes_.back().data[channel] * strength_;
    }

    // 线性插值（简化版，暂不实现 cubic）
    for (size_t i = 0; i < keyframes_.size() - 1; ++i) {
        if (time_ms < keyframes_[i].time_ms || time_ms > keyframes_[i + 1].time_ms) {
            continue;
        }

        // 找到区间，执行插值
        float t = static_cast<float>(time_ms - keyframes_[i].time_ms) / static_cast<float>(keyframes_[i + 1].time_ms - keyframes_[i].time_ms);
        float v0 = keyframes_[i].data[channel];
        float v1 = keyframes_[i + 1].data[channel];
        return (v0 + (v1 - v0) * t) * strength_;
    }

    return 0.0f;
}

float ResourceAnimation::getValueAt(int64_t time_ms, int channel) const {
    return interpolate(time_ms, channel);
}

std::vector<float> ResourceAnimation::getValuesAt(int64_t time_ms) const {
    std::vector<float> values;
    values.reserve(channel_num_);
    for (int i = 0; i < channel_num_; ++i) {
        values.emplace_back(getValueAt(time_ms, i));
    }
    return values;
}

const std::string &ResourceAnimation::getName() const {
    return name_;
}

int ResourceAnimation::getChannelNum() const {
    return channel_num_;
}

float &ResourceAnimation::strength() {
    return strength_;
}

float &ResourceAnimation::speed() {
    return speed_;
}

bool ResourceAnimation::affectsPass(int pass_index) const {
    return std::find(render_pass_index_list_.begin(), render_pass_index_list_.end(),
                     pass_index)
           != render_pass_index_list_.end();
}

std::unique_ptr<UniformParam> ResourceAnimation::convertToUniformParam(int64_t time_ms) const {
    // 获取当前时间点的动画值
    std::vector<float> values = getValuesAt(time_ms);

    // 根据通道数确定类型
    UniformType type;
    if (channel_num_ == 1) {
        type = UniformType::Float;
    } else if (channel_num_ == 2) {
        type = UniformType::Vec2;
    } else if (channel_num_ == 3) {
        type = UniformType::Vec3;
    } else if (channel_num_ == 4) {
        type = UniformType::Vec4;
    } else {
        type = UniformType::Float; // 默认
    }

    // 列表为空表示应用到所有 passes
    bool apply_to_all = render_pass_index_list_.empty();
    return std::make_unique<UniformParam>(name_, type, std::move(values), "", apply_to_all, render_pass_index_list_);
}

} // namespace vp
