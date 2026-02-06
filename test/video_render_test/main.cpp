// 视频渲染测试 - 从 JSON 配置渲染完整视频
// 功能：加载协议 → 逐帧渲染 → FFmpeg 编码 → 保存视频文件
//
// 编译运行：
//   cmake --build build
//   ./build/test/video_render_test/video_render_test
//
// 注意：程序启动时会自动切换到项目根目录，所有相对路径从根目录开始

#include "../../src/engine/root_node.h"
#include "../../src/encoder/video_encoder.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <unistd.h>

using namespace vp;

// 读取 JSON 配置文件
std::string loadConfigFile(const std::string &config_path) {
    std::ifstream file(config_path);
    if (!file.is_open()) {
        std::cerr << "Failed to open config file: " << config_path << std::endl;
        return "";
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    return content;
}

// 解析 JSON 获取配置信息
nlohmann::json parseJson(const std::string &json_str) {
    return nlohmann::json::parse(json_str, nullptr, false);
}

int main(int argc, char *argv[]) {
    // 切换到项目根目录（可执行文件在 build/test/ 下，向上两级）
    if (chdir("../..") != 0) {
        std::cerr << "Failed to change directory to project root" << std::endl;
        return 1;
    }

    std::cout << "==================================" << std::endl;
    std::cout << "  Video Render Test" << std::endl;
    std::cout << "==================================" << std::endl;
    std::cout << std::endl;

    // 解析参数（现在是相对于项目根目录）
    std::string config_path = "./test/test.json";
    std::string output_file = "./output.mp4";

    if (argc > 1) {
        config_path = argv[1];
    }
    if (argc > 2) {
        output_file = argv[2];
    }

    std::cout << "Config: " << config_path << std::endl;
    std::cout << "Output: " << output_file << std::endl;
    std::cout << std::endl;

    // 1. 加载配置
    std::cout << "[1/4] Loading configuration..." << std::endl;
    std::string json_str = loadConfigFile(config_path);
    if (json_str.empty()) {
        std::cerr << "Failed to load config file" << std::endl;
        return 1;
    }

    auto config = parseJson(json_str);
    if (config.is_null()) {
        std::cerr << "Failed to parse JSON" << std::endl;
        return 1;
    }

    int64_t duration_ms = config.value("duration", 0);
    int fps = config.value("fps", 30);
    int width = config["canvas_config"].value("width", 1920);
    int height = config["canvas_config"].value("height", 1080);

    std::cout << "  Duration: " << duration_ms << " ms" << std::endl;
    std::cout << "  FPS: " << fps << std::endl;
    std::cout << "  Resolution: " << width << "x" << height << std::endl;
    std::cout << std::endl;

    // 2. 初始化渲染引擎
    std::cout << "[2/4] Initializing render engine..." << std::endl;
    RootNode root;

    if (!root.init()) {
        std::cerr << "Failed to initialize RootNode" << std::endl;
        return 1;
    }

    std::string error_message = root.loadFromJson(json_str);
    if (!error_message.empty()) {
        std::cerr << "Failed to load RootNode from JSON: " << error_message << std::endl;
        return 1;
    }

    std::cout << "  RootNode loaded successfully" << std::endl;
    std::cout << "  Project ID: " << root.getId() << std::endl;
    std::cout << "  Canvas: " << root.getWidth() << "x" << root.getHeight() << std::endl;
    std::cout << std::endl;

    // 3. 初始化视频编码器
    std::cout << "[3/4] Initializing video encoder..." << std::endl;
    VideoEncoder encoder;

    EncoderConfig encoder_config;
    encoder_config.width = width;
    encoder_config.height = height;
    encoder_config.bit_rate = 4000000; // 4 Mbps
    encoder_config.preset = "medium";
    encoder_config.crf = 23;

    if (!encoder.open(output_file, encoder_config)) {
        std::cerr << "Failed to open video encoder" << std::endl;
        return 1;
    }

    std::cout << "  Encoder opened: " << output_file << std::endl;
    std::cout << "  Resolution: " << encoder.getWidth() << "x" << encoder.getHeight() << std::endl;
    std::cout << "  Target FPS: " << fps << " (from config)" << std::endl;
    std::cout << "  Duration: " << duration_ms << " ms" << std::endl;
    std::cout << std::endl;

    // 4. 逐帧渲染并编码
    std::cout << "[4/4] Rendering and encoding frames..." << std::endl;

    int64_t frame_duration_ms = 1000 / fps;
    std::vector<uint8_t> frame_buffer(width * height * 4);

    auto start_time = std::chrono::high_resolution_clock::now();

    int64_t time_ms = 0;
    int frame_idx = 0;

    while (time_ms < duration_ms) {
        // 设置当前时间并渲染
        root.setCurrentTime(time_ms);
        bool success = root.draw(frame_buffer.data(), frame_buffer.size());
        if (!success) {
            std::cerr << "Failed to render frame at time " << time_ms << " ms" << std::endl;
            time_ms += frame_duration_ms;
            continue;
        }

        // 编码帧（传入时间戳）
        if (!encoder.encodeFrame(frame_buffer.data(), time_ms)) {
            std::cerr << "Failed to encode frame at time " << time_ms << " ms" << std::endl;
            time_ms += frame_duration_ms;
            continue;
        }

        frame_idx++;

        // 进度显示
        if (frame_idx % 10 == 0 || time_ms + frame_duration_ms >= duration_ms) {
            float progress = time_ms * 100.0f / duration_ms;
            std::cout << "  Progress: " << frame_idx << " frames, " << time_ms << "/" << duration_ms << " ms"
                      << " (" << static_cast<int>(progress) << "%)" << std::endl;
        }

        // 递增时间
        time_ms += frame_duration_ms;
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    // 关闭编码器，flush 所有缓存帧
    encoder.close();

    std::cout << std::endl;
    std::cout << "==================================" << std::endl;
    std::cout << "Rendering completed!" << std::endl;
    std::cout << "  Total frames: " << frame_idx << std::endl;
    std::cout << "  Total time: " << duration.count() << " ms" << std::endl;
    std::cout << "  Average FPS: " << (frame_idx * 1000.0 / duration.count()) << std::endl;
    std::cout << "  Output file: " << output_file << std::endl;
    std::cout << "==================================" << std::endl;

    return 0;
}
