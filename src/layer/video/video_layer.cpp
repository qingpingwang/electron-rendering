#include "video_layer.h"
#include "../../core/root_node.h"
#include <nlohmann/json.hpp>
#include "../../gl/functions.h"

using json = nlohmann::json;

namespace vp {

VideoLayer::VideoLayer(RootNode *root) :
    Layer(root) {
    decoder_ = std::make_unique<VideoDecoder>();
}

VideoLayer::~VideoLayer() {
    current_frame_.releaseNative();
    if (decoder_)
        decoder_->close();
    gl::destroyTexture(texture_);
    gl::destroyQuadMesh(quad_);
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
    static_cast<VideoMaterial *>(material_)->updateWHAndDuration(video_width_, video_height_, video_duration_ms_);

    // 长边适配
    const auto &canvas = root_->getCanvas();
    quad_ = createFitQuad(video_width_, video_height_, canvas.width, canvas.height);
    if (!quad_.isValid()) {
        setError("create quad mesh failed");
        return false;
    }

    // source_timerange 由基类解析，duration=0 表示使用完整源视频时长
    if (!config.contains("source_timerange")) {
        setError("source_timerange is required");
        return false;
    }
    if (source_range_.duration == 0)
        source_range_.duration = video_duration_ms_;

    // 创建纹理
    texture_ = gl::createTexture(video_width_, video_height_);
    if (!texture_.isValid()) {
        setError("create texture failed");
        return false;
    }

    return true;
}

json VideoLayer::dump() const {
    json j = Layer::dump();
    j["clip"] = Layer::dumpClip();
    // 视频相关
    j["muted"] = isMuted();
    j["volume"] = getVolume();
    j["visible"] = isVisible();
    j["source_timerange"] = Layer::dumpSourceRange();
    return j;
}

void VideoLayer::prepare() {
    if (decoder_ && decoder_->isOpen()) {
        TimeMs frame_time = calculateFrameTime(getStartTime());
        decoder_->decodeFrameAt(frame_time, current_frame_);
    }
}

bool VideoLayer::renderContent(const gl::FBO &fbo, TimeMs time_ms) {
    TimeMs frame_time = calculateFrameTime(time_ms);
    if (frame_time == kInvalidTime)
        return true;

    // 解码当前时间的帧（decoder内部会处理帧复用）
    if (!decoder_->decodeFrameAt(frame_time, current_frame_))
        return true;

    if (!current_frame_.valid)
        return true;

    if (current_frame_.pts_ms != uploaded_pts_) {
        bool ok = current_frame_.hw ? gl::updateTextureFromNativeBuffer(texture_, current_frame_.native_buf) : gl::updateTexture(texture_, current_frame_.data, current_frame_.width, current_frame_.height);
        if (!ok) return false;
        uploaded_pts_ = current_frame_.pts_ms;
    }

    // 使用通用函数绘制到目标 FBO
    gl::drawTextureQuad(fbo, texture_, root_->getShader(), 0, "uTex", &quad_);
    return true;
}

double VideoLayer::getFrameRate() const {
    return decoder_ ? decoder_->getFrameRate() : 0.0;
}

bool VideoLayer::isLoaded() const {
    return decoder_ && decoder_->isOpen();
}

TimeMs VideoLayer::calculateFrameTime(TimeMs time_ms) const {
    if (time_ms < target_range_.start)
        return kInvalidTime;

    TimeMs layer_time = time_ms - target_range_.start;
    TimeMs layer_duration = target_range_.duration;
    if (layer_time >= layer_duration)
        return kInvalidTime;

    double progress = static_cast<double>(layer_time) / static_cast<double>(layer_duration);
    return source_range_.start + static_cast<TimeMs>(progress * source_range_.duration);
}

} // namespace vp
