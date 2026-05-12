#include "../../src/codec/moov_helper.h"
#include <iostream>

int main(int argc, char *argv[]) {
    const std::string path = argc > 1 ? argv[1] : "../../test/test.mp4";

    vp::MoovHelper moov;
    if (!moov.load(path) || !moov.hasVideoTrack()) {
        std::cerr << "No indexed MP4 video track found: " << path << std::endl;
        return 1;
    }

    const auto frames = moov.getAllFrameLocations();
    std::cout << "{" << std::endl;
    std::cout << "  \"width\": " << moov.width() << "," << std::endl;
    std::cout << "  \"height\": " << moov.height() << "," << std::endl;
    std::cout << "  \"duration_ms\": " << moov.durationMs() << "," << std::endl;
    std::cout << "  \"frame_count\": " << frames.size() << "," << std::endl;

    std::cout << "  \"pts_ms\": [";
    for (size_t i = 0; i < frames.size(); ++i) {
        std::cout << frames[i].pts_ms << (i + 1 == frames.size() ? "" : ", ");
    }
    std::cout << "]," << std::endl;

    std::cout << "  \"keyframe_indices\": [";
    bool first = true;
    for (const auto &frame : frames) {
        if (frame.frame_in_gop != 0) {
            continue;
        }
        if (!first) {
            std::cout << ", ";
        }
        std::cout << frame.sample_index;
        first = false;
    }
    std::cout << "]," << std::endl;

    std::cout << "  \"gops\": [" << std::endl;
    first = true;
    for (const auto &frame : frames) {
        if (frame.frame_in_gop != 0) {
            continue;
        }
        if (!first) {
            std::cout << "," << std::endl;
        }
        std::cout << "    {\"index\": " << frame.gop_index
                  << ", \"offset\": " << frame.gop_offset
                  << ", \"size\": " << frame.gop_size
                  << ", \"frames\": " << frame.gop_frame_count << "}";
        first = false;
    }
    std::cout << std::endl;
    std::cout << "  ]" << std::endl;
    std::cout << "}" << std::endl;

    return 0;
}