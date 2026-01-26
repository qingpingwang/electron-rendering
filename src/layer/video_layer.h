#pragma once

#include "layer.h"
#include "../decoder/video_decoder.h"
#include <memory>

namespace vp
{

    // 视频图层
    class VideoLayer : public Layer
    {
    public:
        VideoLayer(const std::string &name = "video");
        ~VideoLayer() override;

        bool load(const std::string &path);
        void unload();

        void setCurrentTime(int64_t time_ms) override;
        void draw(GLRenderer &renderer) override;

        // 解码指定时间的帧，存储到 out_frame（用于异步预解码）
        bool decodeFrame(int64_t time_ms, VideoFrame &out_frame);

        // 使用给定的帧数据绘制（用于缓存命中时）
        void drawWithFrame(GLRenderer &renderer, const VideoFrame &frame);
        // 使用给定的 RGBA 数据绘制
        void drawWithData(GLRenderer &renderer, const uint8_t *data, int width, int height);

        double getFrameRate() const;
        bool isLoaded() const;

    private:
        std::unique_ptr<VideoDecoder> decoder_;
        VideoFrame current_frame_;
    };

} // namespace vp
