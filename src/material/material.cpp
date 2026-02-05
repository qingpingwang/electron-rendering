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

// ========== EffectMaterial 实现 ==========

bool EffectMaterial::load(const json &material_json) {
    id_ = material_json.value("id", "");
    type_ = material_json.value("type", "resource");

    // 根据类型加载不同字段
    if (type_ == "resource") {
        // resource 类型：存储资源路径
        path_ = material_json.value("resource_path", "");
        if (path_.empty()) {
            return false;
        }
    } else if (type_ == "builtin") {
        // builtin 类型：存储内置特效名称
        effect_name_ = material_json.value("effect_name", "");
        if (effect_name_.empty()) {
            return false;
        }
    } else if (type_ == "lut") {
        // lut 类型：存储 LUT 文件路径
        path_ = material_json.value("lut_file", "");
        if (path_.empty()) {
            return false;
        }
    }

    // 存储额外配置（如果有）
    if (material_json.contains("config")) {
        config_ = material_json["config"];
    }

    return !id_.empty();
}

const std::string &EffectMaterial::getType() const {
    return type_;
}

const std::string &EffectMaterial::getResourcePath() const {
    return path_;
}

const std::string &EffectMaterial::getEffectName() const {
    return effect_name_;
}

const nlohmann::json &EffectMaterial::getConfig() const {
    return config_;
}

} // namespace vp
