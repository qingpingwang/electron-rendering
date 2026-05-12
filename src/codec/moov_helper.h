#pragma once
#include "../core/types.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace vp {

struct FrameLocation {
    TimeMs pts_ms = 0;
    TimeMs dts_ms = 0;
    TimeMs start_ms = 0;
    TimeMs end_ms = 0;
    TimeMs gop_pts_ms = 0;
    int sample_index = 0;
    int display_index = 0;
    int gop_index = 0;
    int64_t gop_offset = 0;
    int64_t gop_size = 0;
    int64_t sample_offset = 0;
    int64_t sample_size = 0;
    int frame_in_gop = 0;
    int gop_frame_count = 0;
    int total_frames = 0;
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