#pragma once

#include "../core/types.h"
#include <cstdint>
#include <string>

extern "C" {
#include <libavutil/rational.h>
}

struct AVFormatContext;
struct AVCodecContext;
struct SwsContext;
struct AVFrame;
struct AVPacket;

namespace vp {

// 解码后的视频帧
struct VideoFrame {
    uint8_t *data = nullptr; // CPU内存中的RGBA数据
    int width = 0;
    int height = 0;
    TimeMs pts_ms = 0;
    bool valid = false;
};

// FFmpeg 视频解码器
class VideoDecoder {
public:
    VideoDecoder();
    ~VideoDecoder();

    VideoDecoder(const VideoDecoder &) = delete;
    VideoDecoder &operator=(const VideoDecoder &) = delete;

    bool open(const std::string &path);
    void close();

    // 解码指定时间的帧
    bool decodeFrameAt(TimeMs time_ms, VideoFrame &out);

    int getWidth() const;
    int getHeight() const;
    TimeMs getDurationMs() const;
    double getFrameRate() const;
    bool isOpen() const;

    bool hasAlpha() const;

private:
    bool decodeNextFrame(VideoFrame &out);
    void convertToRGBA(AVFrame *frame, VideoFrame &out);
    TimeMs ptsToMs(int64_t pts) const;
    int64_t msToPts(TimeMs ms) const;

    AVFormatContext *format_ctx_ = nullptr;
    AVCodecContext *codec_ctx_ = nullptr;
    SwsContext *sws_ctx_ = nullptr;
    AVFrame *frame_ = nullptr;
    AVFrame *frame_rgba_ = nullptr;
    AVPacket *packet_ = nullptr;

    int video_stream_idx_ = -1;
    int width_ = 0;
    int height_ = 0;
    TimeMs duration_ms_ = 0;
    double frame_rate_ = 0.0;
    AVRational time_base_ = {0, 1};

    uint8_t *rgba_buffer_ = nullptr;
    TimeMs last_decoded_ms_ = kInvalidTime;
    std::string path_;
    bool has_alpha_ = false; // 是否包含 Alpha 通道
};

} // namespace vp
