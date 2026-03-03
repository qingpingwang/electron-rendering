#include "group_layer.h"
#include "../base/layer.h"
#include "../video/video_layer.h"
#include "../text/text_layer.h"
#include "../audio/audio_layer.h"
#include "../material/effect.h"
#include "../../core/root_node.h"
#include "../../gl/functions.h"

namespace vp {

GroupLayer::GroupLayer(RootNode *root) : root_(root) {}

void GroupLayer::addLayer(std::unique_ptr<Layer> layer) {
    layers_.emplace_back(std::move(layer));
}

std::string GroupLayer::load(const nlohmann::json &track_config) {
    id_ = track_config.value("id", "");
    type_ = track_config.value("type", "");

    if (!track_config.contains("segments"))
        return "segments is required";

    using Factory = std::unique_ptr<Layer> (*)(RootNode *);
    static const struct { const char *type; Factory fn; } factories[] = {
        {"video", [](RootNode *r) -> std::unique_ptr<Layer> { return std::make_unique<VideoLayer>(r); }},
        {"text",  [](RootNode *r) -> std::unique_ptr<Layer> { return std::make_unique<TextLayer>(r); }},
        {"audio", [](RootNode *r) -> std::unique_ptr<Layer> { return std::make_unique<AudioLayer>(r); }},
    };

    Factory factory = nullptr;
    for (const auto &f : factories) {
        if (type_ == f.type) { factory = f.fn; break; }
    }
    if (!factory)
        return "unknown track type: " + type_;

    const auto &segments = track_config["segments"];
    layers_.reserve(segments.size());
    for (const auto &segment : segments) {
        auto layer = factory(root_);
        if (!layer->load(segment))
            return "load layer failed: " + layer->getErrorMessage();
        layers_.emplace_back(std::move(layer));
    }

    if (!layers_.empty() && layers_.back()->hasTransition())
        return "last layer cannot have transition";

    for (auto &layer : layers_)
        layer->prepare();

    return "";
}

bool GroupLayer::draw(const gl::FBO &target, TimeMs time_ms) {
    if (!visible_)
        return true;

    for (size_t i = 0; i < layers_.size(); i++) {
        auto &layer = layers_[i];
        if (layer->getMaterialType() == MATERIAL_TYPE_AUDIO)
            continue;

        auto *transition = layer->getActiveTransition(time_ms);
        if (!transition || i == layers_.size() - 1) {
            if (!layer->draw(target, time_ms))
                return false;
            continue;
        }

        auto &next = layers_[++i];
        if (!renderTransition(layer.get(), next.get(), transition, time_ms, target))
            return false;
    }
    return true;
}

bool GroupLayer::renderTransition(Layer *from, Layer *to, Effect *transition,
                                  TimeMs time_ms, const gl::FBO &target) {
    auto *pool = root_->getFBOPool();
    gl::FBO fbo0 = pool->acquire(target.width, target.height);
    gl::FBO fbo1 = pool->acquire(target.width, target.height);
    if (!fbo0.isValid() || !fbo1.isValid()) {
        pool->release(fbo0);
        pool->release(fbo1);
        return false;
    }

    TimeMs t0 = time_ms >= from->getEndTime() ? from->getEndTime() - 1 : time_ms;
    TimeMs t1 = time_ms < to->getStartTime() ? to->getStartTime() : time_ms;
    if (!from->draw(fbo0, t0) || !to->draw(fbo1, t1)) {
        pool->release(fbo0);
        pool->release(fbo1);
        return false;
    }

    TimeMs transitionTime = time_ms - from->getEndTime() + transition->getDurationMs() / 2;
    gl::FBO effect_out = transition->apply({fbo0, fbo1}, transitionTime);
    pool->release(fbo0);
    pool->release(fbo1);

    gl::drawTextureQuad(
        target,
        gl::Texture{effect_out.texture, effect_out.width, effect_out.height},
        root_->getShader(), 0, "uTex", root_->getQuad());
    pool->release(effect_out);
    return true;
}

} // namespace vp
