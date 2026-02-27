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

TimeMs VideoMaterial::getDuration() const {
    return duration_;
}

// ========== EffectMaterial 实现 ==========

bool EffectMaterial::load(const json &config, const std::string &base_path) {
    id_ = config.value("id", "");
    type_ = config.value("type", "effect");

    if (id_.empty()) {
        setError("effect material: id is required");
        return false;
    }

    path_ = config.value("resource_path", "");
    if (path_.empty()) {
        setError("effect material[" + id_ + "]: resource_path is required for type 'resource'");
        return false;
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

// 解析 content.solid 模式的颜色（fill / stroke / shadow 共用）
static Color4f parseSolidColor(const json &obj, float default_alpha = 1.0f) {
    Color4f c = {0.0f, 0.0f, 0.0f, default_alpha};
    if (!obj.contains("content") || !obj["content"].contains("solid"))
        return c;

    const auto &solid = obj["content"]["solid"];
    c.a = solid.value("alpha", 1.0f);
    if (solid.contains("color") && solid["color"].is_array()) {
        const auto &arr = solid["color"];
        if (arr.size() > 0) c.r = arr[0].get<float>();
        if (arr.size() > 1) c.g = arr[1].get<float>();
        if (arr.size() > 2) c.b = arr[2].get<float>();
    }
    return c;
}

static bool parseStyleRun(const json &style, TextStyleRun &run) {
    if (!style.contains("range") || !style["range"].is_array() || style["range"].size() < 2)
        return false;

    run.range_start = style["range"][0].get<int>();
    run.range_end = style["range"][1].get<int>();
    run.font_size = style.value("size", 24.0f);
    run.letter_spacing = style.value("letter_spacing", 0.0f);
    run.line_height = style.value("line_height", 1.0f);

    if (style.contains("font")) {
        run.font_id = style["font"].value("id", "");
        run.font_path = style["font"].value("path", "");
    }

    if (style.contains("fill")) {
        const auto &fill = style["fill"];
        float fill_alpha = fill.value("alpha", 1.0f);
        run.fill = parseSolidColor(fill, 1.0f);
        run.fill.a *= fill_alpha;
    }

    if (style.contains("strokes") && style["strokes"].is_array()) {
        for (const auto &s : style["strokes"]) {
            TextStroke stroke;
            stroke.width = s.value("width", 0.0f);
            stroke.color = parseSolidColor(s, 1.0f);
            run.strokes.push_back(stroke);
        }
    }

    if (style.contains("shadows") && style["shadows"].is_array()) {
        for (const auto &s : style["shadows"]) {
            TextShadow shadow;
            shadow.color = parseSolidColor(s, 1.0f);
            shadow.color.a = s.value("alpha", shadow.color.a);
            shadow.angle = s.value("angle", 0.0f);
            shadow.distance = s.value("distance", 0.0f);
            shadow.diffuse = s.value("diffuse", 0.0f);
            run.shadows.push_back(shadow);
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

bool TransitionMaterial::load(const json &config, const std::string &base_path) {
    duration_ms_ = config.value("duration", 0);
    if (duration_ms_ == 0) {
        setError("transition material[" + id_ + "]: duration is required");
        return false;
    }

    return EffectMaterial::load(config, base_path);
}

TimeMs TransitionMaterial::getDuration() const {
    return duration_ms_;
}

} // namespace vp
