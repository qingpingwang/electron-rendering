#pragma once

#include <string>
#include <cstdint>

namespace vp
{

    class GLRenderer;

    // 图层基类
    class Layer
    {
    public:
        Layer(const std::string &name = "") : name_(name) {}
        virtual ~Layer() = default;

        Layer(const Layer &) = delete;
        Layer &operator=(const Layer &) = delete;

        // 设置当前时间并更新状态
        virtual void setCurrentTime(int64_t time_ms) = 0;

        // 绘制到渲染器
        virtual void draw(GLRenderer &renderer) = 0;

        const std::string &getName() const { return name_; }
        int getWidth() const { return width_; }
        int getHeight() const { return height_; }
        int64_t getDurationMs() const { return duration_ms_; }

    protected:
        std::string name_;
        int width_ = 0;
        int height_ = 0;
        int64_t duration_ms_ = 0;
    };

} // namespace vp
