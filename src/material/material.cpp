#include "material.h"

using json = nlohmann::json;

namespace vp {

const std::string &Material::getId() const {
    return id_;
}

const std::string &Material::getPath() const {
    return path_;
}

bool VideoMaterial::load(const json &material_json) {
    id_ = material_json.value("id", "");
    path_ = material_json.value("path", "");
    width_ = material_json.value("width", 0);
    height_ = material_json.value("height", 0);
    duration_ = material_json.value("duration", 0);

    return !id_.empty() && !path_.empty();
}

int VideoMaterial::getWidth() const {
    return width_;
}

int VideoMaterial::getHeight() const {
    return height_;
}

int64_t VideoMaterial::getDuration() const {
    return duration_;
}

} // namespace vp
