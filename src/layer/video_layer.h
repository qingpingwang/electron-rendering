#pragma once

#include "../core/types.h"
#include "../codec/video_decoder.h"
#include "../gl/types.h"
#include "layer.h"
#include <memory>

namespace vp {

// 视频图层
class VideoLayer : public Layer {
public:
    VideoLayer(RootNode *root);
    ~VideoLayer() override;

    bool load(const nlohmann::json &config, const std::string &base_path = "") override;

    double getFrameRate() const;
    bool isLoaded() const;

protected:
    bool renderContent(const gl::FBO &fbo) override;
    MaterialType getMaterialType() const override { return MATERIAL_TYPE_VIDEO; }

private:
    // 计算当前应该显示的帧时间（带线性插值对齐到帧边界）
    TimeMs calculateFrameTime() const;
    std::unique_ptr<VideoDecoder> decoder_ = nullptr;
    VideoFrame current_frame_;
    int video_width_ = 0;
    int video_height_ = 0;
    TimeMs video_duration_ms_ = 0;

    // 源视频时间范围（从源视频的哪个位置开始解码）
    TimeMs source_start_ms_ = 0;    // 源视频的开始时间
    TimeMs source_duration_ms_ = 0; // 从源视频解码的时长

    // 渲染资源
    gl::Texture texture_ = gl::Texture{}; // 自己的纹理

    // 纹理缓存优化：记录已上传的帧PTS，避免重复上传相同数据
    TimeMs uploaded_pts_ = kInvalidTime;
};

} // namespace vp
