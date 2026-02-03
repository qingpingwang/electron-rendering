#pragma once

#include "../nlohmann/json.hpp"
#include <cstdint>
#include <string>

namespace vp {

// 素材基类
class Material {
public:
    Material() = default;
    virtual ~Material() = default;

    Material(const Material &) = delete;
    Material &operator=(const Material &) = delete;

    // 从 JSON 加载素材配置
    virtual bool load(const nlohmann::json &material_json) = 0;

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

    bool load(const nlohmann::json &material_json) override;

    int getWidth() const;
    int getHeight() const;
    int64_t getDuration() const;

private:
    int width_ = 0;
    int height_ = 0;
    int64_t duration_ = 0;
};

} // namespace vp
