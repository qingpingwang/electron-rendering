#pragma once
#include "../core/types.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace vp {

struct FrameLocation {
    // —— 时间戳（毫秒，零点 = 视频首帧的 PTS）——
    TimeMs pts_ms = 0;         // 显示时间戳（Presentation Time Stamp）
    TimeMs dts_ms = 0;         // 解码时间戳（Decoding Time Stamp）；有 B 帧时 dts_ms != pts_ms
    TimeMs start_ms = 0;       // 该帧在显示时间轴上的起点；当前实现下恒等于 pts_ms
    TimeMs end_ms = 0;         // 该帧显示区间的终点（= 显示顺序的下一帧的 start_ms）
    TimeMs gop_pts_ms = 0;     // 该帧所在 GOP 起始关键帧（I 帧）的 pts_ms；seek 用

    // —— 帧索引（两套互相独立的编号体系）——
    int sample_index = 0;      // 在 MP4 stbl 中的 sample 编号（即 DTS / 文件物理顺序，从 0 开始）
    int display_index = 0;     // 按 PTS 排序后的位置（即人眼看到的第几帧，从 0 开始）
    int gop_index = 0;         // 第几个 GOP（从 0 开始）
    int frame_in_gop = 0;      // 在所属 GOP 内的位置（按 sample 顺序，I 帧为 0）
    int gop_frame_count = 0;   // 该 GOP 包含的帧数
    int total_frames = 0;      // 整个视频的总帧数

    // —— 文件物理位置（字节，相对文件起点）——
    int64_t sample_offset = 0; // 该 sample 在 mp4 文件中的起始字节偏移
    int64_t sample_size = 0;   // 该 sample 的字节长度
    int64_t gop_offset = 0;    // 该 GOP 起始 sample（I 帧）的 sample_offset
    int64_t gop_size = 0;      // 该 GOP 所有 sample 的字节长度之和
};

class MoovHelper {
public:
    MoovHelper();
    ~MoovHelper();

    MoovHelper(const MoovHelper &) = delete;
    MoovHelper &operator=(const MoovHelper &) = delete;

    bool load(const std::string &path);
    bool hasVideoTrack() const;
    int width() const;
    int height() const;
    TimeMs durationMs() const;
    double frameRate() const;
    const std::vector<uint8_t> &codecConfig() const;
    bool queryFrame(TimeMs time_ms, FrameLocation &out) const;
    std::vector<FrameLocation> getAllFrameLocations() const;

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace vp