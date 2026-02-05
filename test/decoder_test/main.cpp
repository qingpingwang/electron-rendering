// 视频解码测试 - 解码 WebM 并保存为 PNG
// 编译运行：
//   cmake --build build
//   ./build/test/decoder_test [video_path] [time_ms]

#include "../../src/decoder/video_decoder.h"
#include "../../third_party/stb_image/stb_image_write.h"
#include <iostream>

using namespace vp;

int main(int argc, char *argv[]) {
    // 参数：视频路径
    std::string video_path = "../../test/resources/sprint_effect/textures/mask.webm";

    if (argc > 1) {
        video_path = argv[1];
    }

    std::cout << "==================================" << std::endl;
    std::cout << "Video Decoder Test" << std::endl;
    std::cout << "==================================" << std::endl;
    std::cout << "Video: " << video_path << std::endl;

    // 打开解码器
    VideoDecoder decoder;
    if (!decoder.open(video_path)) {
        std::cerr << "Failed to open video" << std::endl;
        return 1;
    }

    // 打印视频信息
    int64_t duration_ms = decoder.getDurationMs();
    double fps = decoder.getFrameRate();
    int width = decoder.getWidth();
    int height = decoder.getHeight();

    std::cout << "Resolution: " << width << "x" << height << std::endl;
    std::cout << "Duration: " << duration_ms << " ms" << std::endl;
    std::cout << "FPS: " << fps << std::endl;

    // 计算总帧数和帧间隔
    int64_t frame_interval_ms = (fps > 0) ? (int64_t)(1000.0 / fps) : 33;
    int total_frames = (int)(duration_ms / frame_interval_ms);

    std::cout << "Frame interval: " << frame_interval_ms << " ms" << std::endl;
    std::cout << "Estimated frames: " << total_frames << std::endl;
    std::cout << "==================================" << std::endl;
    std::cout << std::endl;

    // 循环解码所有帧
    std::cout << "Decoding frames..." << std::endl;
    VideoFrame frame;
    int64_t time_ms = 0;
    int frame_count = 0;

    while (time_ms < duration_ms) {
        if (!decoder.decodeFrameAt(time_ms, frame) || !frame.valid) {
            std::cerr << "Failed to decode frame at " << time_ms << " ms" << std::endl;
            break;
        }

        // 保存为 PNG
        std::string output_path = "./frame_" + std::to_string(frame_count) + "_" + std::to_string(time_ms) + "ms.png";
        if (!stbi_write_png(output_path.c_str(), frame.width, frame.height, 4,
                            frame.data, frame.width * 4)) {
            std::cerr << "Failed to save frame " << frame_count << std::endl;
            break;
        }

        // 采样几个像素查看数据（只显示第一帧）
        if (frame_count == 0) {
            // 检查多个位置的像素
            int positions[][2] = {{250, 250}, {100, 250}, {250, 100}, {400, 250}, {250, 400}};
            const char *names[] = {"center", "left", "top", "right", "bottom"};
            
            for (int i = 0; i < 5; i++) {
                int x = positions[i][0];
                int y = positions[i][1];
                int offset = (y * 500 + x) * 4;
                std::cout << "  " << names[i] << " (" << x << "," << y << "): "
                          << "R=" << (int)frame.data[offset] 
                          << " G=" << (int)frame.data[offset+1]
                          << " B=" << (int)frame.data[offset+2]
                          << " A=" << (int)frame.data[offset+3] << std::endl;
            }
        }
        
        frame_count++;
        
        // 进度显示
        if (frame_count % 10 == 0 || time_ms + frame_interval_ms >= duration_ms) {
            float progress = (float)time_ms / duration_ms * 100;
            std::cout << "  Frame " << frame_count << " at " << time_ms << " ms (" << (int)progress << "%) -> " << output_path << std::endl;
        }

        time_ms += frame_interval_ms;
    }

    std::cout << std::endl;
    std::cout << "==================================" << std::endl;
    std::cout << "Completed!" << std::endl;
    std::cout << "Total frames decoded: " << frame_count << std::endl;
    std::cout << "==================================" << std::endl;

    return 0;
}
