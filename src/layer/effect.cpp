#include "effect.h"
#include "../resource/render_resource.h"

namespace vp {

// ========== Effect 基类实现 ==========

Effect::Effect(RootNode *root) :
    root_(root) {
}

Effect::~Effect() {
}

bool Effect::isActive(TimeMs time_ms) {
    // todo 更复杂的时间支持
    return time_ms != kInvalidTime && time_ms < getRenderResource()->getResourceDuration();
}

RenderResource *Effect::getRenderResource() {
    return nullptr;
}

const RenderResource *Effect::getRenderResource() const {
    return nullptr;
}

// ========== ResourceEffect 实现 ==========

ResourceEffect::ResourceEffect(RootNode *root) :
    Effect(root) {
    resource_ = std::make_unique<RenderResource>(root);
}

ResourceEffect::~ResourceEffect() = default;

bool ResourceEffect::load(const nlohmann::json &config, const std::string &base_path) {
    // 从 JSON 加载资源路径
    if (!config.contains("resourcePath")) {
        setError("resourcePath is required");
        return false;
    }

    std::string resource_path = config["resourcePath"];
    return loadFromFolder(resource_path);
}

bool ResourceEffect::loadFromFolder(const std::string &folder_path) {
    if (!resource_) {
        setError("render resource not initialized");
        return false;
    }

    // 直接加载 RenderResource
    if (!resource_->loadFromFolder(folder_path)) {
        setError("load resource failed: " + resource_->getErrorMessage());
        return false;
    }

    name_ = resource_->getName();
    return true;
}

gl::FBO ResourceEffect::apply(const std::vector<gl::FBO> &inputs, TimeMs time_ms) {
    if (!resource_ || inputs.empty() || !isActive(time_ms))
        return gl::FBO{};

    return resource_->render(inputs, time_ms);
}

const char *ResourceEffect::getType() const {
    return "effect";
}

RenderResource *ResourceEffect::getRenderResource() {
    return resource_.get();
}

const RenderResource *ResourceEffect::getRenderResource() const {
    return resource_.get();
}

} // namespace vp
