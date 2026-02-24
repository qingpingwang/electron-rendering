#pragma once

#include "../core/loadable.h"
#include <nlohmann/json.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace vp {

// 素材类型（固定数组下标）
enum MaterialType {
    MATERIAL_TYPE_VIDEO = 0,
    MATERIAL_TYPE_EFFECT = 1,
    MATERIAL_TYPE_TEXT = 2,
    MATERIAL_TYPE_COUNT = 3
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
    int64_t getDuration() const;

private:
    int width_ = 0;
    int height_ = 0;
    int64_t duration_ = 0;
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

// 富文本样式区间（纯数据，无渲染依赖）
struct TextStyleRun {
    int range_start = 0;
    int range_end = 0;
    float font_size = 24.0f;
    std::string font_path;
    std::string font_id;
    float color_r = 1.0f;
    float color_g = 1.0f;
    float color_b = 1.0f;
    float alpha = 1.0f;
};

// 文字素材
class TextMaterial : public Material {
public:
    TextMaterial() = default;
    ~TextMaterial() override = default;

    bool load(const nlohmann::json &config, const std::string &base_path = "") override;

    const std::string &getText() const;
    TextAlignment getAlignment() const;
    const std::vector<TextStyleRun> &getStyleRuns() const;

private:
    std::string text_;
    TextAlignment alignment_ = TEXT_ALIGN_LEFT;
    std::vector<TextStyleRun> style_runs_;
};

} // namespace vp
