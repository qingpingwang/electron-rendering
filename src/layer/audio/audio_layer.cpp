#include "audio_layer.h"
#include "../../core/root_node.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace vp {

AudioLayer::AudioLayer(RootNode *root) :
    Layer(root) {
}

bool AudioLayer::load(const json &config, const std::string &base_path) {
    if (!Layer::load(config, base_path))
        return false;

    if (!material_) {
        setError("audio material is null");
        return false;
    }

    if (material_->getPath().empty()) {
        setError("audio material path is empty");
        return false;
    }

    return true;
}

bool AudioLayer::renderContent(const gl::FBO &, TimeMs) {
    return true;
}

} // namespace vp
