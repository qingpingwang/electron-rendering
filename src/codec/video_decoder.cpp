#include "video_decoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/hwcontext.h>
#include <libswscale/swscale.h>
}

namespace vp {

VideoDecoder::VideoDecoder() {
    packet_ = av_packet_alloc();
    frame_ = av_frame_alloc();
    frame_rgba_ = av_frame_alloc();
}

VideoDecoder::~VideoDecoder() {
    close();
    if (packet_)
        av_packet_free(&packet_);
    if (frame_)
        av_frame_free(&frame_);
    if (frame_rgba_)
        av_frame_free(&frame_rgba_);
}

bool VideoDecoder::open(const std::string &path) {
    close();

    path_ = path;

    // WebM: 启用 Alpha 通道解封装
    AVDictionary *format_opts = nullptr;
    av_dict_set(&format_opts, "enable_drefs", "1", 0);

    if (avformat_open_input(&format_ctx_, path_.c_str(), nullptr, &format_opts) < 0) {
        av_dict_free(&format_opts);
        return false;
    }
    av_dict_free(&format_opts);

    if (avformat_find_stream_info(format_ctx_, nullptr) < 0) {
        close();
        return false;
    }

    // 查找视频流
    video_stream_idx_ = -1;
    for (unsigned int i = 0; i < format_ctx_->nb_streams; i++) {
        if (format_ctx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_idx_ = i;
            break;
        }
    }
    if (video_stream_idx_ < 0) {
        close();
        return false;
    }

    AVStream *stream = format_ctx_->streams[video_stream_idx_];
    AVCodecParameters *codecpar = stream->codecpar;

    // 检查是否有 Alpha 通道（VP9 WebM）
    has_alpha_ = false;
    AVDictionaryEntry *alpha_tag = av_dict_get(stream->metadata, "alpha_mode", nullptr, 0);
    if (alpha_tag && std::string(alpha_tag->value) == "1") {
        has_alpha_ = true;
    }

    // 如果是 VP9 with alpha，强制使用 libvpx-vp9 解码器
    const AVCodec *codec = nullptr;
    if (codecpar->codec_id == AV_CODEC_ID_VP9 && has_alpha_) {
        codec = avcodec_find_decoder_by_name("libvpx-vp9");
    }

    // 否则使用默认解码器
    if (!codec) {
        codec = avcodec_find_decoder(codecpar->codec_id);
    }

    if (!codec) {
        close();
        return false;
    }

    codec_ctx_ = avcodec_alloc_context3(codec);
    if (!codec_ctx_ || avcodec_parameters_to_context(codec_ctx_, codecpar) < 0) {
        close();
        return false;
    }

    // VP9: 启用 Alpha 解码
    AVDictionary *opts = nullptr;
    if (codecpar->codec_id == AV_CODEC_ID_VP9) {
        // 请求输出 YUVA420P 格式（包含 alpha 通道）
        codec_ctx_->request_sample_fmt = AV_SAMPLE_FMT_NONE;
        // 强制解码 alpha 通道
        av_dict_set(&opts, "apply_cropping", "0", 0);
    }

    if (avcodec_open2(codec_ctx_, codec, &opts) < 0) {
        av_dict_free(&opts);
        close();
        return false;
    }
    av_dict_free(&opts);

    width_ = codec_ctx_->width;
    height_ = codec_ctx_->height;
    time_base_ = stream->time_base;

    if (format_ctx_->duration != AV_NOPTS_VALUE) {
        duration_ms_ = format_ctx_->duration / (AV_TIME_BASE / 1000);
    } else if (stream->duration != AV_NOPTS_VALUE) {
        duration_ms_ = ptsToMs(stream->duration);
    }

    if (stream->avg_frame_rate.den > 0) {
        frame_rate_ = av_q2d(stream->avg_frame_rate);
    } else if (stream->r_frame_rate.den > 0) {
        frame_rate_ = av_q2d(stream->r_frame_rate);
    } else {
        frame_rate_ = 25.0;
    }

    // 创建格式转换上下文
    // 如果有 alpha，尝试使用 yuva420p 作为源格式
    AVPixelFormat src_fmt = has_alpha_ ? AV_PIX_FMT_YUVA420P : codec_ctx_->pix_fmt;

    sws_ctx_ = sws_getContext(
        width_, height_, src_fmt,
        width_, height_, AV_PIX_FMT_RGBA,
        SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws_ctx_) {
        close();
        return false;
    }

    // 根据解码器报告的色彩范围设置转换参数
    // 注意：实际数据是 limited range (16-235)
    int src_range = (codec_ctx_->color_range == AVCOL_RANGE_JPEG) ? 1 : 0;
    int dst_range = 1; // RGBA 总是 full range

    // 使用 BT.709 色彩空间（现代视频标准）
    const int *inv_table = sws_getCoefficients(SWS_CS_ITU709);
    sws_setColorspaceDetails(sws_ctx_,
                             inv_table, src_range,
                             inv_table, dst_range,
                             0, 1 << 16, 1 << 16);

    // 分配 RGBA 缓冲区
    int buffer_size = av_image_get_buffer_size(AV_PIX_FMT_RGBA, width_, height_, 1);
    rgba_buffer_ = (uint8_t *)av_malloc(buffer_size);

    av_image_fill_arrays(
        frame_rgba_->data, frame_rgba_->linesize,
        rgba_buffer_, AV_PIX_FMT_RGBA,
        width_, height_, 1);

    last_decoded_ms_ = kInvalidTime;
    return true;
}

bool VideoDecoder::hasAlpha() const {
    return has_alpha_;
}

void VideoDecoder::close() {
    if (sws_ctx_) {
        sws_freeContext(sws_ctx_);
        sws_ctx_ = nullptr;
    }
    if (codec_ctx_) {
        avcodec_free_context(&codec_ctx_);
    }
    if (format_ctx_) {
        avformat_close_input(&format_ctx_);
    }
    if (rgba_buffer_) {
        av_free(rgba_buffer_);
        rgba_buffer_ = nullptr;
    }

    video_stream_idx_ = -1;
    width_ = height_ = 0;
    duration_ms_ = 0;
    frame_rate_ = 0.0;
    last_decoded_ms_ = kInvalidTime;
}

bool VideoDecoder::decodeFrameAt(TimeMs time_ms, VideoFrame &out) {
    if (!format_ctx_ || video_stream_idx_ < 0)
        return false;

    if (time_ms >= duration_ms_)
        time_ms = duration_ms_ - 1;

    // 计算帧间隔
    TimeMs frame_interval = (frame_rate_ > 0) ? (TimeMs)(1000.0 / frame_rate_) : 40;

    // 判断是否需要 seek
    bool need_seek = false;
    if (last_decoded_ms_ == kInvalidTime) {
        need_seek = true;
    } else if (time_ms < last_decoded_ms_ && (last_decoded_ms_ - time_ms) > frame_interval) {
        // 倒退超过一帧间隔才seek（容错视频帧PTS的时间戳偏差）
        need_seek = true;
    } else if (last_decoded_ms_ != kInvalidTime && time_ms > last_decoded_ms_ && (time_ms - last_decoded_ms_) > frame_interval * 10) {
        need_seek = true;
    }

    // 如果需要seek，先跳转
    if (need_seek) {
        int64_t target_pts = msToPts(time_ms);
        if (av_seek_frame(format_ctx_, video_stream_idx_, target_pts, AVSEEK_FLAG_BACKWARD) < 0)
            return false;
        avcodec_flush_buffers(codec_ctx_);
        last_decoded_ms_ = kInvalidTime; // 重置，强制解码
    }

    // 已经在目标时间或之后，直接返回（避免重复解码）
    if (last_decoded_ms_ != kInvalidTime && last_decoded_ms_ >= time_ms)
        return true;

    // 解码直到目标时间（seek和顺序解码共用此逻辑）
    while (last_decoded_ms_ == kInvalidTime || last_decoded_ms_ < time_ms) {
        if (!decodeNextFrame(out))
            return false;
        last_decoded_ms_ = out.pts_ms;
    }

    return out.valid;
}

bool VideoDecoder::decodeNextFrame(VideoFrame &out) {
    while (true) {
        int ret = av_read_frame(format_ctx_, packet_);

        if (ret < 0) {
            out.valid = false;
            return false;
        }

        if (packet_->stream_index != video_stream_idx_) {
            av_packet_unref(packet_);
            continue;
        }

        ret = avcodec_send_packet(codec_ctx_, packet_);
        av_packet_unref(packet_);

        if (ret < 0) {
            continue;
        }

        ret = avcodec_receive_frame(codec_ctx_, frame_);

        if (ret == AVERROR(EAGAIN)) {
            continue;
        }
        if (ret < 0) {
            out.valid = false;
            return false;
        }

        convertToRGBA(frame_, out);

        av_frame_unref(frame_);
        return true;
    }
}

void VideoDecoder::convertToRGBA(AVFrame *frame, VideoFrame &out) {
    // 检查实际的 frame 格式
    AVPixelFormat actual_fmt = (AVPixelFormat)frame->format;
    bool frame_has_alpha = (actual_fmt == AV_PIX_FMT_YUVA420P || actual_fmt == AV_PIX_FMT_YUVA420P10LE || actual_fmt == AV_PIX_FMT_YUVA420P10BE);

    // 如果 sws_context 的格式与实际 frame 格式不匹配，重新创建
    if (has_alpha_ && !frame_has_alpha) {
        // 元数据说有 alpha，但实际解码没有 alpha（FFmpeg bug）
        // 使用实际格式重新创建 sws_context
        sws_freeContext(sws_ctx_);
        sws_ctx_ = sws_getContext(
            width_, height_, actual_fmt,
            width_, height_, AV_PIX_FMT_RGBA,
            SWS_BILINEAR, nullptr, nullptr, nullptr);

        has_alpha_ = false; // 更新标记
    }

    // CPU解码：YUV(A)转RGBA
    sws_scale(
        sws_ctx_,
        frame->data, frame->linesize,
        0, height_,
        frame_rgba_->data, frame_rgba_->linesize);

    out.data = rgba_buffer_;
    out.width = width_;
    out.height = height_;
    out.pts_ms = ptsToMs(frame->pts);
    out.valid = true;
}

TimeMs VideoDecoder::ptsToMs(int64_t pts) const {
    if (pts == AV_NOPTS_VALUE)
        return 0;
    return av_rescale_q(pts, time_base_, {1, 1000});
}

int64_t VideoDecoder::msToPts(TimeMs ms) const {
    return av_rescale_q(ms, {1, 1000}, time_base_);
}

int VideoDecoder::getWidth() const {
    return width_;
}

int VideoDecoder::getHeight() const {
    return height_;
}

TimeMs VideoDecoder::getDurationMs() const {
    return duration_ms_;
}

double VideoDecoder::getFrameRate() const {
    return frame_rate_;
}

bool VideoDecoder::isOpen() const {
    return format_ctx_ != nullptr;
}

} // namespace vp
