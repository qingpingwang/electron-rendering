#pragma once

#include "../layer/layer.h"
#include "../layer/video_layer.h"
#include "../render/gl_renderer.h"
#include <vector>
#include <memory>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>

namespace vp
{

    class RootNode
    {
    public:
        RootNode();
        ~RootNode();

        RootNode(const RootNode &) = delete;
        RootNode &operator=(const RootNode &) = delete;

        bool init();
        void cleanup();

        bool loadFromJson(const std::string &json_str);
        void unload();

        void setCurrentTime(int64_t time_ms);
        bool draw(uint8_t *buffer, size_t buffer_size);

        int getWidth() const { return width_; }
        int getHeight() const { return height_; }
        int64_t getDurationMs() const { return duration_ms_; }
        double getFrameRate() const { return frame_rate_; }
        bool isLoaded() const { return !layers_.empty(); }
        std::string getGPUInfo() const;

    private:
        // 渲染一帧
        bool renderFrame(int64_t time_ms, uint8_t *out_buffer);
        // 启动异步准备下一帧
        void startPrepareNextFrame(int64_t next_time_ms);
        // 取消并等待异步准备完成（兼容未启动情况）
        void cancelPrepare();
        // 缓存是否命中
        bool isCacheHit(int64_t time_ms) const;
        int64_t getHalfFrameMs() const;

        std::unique_ptr<GLRenderer> renderer_;
        std::vector<std::unique_ptr<Layer>> layers_;

        int width_ = 0;
        int height_ = 0;
        int64_t duration_ms_ = 0;
        double frame_rate_ = 0.0;
        int64_t current_time_ms_ = 0;

        // 帧缓存
        std::vector<uint8_t> cache_data_;
        int64_t cache_time_ms_ = -1;
        std::mutex cache_mutex_;

        // 异步准备线程（单次）
        std::thread prepare_thread_;
        std::atomic<bool> preparing_{false};
        std::atomic<bool> cancel_flag_{false};
    };

} // namespace vp
