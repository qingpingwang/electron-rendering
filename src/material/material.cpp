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

// ========== TextMaterial 实现 ==========

static bool parseStyleRun(const json &style, TextStyleRun &run) {
    if (!style.contains("range") || !style["range"].is_array() || style["range"].size() < 2)
        return false;

    run.range_start = style["range"][0].get<int>();
    run.range_end = style["range"][1].get<int>();
    run.font_size = style.value("size", 24.0f);

    if (style.contains("font")) {
        run.font_id = style["font"].value("id", "");
        run.font_path = style["font"].value("path", "");
    }

    if (style.contains("fill")) {
        const auto &fill = style["fill"];
        float fill_alpha = fill.value("alpha", 1.0f);

        if (fill.contains("content") && fill["content"].contains("solid")) {
            const auto &solid = fill["content"]["solid"];
            float solid_alpha = solid.value("alpha", 1.0f);
            run.alpha = fill_alpha * solid_alpha;

            if (solid.contains("color") && solid["color"].is_array()) {
                const auto &c = solid["color"];
                if (c.size() > 0) run.color_r = c[0].get<float>();
                if (c.size() > 1) run.color_g = c[1].get<float>();
                if (c.size() > 2) run.color_b = c[2].get<float>();
            }
        }
    }

    return true;
}

bool TextMaterial::load(const json &config, const std::string &base_path) {
    id_ = config.value("id", "");
    if (id_.empty()) {
        setError("text material: id is required");
        return false;
    }

    alignment_ = static_cast<TextAlignment>(config.value("alignment", 0));

    std::string content_str = config.value("content", "");
    if (content_str.empty()) {
        setError("text material[" + id_ + "]: content is required");
        return false;
    }

    json content;
    try {
        content = json::parse(content_str);
    } catch (const json::parse_error &e) {
        setError("text material[" + id_ + "]: invalid content JSON: " + std::string(e.what()));
        return false;
    }

    text_ = content.value("text", "");

    if (content.contains("styles") && content["styles"].is_array()) {
        const auto &styles = content["styles"];
        style_runs_.reserve(styles.size());
        for (const auto &s : styles) {
            TextStyleRun run;
            if (!parseStyleRun(s, run)) {
                setError("text material[" + id_ + "]: invalid style run");
                return false;
            }
            style_runs_.push_back(std::move(run));
        }
    }

    if (style_runs_.empty()) {
        setError("text material[" + id_ + "]: no style runs");
        return false;
    }

    return true;
}

const std::string &TextMaterial::getText() const {
    return text_;
}
TextAlignment TextMaterial::getAlignment() const {
    return alignment_;
}
const std::vector<TextStyleRun> &TextMaterial::getStyleRuns() const {
    return style_runs_;
}

} // namespace vp
