#include "video_decoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/hwcontext.h>
#include <libswscale/swscale.h>
}

#ifdef __APPLE__
#include <CoreVideo/CoreVideo.h>
#endif

static enum AVPixelFormat getHwFormat(AVCodecContext *, const enum AVPixelFormat *pix_fmts) {
    for (auto p = pix_fmts; *p != AV_PIX_FMT_NONE; p++) {
        if (*p == AV_PIX_FMT_VIDEOTOOLBOX)
            return AV_PIX_FMT_VIDEOTOOLBOX;
    }
    return pix_fmts[0];
}

namespace vp {

void VideoFrame::releaseNative() {
#ifdef __APPLE__
    if (native_buf) {
        CVPixelBufferRelease(static_cast<CVPixelBufferRef>(native_buf));
        native_buf = nullptr;
    }
#endif
    hw = false;
}

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

    // H.264/HEVC（无 alpha）：尝试 VideoToolbox 硬件解码
    if (!has_alpha_ && (codecpar->codec_id == AV_CODEC_ID_H264 || codecpar->codec_id == AV_CODEC_ID_HEVC)) {
        if (av_hwdevice_ctx_create(&hw_device_ctx_, AV_HWDEVICE_TYPE_VIDEOTOOLBOX,
                                   nullptr, nullptr, 0)
            == 0) {
            codec_ctx_->hw_device_ctx = av_buffer_ref(hw_device_ctx_);
            codec_ctx_->get_format = getHwFormat;
            hw_accel_ = true;
        }
    }

