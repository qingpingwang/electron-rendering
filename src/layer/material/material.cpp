#include "material.h"
#include "effect.h"
#include "../../resource/render_resource.h"
#include <filesystem>

using json = nlohmann::json;

namespace vp {

const std::string &Material::getId() const {
    return id_;
}

const std::string &Material::getPath() const {
    return path_;
}

bool Material::load(const json &config, const std::string &base_path) {
    id_ = config.value("id", "");
    path_ = config.value("path", "");
    type_ = config.value("type", "");
    name_ = config.value("name", "");
    if (id_.empty()) {
        setError("material: id is required");
        return false;
    }
    return true;
}

json Material::dump() const {
    return {
        {"id", id_},
        {"path", path_},
        {"type", type_},
        {"name", name_},
    };
}

bool VideoMaterial::load(const json &config, const std::string &base_path) {
    if (!Material::load(config, base_path)) {
        setError("video material: " + getErrorMessage());
        return false;
    }
    if (path_.empty()) {
        setError("video material[" + id_ + "]: path is required");
        return false;
    }

    width_ = config.value("width", 0);
    height_ = config.value("height", 0);
    duration_ = config.value("duration", 0);

    return true;
}

json VideoMaterial::dump() const {
    json j = Material::dump();
    j["width"] = width_;
    j["height"] = height_;
    j["duration"] = duration_;
    return j;
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

void VideoMaterial::updateWHAndDuration(int width, int height, TimeMs duration) {
    width_ = width;
    height_ = height;
    duration_ = duration;
}

// ========== EffectMaterial 实现 ==========

EffectMaterial::EffectMaterial(RootNode *root) : root_(root) {}

EffectMaterial::~EffectMaterial() = default;

bool EffectMaterial::load(const json &config, const std::string &base_path) {
    if (!Material::load(config, base_path)) {
        setError("effect material: " + getErrorMessage());
        return false;
    }

    if (path_.empty()) {
        setError("effect material[" + id_ + "]: path is required");
        return false;
    }

    effect_name_ = config.value("name", "");

    if (config.contains("config"))
        config_ = config["config"];

    // 创建并加载 ResourceEffect
    if (root_) {
        resource_effect_ = std::make_unique<ResourceEffect>(root_);
        const std::string folder = std::filesystem::path(path_).is_absolute()
                                       ? path_
                                       : (base_path.empty() ? path_ : base_path + "/" + path_);
        if (!resource_effect_->loadFromFolder(folder)) {
            setError("effect material[" + id_ + "]: " + resource_effect_->getErrorMessage());
            return false;
        }
    }

    return true;
}

json EffectMaterial::dump() const {
    json j = Material::dump();
    if (!config_.is_null())
        j["config"] = config_;
    return j;
}

const std::string &EffectMaterial::getEffectName() const { return effect_name_; }
const nlohmann::json &EffectMaterial::getConfig() const { return config_; }

gl::FBO EffectMaterial::apply(const std::vector<gl::FBO> &inputs, TimeMs time_ms) {
    if (!resource_effect_) return gl::FBO{};
    return resource_effect_->apply(inputs, time_ms);
}

bool EffectMaterial::isActive(TimeMs time_ms) const {
    if (!resource_effect_) return false;
    return resource_effect_->isActive(time_ms);
}

TimeMs EffectMaterial::getDurationMs() const {
    if (!resource_effect_) return 0;
    return resource_effect_->getDurationMs();
}

Effect *EffectMaterial::getEffect() const {
    return resource_effect_.get();
}

RenderResource *EffectMaterial::getRenderResource() const {
    if (!resource_effect_) return nullptr;
    return resource_effect_->getRenderResource();
}

bool EffectMaterial::setFloatParam(const std::string &name, float value) {
    auto *r = getRenderResource();
    return r && r->setFloatParam(name, value);
}

bool EffectMaterial::setVecParam(const std::string &name, const std::vector<float> &value) {
    auto *r = getRenderResource();
    return r && r->setVecParam(name, value);
}

bool EffectMaterial::setBoolParam(const std::string &name, bool value) {
    auto *r = getRenderResource();
    return r && r->setBoolParam(name, value);
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
    if (!Material::load(config, base_path)) {
        setError("text material: " + getErrorMessage());
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

static json dumpSolidColor(const Color4f &c) {
    return {{"content", {{"solid", {{"color", {c.r, c.g, c.b}}, {"alpha", c.a}}}}}};
}

static json dumpStyleRun(const TextStyleRun &run) {
    json s;
    s["range"] = {run.range_start, run.range_end};
    s["size"] = run.font_size;
    s["letter_spacing"] = run.letter_spacing;
    s["line_height"] = run.line_height;

    if (!run.font_id.empty() || !run.font_path.empty())
        s["font"] = {{"id", run.font_id}, {"path", run.font_path}};

    s["fill"] = dumpSolidColor(run.fill);

    if (!run.strokes.empty()) {
        auto &arr = s["strokes"] = json::array();
        for (const auto &st : run.strokes) {
            json sj = dumpSolidColor(st.color);
            sj["width"] = st.width;
            arr.push_back(sj);
        }
    }
    if (!run.shadows.empty()) {
        auto &arr = s["shadows"] = json::array();
        for (const auto &sh : run.shadows) {
            json sj = dumpSolidColor(sh.color);
            sj["alpha"] = sh.color.a;
            sj["angle"] = sh.angle;
            sj["distance"] = sh.distance;
            sj["diffuse"] = sh.diffuse;
            arr.push_back(sj);
        }
    }
    return s;
}

json TextMaterial::dump() const {
    json content;
    content["text"] = text_;
    auto &styles = content["styles"] = json::array();
    for (const auto &run : style_runs_)
        styles.push_back(dumpStyleRun(run));

    return {
        {"id", id_},
        {"alignment", static_cast<int>(alignment_)},
        {"content", content.dump()},
    };
}

const std::string &TextMaterial::getText() const {
    return text_;
}

void TextMaterial::setText(const std::string &text) {
    text_ = text;
    adjustRunsToText();
}

int TextMaterial::utf8Length(const std::string &s) {
    int n = 0;
    for (size_t i = 0; i < s.size(); ++n) {
        unsigned char c = s[i];
        if (c < 0x80)
            i += 1;
        else if ((c >> 5) == 0x06)
            i += 2;
        else if ((c >> 4) == 0x0E)
            i += 3;
        else if ((c >> 3) == 0x1E)
            i += 4;
        else
            i += 1;
    }
    return n;
}

void TextMaterial::adjustRunsToText() {
    int len = utf8Length(text_);

    // Remove runs from the back whose range_start >= len (no characters left)
    while (style_runs_.size() > 1 && style_runs_.back().range_start >= len)
        style_runs_.pop_back();

    // Clamp remaining runs' range_end
    for (auto &r : style_runs_) {
        if (r.range_start > len) r.range_start = len;
        if (r.range_end > len) r.range_end = len;
    }

    // Remove trailing runs with zero-length ranges, keep at least 1
    while (style_runs_.size() > 1 && style_runs_.back().range_start >= style_runs_.back().range_end)
        style_runs_.pop_back();

    // Extend last run to cover all characters
    if (!style_runs_.empty())
        style_runs_.back().range_end = len;
}

TextAlignment TextMaterial::getAlignment() const {
    return alignment_;
}
const std::vector<TextStyleRun> &TextMaterial::getStyleRuns() const {
    return style_runs_;
}

TransitionMaterial::TransitionMaterial(RootNode *root) : EffectMaterial(root) {}

bool TransitionMaterial::load(const json &config, const std::string &base_path) {
    duration_ms_ = config.value("duration", 0);
    if (duration_ms_ == 0) {
        setError("transition material[" + id_ + "]: duration is required");
        return false;
    }

    if (!EffectMaterial::load(config, base_path)) {
        setError("transition material: " + getErrorMessage());
        return false;
    }

    // 把协议声明的时长同步给 RenderResource
    if (resource_effect_ && resource_effect_->getRenderResource())
        resource_effect_->getRenderResource()->setResourceDuration(duration_ms_);

    return true;
}

json TransitionMaterial::dump() const {
    json j = EffectMaterial::dump();
    j["duration"] = duration_ms_;
    return j;
}

TimeMs TransitionMaterial::getDuration() const {
    return duration_ms_;
}

// ========== AudioMaterial 实现 ==========

bool AudioMaterial::load(const json &config, const std::string &base_path) {
    if (!Material::load(config, base_path)) {
        setError("audio material: " + getErrorMessage());
        return false;
    }

    if (path_.empty()) {
        setError("audio material[" + id_ + "]: path is required");
        return false;
    }

    return true;
}

json AudioMaterial::dump() const {
    return Material::dump();
}

} // namespace vp
