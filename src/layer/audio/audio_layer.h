#pragma once

#include "../base/layer.h"

namespace vp {

class AudioMaterial;

class AudioLayer : public Layer {
public:
    AudioLayer(RootNode *root);
    ~AudioLayer() override = default;

    bool load(const nlohmann::json &config, const std::string &base_path = "") override;

    nlohmann::json dump() const override;

    void prepare() override {
    }

protected:
    bool renderContent(const gl::FBO &fbo, TimeMs time_ms) override;
    MaterialType getMaterialType() const override {
        return MATERIAL_TYPE_AUDIO;
    }
};

} // namespace vp
