#!/bin/bash
# 构建 N-API 适配层及 nle-sdk，核心测试在 SDK 仓库维护。
set -eu
cd "$(dirname "$0")"
BUILD_TYPE="${1:-Release}"
case "$BUILD_TYPE" in
    Debug|Release) ;;
    *) echo "Usage: $0 [Debug|Release]"; exit 1 ;;
esac
cmake -S . -B "build/$BUILD_TYPE" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
cmake --build "build/$BUILD_TYPE" --target video_player -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 8)"
