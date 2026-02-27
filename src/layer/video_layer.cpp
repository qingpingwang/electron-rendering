#include "video_layer.h"
#include "../core/root_node.h"
#include <nlohmann/json.hpp>
#include "../gl/functions.h"
#include "../gl/shader.h"

using json = nlohmann::json;

namespace vp {

VideoLayer::VideoLayer(RootNode *root) :
    Layer(root) {
    decoder_ = std::make_unique<VideoDecoder>();
}

VideoLayer::~VideoLayer() {
    if (decoder_)
        decoder_->close();
    gl::destroyTexture(texture_);
}

bool VideoLayer::load(const json &config, const std::string &base_path) {
    if (!root_) {
        setError("root node is null");
        return false;
    }

    // 调用基类解析通用属性（包括获取 material_ 指针）
    if (!Layer::load(config, base_path))
        return false;

    // 检查素材指针
    if (!material_) {
        setError("material is null");
        return false;
    }

    // 获取视频路径
    const std::string &video_path = material_->getPath();
    if (video_path.empty()) {
        setError("video path is empty");
        return false;
    }

    // 打开视频文件
    if (!decoder_->open(video_path)) {
        setError("open video file failed: " + video_path);
        return false;
    }

    video_width_ = decoder_->getWidth();
    video_height_ = decoder_->getHeight();
    video_duration_ms_ = decoder_->getDurationMs();

    // 解析源视频时间范围，必须有
    if (!config.contains("source_timerange")) {
        setError("source_timerange is required");
        return false;
    }
    source_start_ms_ = config["source_timerange"].value("start", 0);
    source_duration_ms_ = config["source_timerange"].value("duration", video_duration_ms_);

    // 创建纹理
    texture_ = gl::createTexture(video_width_, video_height_);
    if (!texture_.isValid()) {
        setError("create texture failed");
        return false;
    }

    return true;
}

bool VideoLayer::renderContent(const gl::FBO &fbo, TimeMs time_ms) {
    TimeMs frame_time = calculateFrameTime(time_ms);
    if (frame_time == kInvalidTime)
        return true;

    // 解码当前时间的帧（decoder内部会处理帧复用）
    if (!decoder_->decodeFrameAt(frame_time, current_frame_))
        return true;

    // 检查当前帧是否有效
    if (!current_frame_.valid || !current_frame_.data)
        return true;

    // 更新纹理（仅在帧变化时）
    if (current_frame_.pts_ms != uploaded_pts_) {
        if (!gl::updateTexture(texture_, current_frame_.data, current_frame_.width, current_frame_.height))
            return false;
        uploaded_pts_ = current_frame_.pts_ms;
    }

    // 使用通用函数绘制到目标 FBO
    gl::drawTextureQuad(fbo, texture_, root_->getShader(), 0, "uTex", root_->getQuad());
    return true;
}

double VideoLayer::getFrameRate() const {
    return decoder_ ? decoder_->getFrameRate() : 0.0;
}

bool VideoLayer::isLoaded() const {
    return decoder_ && decoder_->isOpen();
}

TimeMs VideoLayer::calculateFrameTime(TimeMs time_ms) const {
    if (time_ms < start_time_ms_)
        return kInvalidTime;

    TimeMs layer_time = time_ms - start_time_ms_;
    TimeMs layer_duration = end_time_ms_ - start_time_ms_;
    if (layer_time >= layer_duration)
        return kInvalidTime;

    double progress = static_cast<double>(layer_time) / static_cast<double>(layer_duration);
    return source_start_ms_ + static_cast<TimeMs>(progress * source_duration_ms_);
}

} // namespace vp
