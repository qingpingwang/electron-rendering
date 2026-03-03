#pragma once

#include "../../core/types.h"
#include "../../codec/video_decoder.h"
#include "../../gl/types.h"
#include "../base/layer.h"
#include <memory>

namespace vp {

// 视频图层
class VideoLayer : public Layer {
public:
    VideoLayer(RootNode *root);
    ~VideoLayer() override;

    bool load(const nlohmann::json &config, const std::string &base_path = "") override;
    void prepare() override;

    double getFrameRate() const;
    bool isLoaded() const;

protected:
    bool renderContent(const gl::FBO &fbo, TimeMs time_ms) override;
    MaterialType getMaterialType() const override { return MATERIAL_TYPE_VIDEO; }

private:
    // 计算当前应该显示的帧时间（带线性插值对齐到帧边界）
    TimeMs calculateFrameTime(TimeMs time_ms) const;
    std::unique_ptr<VideoDecoder> decoder_ = nullptr;
    VideoFrame current_frame_;
    int video_width_ = 0;
    int video_height_ = 0;
    TimeMs video_duration_ms_ = 0;

    // 渲染资源
    gl::Texture texture_ = gl::Texture{}; // 自己的纹理
    gl::QuadMesh quad_;                    // 长边适配后的顶点网格

    // 纹理缓存优化：记录已上传的帧PTS，避免重复上传相同数据
    TimeMs uploaded_pts_ = kInvalidTime;
};

} // namespace vp
