// 视频解码测试 - 解码所有帧并保存为 PNG
// 编译运行：
//   cmake --build build
//   ./build/test/decoder_test [video_path]

#include "../../src/codec/video_decoder.h"
#include "../../third_party/stb_image/stb_image_write.h"
#include <iostream>

using namespace vp;

int main(int argc, char *argv[]) {
    std::string video_path = "./test/resources/sprint_effect/textures/mask.webm";
    if (argc > 1) {
        video_path = argv[1];
    }

    // 打开解码器
    VideoDecoder decoder;
    if (!decoder.open(video_path)) {
        std::cerr << "Failed to open video: " << video_path << std::endl;
        return 1;
    }

    int64_t duration_ms = decoder.getDurationMs();
    double fps = decoder.getFrameRate();
    int64_t frame_interval_ms = (fps > 0) ? (int64_t)(1000.0 / fps) : 33;

    std::cout << "Decoding: " << video_path << std::endl;
    std::cout << "Resolution: " << decoder.getWidth() << "x" << decoder.getHeight()
              << ", Duration: " << duration_ms << "ms, FPS: " << fps << std::endl;

    // 循环解码所有帧
    VideoFrame frame;
    int64_t time_ms = 0;
    int frame_count = 0;

    while (time_ms < duration_ms) {
        if (!decoder.decodeFrameAt(time_ms, frame) || !frame.valid) {
            std::cerr << "Failed to decode frame at " << time_ms << "ms" << std::endl;
            break;
        }

        // 保存为 frame_pts.png
        std::string output_path = "./frame_" + std::to_string(time_ms) + "ms.png";
        if (!stbi_write_png(output_path.c_str(), frame.width, frame.height, 4,
                            frame.data, frame.width * 4)) {
            std::cerr << "Failed to save " << output_path << std::endl;
            break;
        }

        frame_count++;
        time_ms += frame_interval_ms;
    }

    std::cout << "Completed: " << frame_count << " frames saved" << std::endl;
    return 0;
}
