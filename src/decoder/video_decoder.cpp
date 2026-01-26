#include "video_decoder.h"

namespace vp
{

    VideoDecoder::VideoDecoder()
    {
        packet_ = av_packet_alloc();
        frame_ = av_frame_alloc();
        frame_rgba_ = av_frame_alloc();
    }

    VideoDecoder::~VideoDecoder()
    {
        close();
        if (packet_)
            av_packet_free(&packet_);
        if (frame_)
            av_frame_free(&frame_);
        if (frame_rgba_)
            av_frame_free(&frame_rgba_);
    }

    bool VideoDecoder::open(const std::string &path)
    {
        close();

        if (avformat_open_input(&format_ctx_, path.c_str(), nullptr, nullptr) < 0)
        {
            return false;
        }

        if (avformat_find_stream_info(format_ctx_, nullptr) < 0)
        {
            close();
            return false;
        }

        // 查找视频流
        video_stream_idx_ = -1;
        for (unsigned int i = 0; i < format_ctx_->nb_streams; i++)
        {
            if (format_ctx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
            {
                video_stream_idx_ = i;
                break;
            }
        }
        if (video_stream_idx_ < 0)
        {
            close();
            return false;
        }

        AVStream *stream = format_ctx_->streams[video_stream_idx_];
        AVCodecParameters *codecpar = stream->codecpar;

        const AVCodec *codec = avcodec_find_decoder(codecpar->codec_id);
        if (!codec)
        {
            close();
            return false;
        }

        codec_ctx_ = avcodec_alloc_context3(codec);
        if (!codec_ctx_ || avcodec_parameters_to_context(codec_ctx_, codecpar) < 0)
        {
            close();
            return false;
        }

        if (avcodec_open2(codec_ctx_, codec, nullptr) < 0)
        {
            close();
            return false;
        }

        width_ = codec_ctx_->width;
        height_ = codec_ctx_->height;
        time_base_ = stream->time_base;

        if (format_ctx_->duration != AV_NOPTS_VALUE)
        {
            duration_ms_ = format_ctx_->duration / (AV_TIME_BASE / 1000);
        }
        else if (stream->duration != AV_NOPTS_VALUE)
        {
            duration_ms_ = ptsToMs(stream->duration);
        }

        if (stream->avg_frame_rate.den > 0)
        {
            frame_rate_ = av_q2d(stream->avg_frame_rate);
        }
        else if (stream->r_frame_rate.den > 0)
        {
            frame_rate_ = av_q2d(stream->r_frame_rate);
        }
        else
        {
            frame_rate_ = 25.0;
        }

        // 创建格式转换上下文
        sws_ctx_ = sws_getContext(
            width_, height_, codec_ctx_->pix_fmt,
            width_, height_, AV_PIX_FMT_RGBA,
            SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!sws_ctx_)
        {
            close();
            return false;
        }

        // 分配 RGBA 缓冲区
        int buffer_size = av_image_get_buffer_size(AV_PIX_FMT_RGBA, width_, height_, 1);
        rgba_buffer_ = (uint8_t *)av_malloc(buffer_size);

        av_image_fill_arrays(
            frame_rgba_->data, frame_rgba_->linesize,
            rgba_buffer_, AV_PIX_FMT_RGBA,
            width_, height_, 1);

        last_decoded_ms_ = -1;
        return true;
    }

    void VideoDecoder::close()
    {
        if (sws_ctx_)
        {
            sws_freeContext(sws_ctx_);
            sws_ctx_ = nullptr;
        }
        if (codec_ctx_)
        {
            avcodec_free_context(&codec_ctx_);
        }
        if (format_ctx_)
        {
            avformat_close_input(&format_ctx_);
        }
        if (rgba_buffer_)
        {
            av_free(rgba_buffer_);
            rgba_buffer_ = nullptr;
        }

        video_stream_idx_ = -1;
        width_ = height_ = 0;
        duration_ms_ = 0;
        frame_rate_ = 0.0;
        last_decoded_ms_ = -1;
    }

    bool VideoDecoder::decodeFrameAt(int64_t time_ms, VideoFrame &out)
    {
        if (!format_ctx_ || video_stream_idx_ < 0)
        {
            return false;
        }

        if (time_ms < 0)
            time_ms = 0;
        if (time_ms >= duration_ms_)
            time_ms = duration_ms_ - 1;

        // 计算帧间隔
        int64_t frame_interval = (frame_rate_ > 0) ? (int64_t)(1000.0 / frame_rate_) : 40;

        // 判断是否需要 seek
        bool need_seek = false;
        if (last_decoded_ms_ < 0)
        {
            need_seek = true;
        }
        else if (time_ms < last_decoded_ms_)
        {
            need_seek = true;
        }
        else if (time_ms - last_decoded_ms_ > frame_interval * 10)
        {
            need_seek = true;
        }

        if (need_seek)
        {
            int64_t target_pts = msToPts(time_ms);
            if (av_seek_frame(format_ctx_, video_stream_idx_, target_pts, AVSEEK_FLAG_BACKWARD) < 0)
            {
                return false;
            }
            avcodec_flush_buffers(codec_ctx_);

            // 解码直到目标时间
            while (decodeNextFrame(out))
            {
                if (out.pts_ms >= time_ms)
                {
                    last_decoded_ms_ = out.pts_ms;
                    return true;
                }
            }
            return false;
        }
        else
        {
            // 顺序解码
            while (last_decoded_ms_ < time_ms)
            {
                if (!decodeNextFrame(out))
                {
                    return false;
                }
                last_decoded_ms_ = out.pts_ms;
            }
            return out.valid;
        }
    }

    bool VideoDecoder::decodeNextFrame(VideoFrame &out)
    {
        while (true)
        {
            int ret = av_read_frame(format_ctx_, packet_);
            if (ret < 0)
            {
                out.valid = false;
                return false;
            }

            if (packet_->stream_index != video_stream_idx_)
            {
                av_packet_unref(packet_);
                continue;
            }

            ret = avcodec_send_packet(codec_ctx_, packet_);
            av_packet_unref(packet_);

            if (ret < 0)
                continue;

            ret = avcodec_receive_frame(codec_ctx_, frame_);
            if (ret == AVERROR(EAGAIN))
                continue;
            if (ret < 0)
            {
                out.valid = false;
                return false;
            }

            convertToRGBA(frame_, out);
            av_frame_unref(frame_);
            return true;
        }
    }

    void VideoDecoder::convertToRGBA(AVFrame *frame, VideoFrame &out)
    {
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

    int64_t VideoDecoder::ptsToMs(int64_t pts) const
    {
        if (pts == AV_NOPTS_VALUE)
            return 0;
        return av_rescale_q(pts, time_base_, {1, 1000});
    }

    int64_t VideoDecoder::msToPts(int64_t ms) const
    {
        return av_rescale_q(ms, {1, 1000}, time_base_);
    }

} // namespace vp
