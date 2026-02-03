#pragma once

#include "../nlohmann/json.hpp"
#include <cstdint>
#include <string>

namespace vp {

class RootNode;
class Material;

// 图层基类
class Layer {
public:
    Layer(RootNode *root);
    virtual ~Layer() = default;

    Layer(const Layer &) = delete;
    Layer &operator=(const Layer &) = delete;

    // 从 JSON 加载图层基础配置（解析通用属性）
    virtual bool load(const nlohmann::json &segment_json);

    // 绘制图层（由子类实现具体绘制逻辑）
    virtual bool draw() = 0;

    const std::string &getName() const;
    int getWidth() const;
    int getHeight() const;
    int64_t getDurationMs() const;

    int64_t getStartTime() const;
    int64_t getEndTime() const;
    bool isActive() const; // 判断当前时间是否在图层的时间范围内

protected:
    RootNode *root_ = nullptr;
    Material *material_ = nullptr; // 图层使用的素材
    std::string name_;
    int width_ = 0;
    int height_ = 0;
    int64_t duration_ms_ = 0;
    int64_t start_time_ms_ = 0;
    int64_t end_time_ms_ = 0;
};

} // namespace vp
