#pragma once

#include "layer.h"

namespace vp {

class TextMaterial;

class TextLayer : public Layer {
public:
    TextLayer(RootNode *root);
    ~TextLayer() override;

    bool load(const nlohmann::json &config, const std::string &base_path = "") override;

protected:
    bool renderContent(const gl::FBO &fbo, TimeMs time_ms) override;
    MaterialType getMaterialType() const override { return MATERIAL_TYPE_TEXT; }

private:
    TextMaterial *text_material_ = nullptr;
};

} // namespace vp
