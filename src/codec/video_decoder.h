#pragma once

#include "../core/types.h"
#include <cstdint>
#include <string>

extern "C" {
#include <libavutil/rational.h>
}

struct AVFormatContext;
struct AVCodecContext;
struct AVBufferRef;
struct SwsContext;
struct AVFrame;
struct AVPacket;

namespace vp {

// 解码后的视频帧
struct VideoFrame {
    uint8_t *data = nullptr;    // SW: CPU RGBA 像素
    void *native_buf = nullptr; // HW: 平台原生缓冲区 (macOS: CVPixelBufferRef)
    int width = 0;
    int height = 0;
    TimeMs pts_ms = 0;
    bool valid = false;
    bool hw = false;

    void releaseNative();
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
    void ensureSwsContext(int src_fmt);
    TimeMs ptsToMs(int64_t pts) const;
    int64_t msToPts(TimeMs ms) const;

    void savePrevFrame(const VideoFrame &current);
    bool restorePrevFrame(VideoFrame &out);
    /** seek 到 time_ms 附近关键帧并 flush，清空解码游标与 prev */
    bool seekTo(TimeMs time_ms);

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

    AVBufferRef *hw_device_ctx_ = nullptr;
    bool hw_accel_ = false;
    int sws_src_fmt_ = -1;

    uint8_t *rgba_buffers_[2] = {nullptr, nullptr};
    int active_buf_ = 0;
    /** 唯一帧缓存：顺序解码时保留的「上一帧」，供回退/相邻请求复用 */
    VideoFrame prev_frame_;
    int prev_buf_idx_ = -1;

    /** 解码器游标：最后一次写入 out 的帧时间，并非单独再缓存一帧「当前」 */
    TimeMs last_decoded_ms_ = kInvalidTime;
    std::string path_;
    bool has_alpha_ = false;
};

} // namespace vp
