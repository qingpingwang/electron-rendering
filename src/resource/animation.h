#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include "uniform_param.h"

// 前置声明
namespace vp {
namespace gl {
class Shader;
}

// 资源动画系统
// 支持关键帧插值、repeatMode、speed/strength
class ResourceAnimation {
public:
    ResourceAnimation();
    ~ResourceAnimation();

    // 从 JSON 加载
    bool load(const nlohmann::json &config);

    // 获取指定时间、指定通道的值
    float getValueAt(int64_t time_ms, int channel) const;

    // 获取所有通道的值（vec2/vec3/vec4）
    std::vector<float> getValuesAt(int64_t time_ms) const;

    const std::string &getName() const;
    int getChannelNum() const;

    float &strength(); // 动画强度
    float &speed();    // 动画速度

    // 是否影响指定 pass
    bool affectsPass(int pass_index) const;

    // 转换为 UniformParam（指定时间点的静态值）
    std::unique_ptr<UniformParam> convertToUniformParam(int64_t time_ms) const;

private:
    std::string name_;
    int channel_num_; // 1,2,3,4 → float, vec2, vec3, vec4
    float strength_ = 1.0f;
    float speed_ = 1.0f;
    int repeat_mode_;                         // 0=停止最后一帧, 2=循环
    std::string interpolation_type_;          // "linear", "cubic"
    std::vector<int> render_pass_index_list_; // 影响的 pass 列表

    struct Keyframe {
        int64_t time_ms;
        std::vector<float> data;
    };
    std::vector<Keyframe> keyframes_;

    // 插值计算
    float interpolate(int64_t time_ms, int channel) const;

    // 处理 repeatMode
    int64_t adjustTime(int64_t time_ms) const;
};

} // namespace vp
