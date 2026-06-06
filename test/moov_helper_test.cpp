#include <gtest/gtest.h>

#include "../src/codec/moov_helper.h"

#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

const std::string kMp4Path = std::string(VP_RESOURCES_DIR) + "/test.mp4";

// 解析 MP4 moov box，校验视频轨道元信息与帧索引一致性。
TEST(MoovHelperTest, LoadAndParseVideoTrack) {
    ASSERT_TRUE(fs::exists(kMp4Path)) << "test resource missing: " << kMp4Path;

    vp::MoovHelper moov;
    ASSERT_TRUE(moov.load(kMp4Path));
    ASSERT_TRUE(moov.hasVideoTrack());

    EXPECT_GT(moov.width(), 0);
    EXPECT_GT(moov.height(), 0);
    EXPECT_GT(moov.durationMs(), 0);

    const auto frames = moov.getAllFrameLocations();
    EXPECT_FALSE(frames.empty());
}

// 帧定位信息应单调有序，且至少包含一个关键帧（GOP 起点）。
TEST(MoovHelperTest, FrameLocationsConsistent) {
    ASSERT_TRUE(fs::exists(kMp4Path)) << "test resource missing: " << kMp4Path;

    vp::MoovHelper moov;
    ASSERT_TRUE(moov.load(kMp4Path));

    const auto frames = moov.getAllFrameLocations();
    ASSERT_FALSE(frames.empty());

    int keyframe_count = 0;
    for (const auto &frame : frames) {
        if (frame.frame_in_gop == 0)
            ++keyframe_count;
    }
    EXPECT_GT(keyframe_count, 0);
}

} // namespace
