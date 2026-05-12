#include "video_decoder.h"
#include <algorithm>
#include <cstring>

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

    if (avformat_open_input(&format_ctx_, path_.c_str(), nullptr, nullptr) < 0) {
        return false;
    }

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

    has_alpha_ = false;
    const AVCodec *codec = avcodec_find_decoder(codecpar->codec_id);
    if (!codec) {
        close();
        return false;
    }

    codec_ctx_ = avcodec_alloc_context3(codec);
    if (!codec_ctx_ || avcodec_parameters_to_context(codec_ctx_, codecpar) < 0) {
        close();
        return false;
    }

    // H.264/HEVC：macOS 优先 VideoToolbox，后续通过 CVPixelBuffer/IOSurface 进 GL。
    if (codecpar->codec_id == AV_CODEC_ID_H264 || codecpar->codec_id == AV_CODEC_ID_HEVC) {
        if (av_hwdevice_ctx_create(&hw_device_ctx_, AV_HWDEVICE_TYPE_VIDEOTOOLBOX,
                                   nullptr, nullptr, 0)
            == 0) {
            codec_ctx_->hw_device_ctx = av_buffer_ref(hw_device_ctx_);
            codec_ctx_->get_format = getHwFormat;
            hw_accel_ = true;
        }
    }

    AVDictionary *opts = nullptr;
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

    has_frame_index_ = moov_.load(path_) && moov_.hasVideoTrack();
    clearGopCache();
    decode_cursor_sample_ = -1;
    decode_cursor_gop_ = -1;
    return true;
}

bool VideoDecoder::hasAlpha() const {
    return has_alpha_;
}

void VideoDecoder::copyFrameWithNativeRetain(VideoFrame &dst, const VideoFrame &src) const {
    dst.releaseNative();
    dst = src;
#ifdef __APPLE__
    if (src.hw && src.native_buf)
        CVPixelBufferRetain(static_cast<CVPixelBufferRef>(src.native_buf));
#endif
}

void VideoDecoder::fillFrameLocation(VideoFrame &frame, const FrameLocation &loc) const {
    frame.pts_ms = loc.pts_ms;
    frame.sample_index = loc.sample_index;
    frame.display_index = loc.display_index;
    frame.gop_index = loc.gop_index;
    frame.frame_in_gop = loc.frame_in_gop;
}

void VideoDecoder::clearGopCache() {
    for (auto &cached : gop_cache_) {
        cached.frame.releaseNative();
    }
    gop_cache_.clear();
    cached_gop_index_ = -1;
}

bool VideoDecoder::restoreCachedFrame(int sample_index, VideoFrame &out) const {
    auto it = std::find_if(gop_cache_.begin(), gop_cache_.end(), [&](const CachedFrame &cached) {
        return cached.frame.valid && cached.frame.sample_index == sample_index;
    });
    if (it == gop_cache_.end()) {
        return false;
    }
    copyFrameWithNativeRetain(out, it->frame);
    return true;
}

void VideoDecoder::cacheDecodedFrame(const VideoFrame &frame) {
    if (!frame.valid || frame.sample_index < 0) {
        return;
    }
    if (cached_gop_index_ != frame.gop_index) {
        clearGopCache();
        cached_gop_index_ = frame.gop_index;
    }
    auto it = std::find_if(gop_cache_.begin(), gop_cache_.end(), [&](const CachedFrame &cached) {
        return cached.frame.sample_index == frame.sample_index;
    });
    if (it != gop_cache_.end()) {
        it->frame.releaseNative();
        gop_cache_.erase(it);
    }

    CachedFrame cached;
    cached.frame = frame;
    if (frame.hw) {
#ifdef __APPLE__
        if (frame.native_buf) {
            CVPixelBufferRetain(static_cast<CVPixelBufferRef>(frame.native_buf));
        }
#endif
    } else if (frame.data && frame.width > 0 && frame.height > 0) {
        const size_t bytes = static_cast<size_t>(frame.width) * static_cast<size_t>(frame.height) * 4;
        cached.rgba.resize(bytes);
        std::memcpy(cached.rgba.data(), frame.data, bytes);
        cached.frame.data = cached.rgba.data();
    }
    gop_cache_.push_back(std::move(cached));
    if (!gop_cache_.back().rgba.empty()) {
        gop_cache_.back().frame.data = gop_cache_.back().rgba.data();
    }
}

bool VideoDecoder::cacheCurrentGopUntil(const FrameLocation &target, VideoFrame &out) {
    const int max_decodes = std::max(target.gop_frame_count + 16, target.frame_in_gop + 16);
    for (int decoded = 0; decoded < max_decodes; ++decoded) {
        active_buf_ = 1 - active_buf_;
        VideoFrame frame;
        if (!decodeNextFrame(frame)) {
            return restoreCachedFrame(target.sample_index, out);
        }

        FrameLocation loc;
        if (has_frame_index_ && moov_.queryFrame(frame.pts_ms, loc)) {
            fillFrameLocation(frame, loc);
        } else {
            frame.sample_index = ++decode_cursor_sample_;
            frame.gop_index = target.gop_index;
            frame.frame_in_gop = frame.sample_index;
        }

        decode_cursor_sample_ = std::max(decode_cursor_sample_, frame.sample_index);
        decode_cursor_gop_ = frame.gop_index;
        if (frame.gop_index == target.gop_index) {
            cacheDecodedFrame(frame);
        }

        if (restoreCachedFrame(target.sample_index, out)) {
            frame.releaseNative();
            return true;
        }
        frame.releaseNative();
    }
    return false;
}

bool VideoDecoder::seekTo(TimeMs time_ms) {
    int64_t target_pts = msToPts(time_ms);
    if (av_seek_frame(format_ctx_, video_stream_idx_, target_pts, AVSEEK_FLAG_BACKWARD) < 0)
        return false;
    avcodec_flush_buffers(codec_ctx_);
    clearGopCache();
    decode_cursor_sample_ = -1;
    decode_cursor_gop_ = -1;
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

    clearGopCache();
    decode_cursor_sample_ = -1;
    decode_cursor_gop_ = -1;
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
    has_frame_index_ = false;
}

bool VideoDecoder::decodeFrameAt(TimeMs time_ms, VideoFrame &out) {
    if (!format_ctx_ || video_stream_idx_ < 0)
        return false;

    if (duration_ms_ == 0)
        return false;

    if (time_ms >= duration_ms_)
        time_ms = duration_ms_ - 1;

    if (has_frame_index_) {
        FrameLocation target;
        if (!moov_.queryFrame(time_ms, target)) {
            return false;
        }

        if (restoreCachedFrame(target.sample_index, out)) {
            return true;
        }

        const bool can_continue = decode_cursor_gop_ == target.gop_index &&
            decode_cursor_sample_ >= 0 &&
            decode_cursor_sample_ <= target.sample_index;
        if (!can_continue) {
            if (!seekTo(target.gop_pts_ms)) {
                return false;
            }
            cached_gop_index_ = target.gop_index;
            decode_cursor_sample_ = target.sample_index - target.frame_in_gop - 1;
            decode_cursor_gop_ = target.gop_index;
        }
        return cacheCurrentGopUntil(target, out);
    }

    if (!seekTo(time_ms)) {
        return false;
    }
    while (true) {
        active_buf_ = 1 - active_buf_;
        if (!decodeNextFrame(out)) {
            return false;
        }
        if (out.pts_ms >= time_ms) {
            return out.valid;
        }
    }
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
