#include "video_layer.h"
#include "../render/gl_renderer.h"

namespace vp {

VideoLayer::VideoLayer(const std::string& name) : Layer(name) {
    decoder_ = std::make_unique<VideoDecoder>();
}

VideoLayer::~VideoLayer() {
    unload();
}

bool VideoLayer::load(const std::string& path) {
    unload();
    
    if (!decoder_->open(path)) {
        return false;
    }
    
    width_ = decoder_->getWidth();
    height_ = decoder_->getHeight();
    duration_ms_ = decoder_->getDurationMs();
    
    // 解码第一帧
    decoder_->decodeFrameAt(0, current_frame_);
    
    return true;
}

void VideoLayer::unload() {
    if (decoder_) {
        decoder_->close();
    }
    width_ = height_ = 0;
    duration_ms_ = 0;
    current_frame_ = VideoFrame{};
}

void VideoLayer::setCurrentTime(int64_t time_ms) {
    if (!decoder_ || !decoder_->isOpen()) return;
    decoder_->decodeFrameAt(time_ms, current_frame_);
}

void VideoLayer::draw(GLRenderer& renderer) {
    if (!current_frame_.valid || !current_frame_.data) return;
    renderer.uploadTexture(current_frame_.data, current_frame_.width, current_frame_.height);
    renderer.render();
}

bool VideoLayer::decodeFrame(int64_t time_ms, VideoFrame& out_frame) {
    if (!decoder_ || !decoder_->isOpen()) return false;
    return decoder_->decodeFrameAt(time_ms, out_frame);
}

void VideoLayer::drawWithFrame(GLRenderer& renderer, const VideoFrame& frame) {
    if (!frame.valid || !frame.data) return;
    renderer.uploadTexture(frame.data, frame.width, frame.height);
    renderer.render();
}

void VideoLayer::drawWithData(GLRenderer& renderer, const uint8_t* data, int width, int height) {
    if (!data || width <= 0 || height <= 0) return;
    renderer.uploadTexture(data, width, height);
    renderer.render();
}

double VideoLayer::getFrameRate() const {
    return decoder_ ? decoder_->getFrameRate() : 0.0;
}

bool VideoLayer::isLoaded() const {
    return decoder_ && decoder_->isOpen();
}

} // namespace vp
