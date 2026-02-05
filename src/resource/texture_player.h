#pragma once

#include "../gl/types.h"
#include <nlohmann/json.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace vp {

// 前向声明
class RootNode;
class VideoDecoder;

// 纹理播放器基类
class TexturePlayer {
public:
    TexturePlayer(RootNode *root);
    virtual ~TexturePlayer();

    // 从配置加载
    virtual bool load(const nlohmann::json &config, const std::string &base_path);

    // 播放（获取指定时间的纹理）
    virtual gl::Texture play(int64_t time_ms) = 0;

    // 获取信息
    std::string getName() const {
        return name_;
    }
    int getWidth() const {
        return width_;
    }
    int getHeight() const {
        return height_;
    }

    const std::vector<std::tuple<int, int>> &getRenderPassIndices() const {
        return render_pass_indices_;
    }

    int getPipeForPass(int pass_index) const {
        auto it = std::find_if(render_pass_indices_.begin(), render_pass_indices_.end(),
                               [pass_index](const std::tuple<int, int> &item) {
                                   return std::get<1>(item) == pass_index;
                               });
        return (it != render_pass_indices_.end()) ? std::get<0>(*it) : -1;
    }

    GLuint getTextureId() const {
        return texture_.id;
    }

protected:
    RootNode *root_;
    std::string name_;
    std::string file_path_;
    int width_ = 0;
    int height_ = 0;
    int repeat_mode_ = 0;                                   // 0=停止, 2=循环
    std::vector<std::tuple<int, int>> render_pass_indices_; // 该纹理会被哪些 pass 使用,pipe 和 pass 索引
    gl::Texture texture_;                                   // 纹理对象（所有派生类共用）
};

// 图片纹理播放器（支持静态图和 GIF）
class ImageTexture : public TexturePlayer {
public:
    ImageTexture(RootNode *root);
    ~ImageTexture() override;

    bool load(const nlohmann::json &config, const std::string &base_path) override;
    gl::Texture play(int64_t time_ms) override;

private:
    bool loadStaticImage(); // 加载静态图片
    bool loadGif();         // 加载 GIF 动画

    std::vector<std::vector<uint8_t>> frame_data_; // GIF 帧数据（静态图为空）
    float fps_ = 0.0f;
    int64_t last_frame_index_ = -1;
};

// 视频纹理播放器
class VideoTexture : public TexturePlayer {
public:
    VideoTexture(RootNode *root);
    ~VideoTexture() override;

    bool load(const nlohmann::json &config, const std::string &base_path) override;
    gl::Texture play(int64_t time_ms) override;

private:
    std::unique_ptr<VideoDecoder> decoder_;
    int64_t last_time_ms_ = -1;
    int64_t duration_ms_ = 0;
    float fps_ = 0.0f;
};

} // namespace vp