    AVDictionary *opts = nullptr;
    if (codecpar->codec_id == AV_CODEC_ID_VP9) {
        codec_ctx_->request_sample_fmt = AV_SAMPLE_FMT_NONE;
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

    // sws_ctx_ 在 convertToRGBA 中按实际帧格式惰性创建

    if (!hw_accel_) {
        int buffer_size = av_image_get_buffer_size(AV_PIX_FMT_RGBA, width_, height_, 1);
        rgba_buffers_[0] = (uint8_t *)av_malloc(buffer_size);
        rgba_buffers_[1] = (uint8_t *)av_malloc(buffer_size);
        active_buf_ = 0;

        av_image_fill_arrays(
            frame_rgba_->data, frame_rgba_->linesize,
            rgba_buffers_[0], AV_PIX_FMT_RGBA,
            width_, height_, 1);
    }

    current_frame_ = VideoFrame{};
    current_buf_idx_ = -1;
    current_decoded_ms_ = kInvalidTime;
    prev_frame_ = VideoFrame{};
    prev_buf_idx_ = -1;
    prev_decoded_ms_ = kInvalidTime;
    return true;
}

bool VideoDecoder::hasAlpha() const {
    return has_alpha_;
}

void VideoDecoder::copyFrameWithNativeRetain(VideoFrame &dst, const VideoFrame &src) {
    dst.releaseNative();
    dst = src;
#ifdef __APPLE__
    if (src.hw && src.native_buf)
        CVPixelBufferRetain(static_cast<CVPixelBufferRef>(src.native_buf));
#endif
}

void VideoDecoder::saveCurrentFrame(const VideoFrame &current) {
    if (!current.valid) return;
    copyFrameWithNativeRetain(current_frame_, current);
    current_buf_idx_ = active_buf_;
    current_decoded_ms_ = current.pts_ms;
}

bool VideoDecoder::restoreCurrentFrame(VideoFrame &out) {
    if (!current_frame_.valid) return false;
    copyFrameWithNativeRetain(out, current_frame_);
    active_buf_ = current_buf_idx_;
    current_decoded_ms_ = current_frame_.pts_ms;
    return true;
}

void VideoDecoder::savePrevFrame(const VideoFrame &prev_frame) {
    if (!prev_frame.valid) return;
    copyFrameWithNativeRetain(prev_frame_, prev_frame);
    prev_buf_idx_ = active_buf_;
    prev_decoded_ms_ = prev_frame.pts_ms;
}

bool VideoDecoder::restorePrevFrame(VideoFrame &out) {
    if (!prev_frame_.valid) return false;
    copyFrameWithNativeRetain(out, prev_frame_);
    active_buf_ = prev_buf_idx_;
    current_decoded_ms_ = prev_frame_.pts_ms;
    return true;
}

bool VideoDecoder::seekTo(TimeMs time_ms) {
    int64_t target_pts = msToPts(time_ms);
    if (av_seek_frame(format_ctx_, video_stream_idx_, target_pts, AVSEEK_FLAG_BACKWARD) < 0)
        return false;
    avcodec_flush_buffers(codec_ctx_);
    current_frame_.releaseNative();
    current_frame_ = VideoFrame{};
    current_buf_idx_ = -1;
    current_decoded_ms_ = kInvalidTime;
    prev_frame_.releaseNative();
    prev_frame_ = VideoFrame{};
    prev_buf_idx_ = -1;
    prev_decoded_ms_ = kInvalidTime;
    return true;
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
    for (int i = 0; i < 2; i++) {
        if (rgba_buffers_[i]) {
            av_free(rgba_buffers_[i]);
            rgba_buffers_[i] = nullptr;
        }
    }
    active_buf_ = 0;

    current_frame_.releaseNative();
    current_frame_ = VideoFrame{};
    current_buf_idx_ = -1;
    current_decoded_ms_ = kInvalidTime;

    prev_frame_.releaseNative();
    prev_frame_ = VideoFrame{};
    prev_buf_idx_ = -1;
    prev_decoded_ms_ = kInvalidTime;
    if (hw_device_ctx_) {
        av_buffer_unref(&hw_device_ctx_);
        hw_device_ctx_ = nullptr;
    }
    hw_accel_ = false;
    sws_src_fmt_ = -1;

    video_stream_idx_ = -1;
    width_ = height_ = 0;
    duration_ms_ = 0;
    frame_rate_ = 0.0;
}

bool VideoDecoder::decodeFrameAt(TimeMs time_ms, VideoFrame &out) {
    if (!format_ctx_ || video_stream_idx_ < 0)
        return false;

    if (time_ms >= duration_ms_)
        time_ms = duration_ms_ - 1;

    TimeMs frame_interval = (frame_rate_ > 0) ? (TimeMs)(1000.0 / frame_rate_) : 40;
    TimeMs half_frame = frame_interval / 2;

    auto near = [&](TimeMs t, TimeMs ref) -> bool {
        if (ref == kInvalidTime) return false;
        TimeMs d = (t >= ref) ? (t - ref) : (ref - t);
        return d <= half_frame;
    };

    // 1) 双帧缓存命中：同帧优先返回 current，其次回退到 prev
    if (current_frame_.valid && near(time_ms, current_frame_.pts_ms)) {
        return restoreCurrentFrame(out);
    }
    if (prev_frame_.valid && near(time_ms, prev_frame_.pts_ms)) {
        return restorePrevFrame(out);
    }

    // 需 seek：无游标、大跳、回拉
    bool need_seek = false;
    if (current_decoded_ms_ == kInvalidTime) {
        need_seek = true;
    } else if (time_ms < current_decoded_ms_) {
        need_seek = true;
    } else if ((time_ms - current_decoded_ms_) > frame_interval * 20) {
        need_seek = true;
    }

    if (need_seek) {
        if (!seekTo(time_ms)) {
            return false;
        }
        out.releaseNative();
        out = VideoFrame{};
    }

    while (current_decoded_ms_ == kInvalidTime || current_decoded_ms_ < time_ms) {
        if (out.valid) {
            savePrevFrame(out);
        }
        active_buf_ = 1 - active_buf_;
        if (!decodeNextFrame(out)) {
            return false;
        }
        saveCurrentFrame(out);
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

void VideoDecoder::ensureSwsContext(int src_fmt) {
    if (sws_ctx_ && sws_src_fmt_ == src_fmt)
        return;

    if (sws_ctx_)
        sws_freeContext(sws_ctx_);

    sws_ctx_ = sws_getContext(
        width_, height_, (AVPixelFormat)src_fmt,
        width_, height_, AV_PIX_FMT_RGBA,
        SWS_BILINEAR, nullptr, nullptr, nullptr);

    if (!sws_ctx_)
        return;

    int src_range = (codec_ctx_ && codec_ctx_->color_range == AVCOL_RANGE_JPEG) ? 1 : 0;
    const int *coeff = sws_getCoefficients(SWS_CS_ITU709);
    sws_setColorspaceDetails(sws_ctx_,
                             coeff, src_range,
                             coeff, 1,
                             0, 1 << 16, 1 << 16);
    sws_src_fmt_ = src_fmt;
}

void VideoDecoder::convertToRGBA(AVFrame *frame, VideoFrame &out) {
    out.releaseNative();

#ifdef __APPLE__
    if (hw_accel_ && frame->format == AV_PIX_FMT_VIDEOTOOLBOX) {
        auto pixbuf = reinterpret_cast<CVPixelBufferRef>(frame->data[3]);
        CVPixelBufferRetain(pixbuf);
        out.native_buf = pixbuf;
        out.hw = true;
        out.data = nullptr;
        out.width = width_;
        out.height = height_;
        out.pts_ms = ptsToMs(frame->pts);
        out.valid = true;
        return;
    }
#endif

    // 软件路径
    AVPixelFormat actual_fmt = (AVPixelFormat)frame->format;

    if (has_alpha_) {
        bool frame_has_alpha = (actual_fmt == AV_PIX_FMT_YUVA420P || actual_fmt == AV_PIX_FMT_YUVA420P10LE || actual_fmt == AV_PIX_FMT_YUVA420P10BE);
        if (!frame_has_alpha)
            has_alpha_ = false;
    }

    ensureSwsContext((int)actual_fmt);

    if (!sws_ctx_) {
        out.valid = false;
        return;
    }

    frame_rgba_->data[0] = rgba_buffers_[active_buf_];

    sws_scale(sws_ctx_,
              frame->data, frame->linesize,
              0, height_,
              frame_rgba_->data, frame_rgba_->linesize);

    out.data = rgba_buffers_[active_buf_];
    out.hw = false;
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
