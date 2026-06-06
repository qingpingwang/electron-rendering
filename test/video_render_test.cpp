#include <gtest/gtest.h>

#include "../src/codec/video_encoder.h"
#include "../src/core/root_node.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

const std::string kConfigPath = std::string(VP_RESOURCES_DIR) + "/test.json";

std::string loadFile(const std::string &path) {
    std::ifstream file(path);
    if (!file.is_open())
        return "";
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

// 完整渲染管线：按协议 fps 逐帧渲染整段时长 → FFmpeg VFR 编码。
TEST(VideoRenderTest, RenderAndEncodeFrames) {
    ASSERT_TRUE(fs::exists(kConfigPath)) << "test resource missing: " << kConfigPath;

    std::string json_str = loadFile(kConfigPath);
    ASSERT_FALSE(json_str.empty());

    auto config = nlohmann::json::parse(json_str, nullptr, false);
    ASSERT_FALSE(config.is_null());

    int fps              = config.value("fps", 30);
    int width            = config["canvas_config"].value("width", 1920);
    int height           = config["canvas_config"].value("height", 1080);
    int64_t duration_ms  = config.value("duration", 0LL);
    int64_t step_ms      = fps > 0 ? 1000 / fps : 33;
    ASSERT_GT(duration_ms, 0) << "protocol duration is 0";

    vp::RootNode root;
    ASSERT_TRUE(root.init()) << "failed to init RootNode";

    nlohmann::json protocol = nlohmann::json::parse(json_str);
    std::string base_path = VP_RESOURCES_DIR;
    ASSERT_TRUE(root.load(protocol, base_path)) << root.getErrorMessage();

    fs::path output_dir = fs::path(VP_TEST_BINARY_DIR) / "gtest_output";
    fs::create_directories(output_dir);
    std::string output_file = (output_dir / "vp_video_render_test.mp4").string();

    vp::VideoEncoder encoder;
    vp::EncoderConfig enc_cfg;
    enc_cfg.width    = width;
    enc_cfg.height   = height;
    enc_cfg.fps      = fps;
    enc_cfg.bit_rate = 4000000;
    enc_cfg.preset   = "medium";
    enc_cfg.crf      = 23;
    ASSERT_TRUE(encoder.open(output_file, enc_cfg));

    std::vector<uint8_t> frame_buffer(static_cast<size_t>(width) * height * 4);
    int frame_idx = 0;

    for (int64_t time_ms = 0; time_ms < duration_ms; time_ms += step_ms) {
        root.setCurrentTime(time_ms);
        EXPECT_GE(root.draw(frame_buffer.data(), frame_buffer.size()), 0)
            << "render failed at " << time_ms << " ms";
        ASSERT_TRUE(encoder.encodeFrame(frame_buffer.data(), time_ms))
            << "encode failed at " << time_ms << " ms";
        ++frame_idx;
    }

    encoder.close();

    EXPECT_GT(frame_idx, 0) << "no frames were rendered";
    EXPECT_TRUE(fs::exists(output_file)) << "output file not found: " << output_file;
    if (fs::exists(output_file))
        EXPECT_GT(fs::file_size(output_file), 0u) << "output file is empty: " << output_file;
}

} // namespace
