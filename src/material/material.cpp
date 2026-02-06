#include "material.h"

using json = nlohmann::json;

namespace vp {

const std::string &Material::getId() const {
    return id_;
}

const std::string &Material::getPath() const {
    return path_;
}

bool VideoMaterial::load(const json &config, const std::string &base_path) {
    id_ = config.value("id", "");
    path_ = config.value("path", "");
    width_ = config.value("width", 0);
    height_ = config.value("height", 0);
    duration_ = config.value("duration", 0);

    if (id_.empty()) {
        setError("video material: id is required");
        return false;
    }
    if (path_.empty()) {
        setError("video material[" + id_ + "]: path is required");
        return false;
    }

    return true;
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

bool EffectMaterial::load(const json &config, const std::string &base_path) {
    id_ = config.value("id", "");
    type_ = config.value("type", "resource");

    if (id_.empty()) {
        setError("effect material: id is required");
        return false;
    }

    // 根据类型加载不同字段
    if (type_ == "resource") {
        // resource 类型：存储资源路径
        path_ = config.value("resource_path", "");
        if (path_.empty()) {
            setError("effect material[" + id_ + "]: resource_path is required for type 'resource'");
            return false;
        }
    } else if (type_ == "builtin") {
        // builtin 类型：存储内置特效名称
        effect_name_ = config.value("effect_name", "");
        if (effect_name_.empty()) {
            setError("effect material[" + id_ + "]: effect_name is required for type 'builtin'");
            return false;
        }
    } else if (type_ == "lut") {
        // lut 类型：存储 LUT 文件路径
        path_ = config.value("lut_file", "");
        if (path_.empty()) {
            setError("effect material[" + id_ + "]: lut_file is required for type 'lut'");
            return false;
        }
    }

    // 存储额外配置（如果有）
    if (config.contains("config")) {
        config_ = config["config"];
    }

    return true;
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
