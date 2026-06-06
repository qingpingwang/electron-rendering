#include "video_encoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#include <iostream>

namespace vp {

VideoEncoder::VideoEncoder() {
}

VideoEncoder::~VideoEncoder() {
    close();
}

bool VideoEncoder::open(const std::string &output_file, const EncoderConfig &config) {
    width_ = config.width;
    height_ = config.height;

    // 1. 分配输出上下文
    int ret = avformat_alloc_output_context2(&fmt_ctx_, nullptr, nullptr, output_file.c_str());
    if (!fmt_ctx_) {
        std::cerr << "Failed to create output context" << std::endl;
        return false;
    }

    // 2. 查找编码器
    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) {
        std::cerr << "H264 encoder not found" << std::endl;
        return false;
    }

    // 3. 创建视频流
    stream_ = avformat_new_stream(fmt_ctx_, nullptr);
    if (!stream_) {
        std::cerr << "Failed to create stream" << std::endl;
        return false;
    }
    stream_->id = fmt_ctx_->nb_streams - 1;

    // 4. 创建编码器上下文
    codec_ctx_ = avcodec_alloc_context3(codec);
    if (!codec_ctx_) {
        std::cerr << "Failed to allocate codec context" << std::endl;
        return false;
    }

    // 5. 设置编码参数
    codec_ctx_->codec_id = AV_CODEC_ID_H264;
    codec_ctx_->bit_rate = config.bit_rate;
    codec_ctx_->width = width_;
    codec_ctx_->height = height_;
    int fps = (config.fps > 0) ? config.fps : 30;
    codec_ctx_->time_base = AVRational{1, 1000};           // 毫秒级时间基
    codec_ctx_->framerate = AVRational{fps, 1};
    codec_ctx_->gop_size = 12;
    codec_ctx_->max_b_frames = 2;
    codec_ctx_->pix_fmt = AV_PIX_FMT_YUV420P;

    if (fmt_ctx_->oformat->flags & AVFMT_GLOBALHEADER) {
        codec_ctx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    // H264 编码器选项
    av_opt_set(codec_ctx_->priv_data, "preset", config.preset.c_str(), 0);
    av_opt_set(codec_ctx_->priv_data, "crf", std::to_string(config.crf).c_str(), 0);

    // 6. 打开编码器
    ret = avcodec_open2(codec_ctx_, codec, nullptr);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
        std::cerr << "Failed to open codec: " << errbuf << std::endl;
        return false;
    }

    // 7. 拷贝编码参数到流
    ret = avcodec_parameters_from_context(stream_->codecpar, codec_ctx_);
    if (ret < 0) {
        std::cerr << "Failed to copy codec parameters" << std::endl;
        return false;
    }

    // 8. 打开输出文件
    if (!(fmt_ctx_->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&fmt_ctx_->pb, output_file.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            std::cerr << "Failed to open output file" << std::endl;
            return false;
        }
    }

    // 9. 写文件头（header 写完后 stream_->time_base 由 muxer 最终确定）
    ret = avformat_write_header(fmt_ctx_, nullptr);
    if (ret < 0) {
        std::cerr << "Failed to write header" << std::endl;
        return false;
    }
    stream_->avg_frame_rate = AVRational{fps, 1};
    stream_->r_frame_rate   = AVRational{fps, 1};

    // 10. 分配帧
    frame_ = av_frame_alloc();
    if (!frame_) {
        std::cerr << "Failed to allocate frame" << std::endl;
        return false;
    }

    frame_->format = codec_ctx_->pix_fmt;
    frame_->width = width_;
    frame_->height = height_;

    ret = av_frame_get_buffer(frame_, 0);
    if (ret < 0) {
        std::cerr << "Failed to allocate frame buffer" << std::endl;
        return false;
    }

    // 11. RGBA → YUV 转换器
    sws_ctx_ = sws_getContext(width_, height_, AV_PIX_FMT_RGBA,
                               width_, height_, AV_PIX_FMT_YUV420P,
                               SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws_ctx_) {
        std::cerr << "Failed to create sws context" << std::endl;
        return false;
    }

    is_open_ = true;
    frame_count_ = 0;

    return true;
}

bool VideoEncoder::encodeFrame(const uint8_t *rgba_data, TimeMs pts_ms) {
    if (!is_open_ || !codec_ctx_ || !frame_ || !sws_ctx_) {
        return false;
    }

    // RGBA → YUV 转换
    const uint8_t *src_data[1] = {rgba_data};
    int src_linesize[1] = {width_ * 4};

    sws_scale(sws_ctx_, src_data, src_linesize, 0, height_,
              frame_->data, frame_->linesize);

    // time_base = 1/1000，直接使用毫秒作为 pts
    frame_->pts = pts_ms;
    frame_count_++;

    // 发送帧到编码器
    int ret = avcodec_send_frame(codec_ctx_, frame_);
    if (ret < 0) {
        std::cerr << "Failed to send frame to encoder" << std::endl;
        return false;
    }

    // 接收编码后的包
    while (ret >= 0) {
        AVPacket *pkt = av_packet_alloc();
        ret = avcodec_receive_packet(codec_ctx_, pkt);

        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            av_packet_free(&pkt);
            break;
        } else if (ret < 0) {
            av_packet_free(&pkt);
            std::cerr << "Failed to receive packet" << std::endl;
            return false;
        }

        // 写入文件
        bool write_ok = writePacket(pkt);
        av_packet_free(&pkt);

        if (!write_ok) {
            return false;
        }
    }

    return true;
}

void VideoEncoder::close() {
    if (!is_open_) {
        return;
    }

    // Flush 编码器
    if (codec_ctx_) {
        avcodec_send_frame(codec_ctx_, nullptr);

        while (true) {
            AVPacket *pkt = av_packet_alloc();
            int ret = avcodec_receive_packet(codec_ctx_, pkt);

            if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN)) {
                av_packet_free(&pkt);
                break;
            }

            if (ret >= 0) {
                writePacket(pkt);
            }

            av_packet_free(&pkt);
        }
    }

    // 写文件尾
    if (fmt_ctx_) {
        av_write_trailer(fmt_ctx_);
    }

    // 清理资源
    if (frame_) {
        av_frame_free(&frame_);
    }

    if (codec_ctx_) {
        avcodec_free_context(&codec_ctx_);
    }

    if (sws_ctx_) {
        sws_freeContext(sws_ctx_);
    }

    if (fmt_ctx_ && !(fmt_ctx_->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&fmt_ctx_->pb);
    }

    if (fmt_ctx_) {
        avformat_free_context(fmt_ctx_);
        fmt_ctx_ = nullptr;
    }

    is_open_ = false;
}

int VideoEncoder::getWidth() const {
    return width_;
}

int VideoEncoder::getHeight() const {
    return height_;
}

int64_t VideoEncoder::getEncodedFrames() const {
    return frame_count_;
}

bool VideoEncoder::isOpen() const {
    return is_open_;
}

bool VideoEncoder::writePacket(AVPacket *pkt) {
    if (!fmt_ctx_ || !stream_) {
        return false;
    }

    pkt->stream_index = stream_->index;
    av_packet_rescale_ts(pkt, codec_ctx_->time_base, stream_->time_base);

    int ret = av_interleaved_write_frame(fmt_ctx_, pkt);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
        std::cerr << "Failed to write packet: " << errbuf << std::endl;
        return false;
    }

    return true;
}

} // namespace vp
