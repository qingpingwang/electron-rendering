#include "texture_player.h"
#include "../core/root_node.h"
#include "../codec/video_decoder.h"
#include "../gl/functions.h"
#include "../third_party/stb_image/stb_image.h"
#include <cstring>
#include <filesystem>
#include <fstream>

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
    // 使用 stb_image 加载静态图片
    int width, height, channels;
    stbi_set_flip_vertically_on_load(false);
    unsigned char *data = stbi_load(file_path_.c_str(), &width, &height, &channels, STBI_rgb_alpha);

    if (!data) {
        setError("load static image failed: " + file_path_);
        return false;
    }

    width_ = width;
    height_ = height;

    // 创建纹理并上传数据
    texture_ = gl::createTexture(width, height, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
    gl::updateTexture(texture_, data, width, height);

    // 释放 CPU 数据（静态图不需要保留）
    stbi_image_free(data);

    fps_ = 0.0f; // 静态图，无动画
    // frame_data_ 保持为空

    return true;
}

bool ImageTexture::loadGif() {
    // 读取 GIF 文件到内存
    std::ifstream file(file_path_, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        setError("open gif file failed: " + file_path_);
        return false;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<unsigned char> buffer(size);
    if (!file.read(reinterpret_cast<char *>(buffer.data()), size)) {
        setError("read gif file failed: " + file_path_);
        return false;
    }

    // 使用 stb_image 解码 GIF
    int *delays = nullptr;
    int width, height, frames, channels;

    unsigned char *data = stbi_load_gif_from_memory(
        buffer.data(), static_cast<int>(buffer.size()),
        &delays, &width, &height, &frames, &channels, STBI_rgb_alpha);

    if (!data || frames == 0) {
        if (delays)
            free(delays);
        setError("load gif failed: " + file_path_);
        return false;
    }

    width_ = width;
    height_ = height;

    // 计算平均 FPS（从延迟）
    if (frames > 1 && delays) {
        int total_delay_ms = 0;
        for (int i = 0; i < frames; ++i) {
            total_delay_ms += delays[i];
        }
        float avg_delay_ms = static_cast<float>(total_delay_ms) / frames;
        fps_ = avg_delay_ms > 0 ? 1000.0f / avg_delay_ms : 10.0f;
    } else {
        fps_ = 0.0f;
    }

    // 保存所有帧数据到 CPU 内存
    size_t frame_size = width * height * 4; // RGBA
    frame_data_.reserve(frames);

    for (int i = 0; i < frames; ++i) {
        std::vector<uint8_t> frame_buffer(frame_size);
        memcpy(frame_buffer.data(), data + i * frame_size, frame_size);
        frame_data_.emplace_back(std::move(frame_buffer));
    }

    // 创建单张纹理（复用）
    texture_ = gl::createTexture(width, height, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);

    // 上传第一帧
    if (!frame_data_.empty()) {
        gl::updateTexture(texture_, frame_data_[0].data(), width, height);
    }

    // 释放 stb_image 数据
    stbi_image_free(data);
    if (delays)
        free(delays);

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

} // namespace vp
