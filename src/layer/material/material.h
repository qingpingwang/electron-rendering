#pragma once

#include "../../core/loadable.h"
#include "../../core/types.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace vp {

// 素材类型（固定数组下标）
enum MaterialType {
    MATERIAL_TYPE_VIDEO = 0,
    MATERIAL_TYPE_EFFECT = 1,
    MATERIAL_TYPE_TEXT = 2,
    MATERIAL_TYPE_TRANSITION = 3,
    MATERIAL_TYPE_AUDIO = 4,
    MATERIAL_TYPE_COUNT = 5
};

// 素材基类
class Material : public Loadable {
public:
    Material() = default;
    virtual ~Material() = default;

    Material(const Material &) = delete;
    Material &operator=(const Material &) = delete;

    // 从 JSON 加载素材配置
    bool load(const nlohmann::json &config, const std::string &base_path = "") override = 0;

    const std::string &getId() const;
    const std::string &getPath() const;

protected:
    std::string id_;
    std::string path_;
};

// 视频素材
class VideoMaterial : public Material {
public:
    VideoMaterial() = default;
    ~VideoMaterial() override = default;

    bool load(const nlohmann::json &config, const std::string &base_path = "") override;

    int getWidth() const;
    int getHeight() const;
    TimeMs getDuration() const;

private:
    int width_ = 0;
    int height_ = 0;
    TimeMs duration_ = 0;
};

// 特效素材（配置层）
// 存储特效的元信息，实际执行由 Effect 类负责
class EffectMaterial : public Material {
public:
    EffectMaterial() = default;
    ~EffectMaterial() override = default;

    bool load(const nlohmann::json &config, const std::string &base_path = "") override;

    // 获取特效类型（"resource", "builtin", "lut" 等）
    const std::string &getType() const;

    // 获取资源路径（对于 resource 类型）
    const std::string &getResourcePath() const;

    // 获取特效名称（对于 builtin 类型）
    const std::string &getEffectName() const;

    // 获取额外配置（扩展字段）
    const nlohmann::json &getConfig() const;

private:
    std::string type_;        // 特效类型
    std::string effect_name_; // 内置特效名称（builtin 类型）
    nlohmann::json config_;   // 额外配置参数
};

// 文字对齐方式
enum TextAlignment {
    TEXT_ALIGN_LEFT = 0,
    TEXT_ALIGN_CENTER = 1,
    TEXT_ALIGN_RIGHT = 2
};

struct Color4f {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
};

struct TextShadow {
    Color4f color;
    float angle = 0.0f;    // 度，0 = 右，90 = 下
    float distance = 0.0f; // 像素
    float diffuse = 0.0f;  // 模糊扩散
};

struct TextStroke {
    float width = 0.0f;
    Color4f color;
};

// 富文本样式区间（纯数据，无渲染依赖）
struct TextStyleRun {
    int range_start = 0;
    int range_end = 0;
    float font_size = 24.0f;
    std::string font_path;
    std::string font_id;

    Color4f fill = {1.0f, 1.0f, 1.0f, 1.0f};

    float letter_spacing = 0.0f;
    float line_height = 1.0f;

    std::vector<TextStroke> strokes;
    std::vector<TextShadow> shadows;
};

// 文字素材
class TextMaterial : public Material {
public:
    TextMaterial() = default;
    ~TextMaterial() override = default;

    bool load(const nlohmann::json &config, const std::string &base_path = "") override;

    const std::string &getText() const;
    void setText(const std::string &text);

    TextAlignment getAlignment() const;
    void setAlignment(TextAlignment a) { alignment_ = a; }

    const std::vector<TextStyleRun> &getStyleRuns() const;
    size_t getRunCount() const { return style_runs_.size(); }

    bool setRunFontSize(size_t idx, float v) { if (idx >= style_runs_.size()) return false; style_runs_[idx].font_size = v; return true; }
    bool setRunLetterSpacing(size_t idx, float v) { if (idx >= style_runs_.size()) return false; style_runs_[idx].letter_spacing = v; return true; }
    bool setRunLineHeight(size_t idx, float v) { if (idx >= style_runs_.size()) return false; style_runs_[idx].line_height = v; return true; }
    bool setRunFill(size_t idx, float r, float g, float b, float a) {
        if (idx >= style_runs_.size()) return false;
        auto &f = style_runs_[idx].fill;
        f.r = r; f.g = g; f.b = b; f.a = a;
        return true;
    }
    bool setRunStrokeWidth(size_t ri, size_t si, float w) {
        if (ri >= style_runs_.size() || si >= style_runs_[ri].strokes.size()) return false;
        style_runs_[ri].strokes[si].width = w;
        return true;
    }
    bool setRunStrokeColor(size_t ri, size_t si, float r, float g, float b, float a) {
        if (ri >= style_runs_.size() || si >= style_runs_[ri].strokes.size()) return false;
        auto &c = style_runs_[ri].strokes[si].color;
        c.r = r; c.g = g; c.b = b; c.a = a;
        return true;
    }

private:
    void adjustRunsToText();
    static int utf8Length(const std::string &s);

    std::string text_;
    TextAlignment alignment_ = TEXT_ALIGN_LEFT;
    std::vector<TextStyleRun> style_runs_;
};

// 转场继承自特效
class TransitionMaterial : public EffectMaterial {
public:
    TransitionMaterial() = default;
    ~TransitionMaterial() override = default;

    bool load(const nlohmann::json &config, const std::string &base_path = "") override;

    TimeMs getDuration() const;

private:
    TimeMs duration_ms_ = 0;
};

// 音频素材
class AudioMaterial : public Material {
public:
    AudioMaterial() = default;
    ~AudioMaterial() override = default;

    bool load(const nlohmann::json &config, const std::string &base_path = "") override;

    const std::string &getName() const;

private:
    std::string name_;
};

} // namespace vp
