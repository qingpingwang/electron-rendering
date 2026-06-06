#include "texture_player.h"
#include "../core/root_node.h"
#include "../codec/video_decoder.h"
#include "../gl/functions.h"
#include "include/codec/SkCodec.h"
#include "include/core/SkBitmap.h"
#include "include/core/SkData.h"
#include <cstring>
#include <filesystem>
namespace fs = std::filesystem;

namespace vp {

// ====================  TexturePlayer 基类 ====================

TexturePlayer::TexturePlayer(RootNode *root) :
    root_(root) {
}

TexturePlayer::~TexturePlayer() {
}

bool TexturePlayer::load(const nlohmann::json &config, const std::string &base_path) {
    clearError();
    name_ = config.value("name", "");
    repeat_mode_ = config.value("repeatMode", 0);

    const auto &pipe_array = config.value("pipe", std::vector<int>{});
    const auto &pass_array = config.value("renderPassIndex", std::vector<int>{});
    if (pipe_array.size() != pass_array.size()) {
        setError("pipe and renderPassIndex must have the same size");
        return false;
    }
    render_pass_indices_.reserve(pipe_array.size());
    for (size_t i = 0; i < pipe_array.size(); i++) {
        render_pass_indices_.emplace_back(std::make_tuple(pipe_array[i], pass_array[i]));
    }

    std::string url = config.value("url", "");
    if (url.empty()) {
        setError("url is required");
        return false;
    }

    // 拼接完整路径
    file_path_ = base_path + "/" + url;

    // 检查文件是否存在
    if (!fs::exists(file_path_)) {
        setError("file not found: " + file_path_);
        return false;
    }

    return true;
}

// ====================  ImageTexture ====================

ImageTexture::ImageTexture(RootNode *root) :
    TexturePlayer(root) {
}

ImageTexture::~ImageTexture() {
    // 释放纹理
    gl::destroyTexture(texture_);
    frame_data_.clear();
}

bool ImageTexture::load(const nlohmann::json &config, const std::string &base_path) {
    if (!TexturePlayer::load(config, base_path)) {
        return false;
    }

    // 检测是否为 GIF
    bool is_gif = (file_path_.find(".gif") != std::string::npos);

    if (is_gif) {
        return loadGif();
    } else {
        return loadStaticImage();
    }
}

bool ImageTexture::loadStaticImage() {
    auto sk_data = SkData::MakeFromFileName(file_path_.c_str());
    if (!sk_data) {
        setError("load static image failed: " + file_path_);
        return false;
    }

    auto codec = SkCodec::MakeFromData(sk_data);
    if (!codec) {
        setError("unsupported image format: " + file_path_);
        return false;
    }

    SkImageInfo info = codec->getInfo()
                           .makeColorType(kRGBA_8888_SkColorType)
                           .makeAlphaType(kUnpremul_SkAlphaType);
    SkBitmap bitmap;
    bitmap.allocPixels(info);
    if (!bitmap.getPixels()) {
        setError("allocate bitmap failed: " + file_path_);
        return false;
    }

    if (codec->getPixels(info, bitmap.getPixels(), bitmap.rowBytes()) != SkCodec::kSuccess) {
        setError("decode image failed: " + file_path_);
        return false;
    }

    width_ = info.width();
    height_ = info.height();
    texture_ = gl::createTexture(width_, height_, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
    gl::updateTexture(texture_, static_cast<const uint8_t *>(bitmap.getPixels()), width_, height_);

    fps_ = 0.0f;
    return true;
}

bool ImageTexture::loadGif() {
    auto sk_data = SkData::MakeFromFileName(file_path_.c_str());
    if (!sk_data) {
        setError("open gif failed: " + file_path_);
        return false;
    }

    auto codec = SkCodec::MakeFromData(sk_data);
    if (!codec) {
        setError("unsupported gif format: " + file_path_);
        return false;
    }

    int frame_count = codec->getFrameCount();
    if (frame_count == 0) {
        setError("gif has no frames: " + file_path_);
        return false;
    }

    SkImageInfo info = codec->getInfo()
                           .makeColorType(kRGBA_8888_SkColorType)
                           .makeAlphaType(kUnpremul_SkAlphaType);
    width_ = info.width();
    height_ = info.height();

    // 获取帧信息（用于计算 FPS）
    std::vector<SkCodec::FrameInfo> frame_infos = codec->getFrameInfo();

    if (frame_count > 1) {
        int total_ms = 0;
        for (const auto &fi : frame_infos)
            total_ms += fi.fDuration;
        float avg_ms = static_cast<float>(total_ms) / frame_count;
        fps_ = avg_ms > 0 ? 1000.0f / avg_ms : 10.0f;
    }

    // 解码全部帧（逐帧合成）
    size_t frame_size = static_cast<size_t>(width_) * height_ * 4;
    frame_data_.reserve(frame_count);

    SkBitmap canvas_bm;
    canvas_bm.allocPixels(info);

    for (int i = 0; i < frame_count; ++i) {
        SkCodec::Options opts;
        opts.fFrameIndex = i;
        // 需要的先验帧（用于帧间 delta 合成）
        opts.fPriorFrame = (i > 0 && frame_infos[i].fRequiredFrame != SkCodec::kNoFrame)
                               ? (i - 1)
                               : SkCodec::kNoFrame;

        SkCodec::Result res = codec->getPixels(info, canvas_bm.getPixels(),
                                               canvas_bm.rowBytes(), &opts);
        if (res != SkCodec::kSuccess && res != SkCodec::kIncompleteInput)
            break;

        std::vector<uint8_t> frame_buf(frame_size);
        memcpy(frame_buf.data(), canvas_bm.getPixels(), frame_size);
        frame_data_.emplace_back(std::move(frame_buf));
    }

    if (frame_data_.empty()) {
        setError("decode gif frames failed: " + file_path_);
        return false;
    }

    texture_ = gl::createTexture(width_, height_, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
    gl::updateTexture(texture_, frame_data_[0].data(), width_, height_);
    return true;
}

gl::Texture ImageTexture::play(TimeMs time_ms) {
    // 卫语句：纹理未创建
    if (texture_.id == 0) {
        setError("texture not created");
        return gl::Texture{};
    }

    // 卫语句：静态图，直接返回（无需更新）
    if (frame_data_.empty() || fps_ <= 0.0f) {
        setError("static image or gif is empty");
        return texture_;
    }

    // GIF 动画：计算当前帧索引
    int64_t frame_index = static_cast<int64_t>(time_ms * fps_ / 1000.0);

    switch (repeat_mode_) {
    case 0: // 停止在最后一帧
        frame_index = std::min(frame_index, static_cast<int64_t>(frame_data_.size() - 1));
        break;
    case 2: // 循环
        frame_index = frame_index % frame_data_.size();
        break;
    default:
        frame_index = frame_index % frame_data_.size();
        break;
    }

    // 卫语句：帧未变化，无需更新
    if (frame_index == last_frame_index_) {
        return texture_;
    }

    last_frame_index_ = frame_index;

    // 更新纹理为当前帧
    gl::updateTexture(texture_, frame_data_[frame_index].data(), width_, height_);

    return texture_;
}

// ====================  VideoTexture ====================

VideoTexture::VideoTexture(RootNode *root) :
    TexturePlayer(root) {
}

VideoTexture::~VideoTexture() {
    // 释放纹理
    gl::destroyTexture(texture_);
}

bool VideoTexture::load(const nlohmann::json &config, const std::string &base_path) {
    if (!TexturePlayer::load(config, base_path)) {
        return false;
    }

    // 创建解码器
    decoder_ = std::make_unique<VideoDecoder>();
    if (!decoder_->open(file_path_)) {
        setError("open video file failed: " + file_path_);
        return false;
    }

    width_ = decoder_->getWidth();
    height_ = decoder_->getHeight();
    duration_ms_ = decoder_->getDurationMs();
    fps_ = decoder_->getFrameRate();

    // 创建纹理（预分配，不上传数据）
    texture_ = gl::createTexture(width_, height_, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);

    return true;
}

gl::Texture VideoTexture::play(TimeMs time_ms) {
    if (!decoder_ || texture_.id == 0) {
        setError("decoder or texture not created");
        return gl::Texture{};
    }

    // 处理循环模式
    TimeMs sampling_time = time_ms;
    switch (repeat_mode_) {
    case 0: // 停止在最后一帧
        sampling_time = std::min(sampling_time, duration_ms_);
        break;
    case 2: // 循环
        if (duration_ms_ > 0) {
            sampling_time = sampling_time % (duration_ms_ + 1);
        }
        break;
    default:
        if (duration_ms_ > 0) {
            sampling_time = sampling_time % (duration_ms_ + 1);
        }
        break;
    }

    // 卫语句：时间未变化，返回缓存纹理
    if (sampling_time == last_time_ms_) {
        return texture_;
    }

    last_time_ms_ = sampling_time;

    // 解码帧
    VideoFrame frame;
    if (!decoder_->decodeFrameAt(sampling_time, frame) || !frame.valid) {
        setError("decode frame failed: " + file_path_);
        return texture_;
    }

    if (frame.hw)
        gl::updateTextureFromNativeBuffer(texture_, frame.native_buf);
    else
        gl::updateTexture(texture_, frame.data, frame.width, frame.height);
    frame.releaseNative();

    return texture_;
}

nlohmann::json TexturePlayer::dump() const {
    return nlohmann::json::object();
}

} // namespace vp
