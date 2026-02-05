#pragma once

#include <cstdint>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

namespace vp {

// 编码器配置
struct EncoderConfig {
    int width = 1920;
    int height = 1080;
    int bit_rate = 4000000;  // 4 Mbps
    std::string preset = "medium";
    int crf = 23;
};

// FFmpeg 视频编码器
class VideoEncoder {
public:
    VideoEncoder();
    ~VideoEncoder();

    VideoEncoder(const VideoEncoder &) = delete;
    VideoEncoder &operator=(const VideoEncoder &) = delete;

    // 打开编码器
    bool open(const std::string &output_file, const EncoderConfig &config);

    // 编码一帧（输入 RGBA 数据和时间戳，单位：毫秒）
    bool encodeFrame(const uint8_t *rgba_data, int64_t pts_ms);

    // 关闭编码器（会 flush 所有缓存帧）
    void close();

    // 获取编码信息
    int getWidth() const;
    int getHeight() const;
    int64_t getEncodedFrames() const;
    bool isOpen() const;

private:
    bool writePacket(AVPacket *pkt);

    AVFormatContext *fmt_ctx_ = nullptr;
    AVCodecContext *codec_ctx_ = nullptr;
    AVStream *stream_ = nullptr;
    AVFrame *frame_ = nullptr;
    SwsContext *sws_ctx_ = nullptr;

    int width_ = 0;
    int height_ = 0;
    int64_t frame_count_ = 0;
    bool is_open_ = false;
};

} // namespace vp
