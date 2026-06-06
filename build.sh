#!/bin/bash
# 构建入口：Debug / Release / GTest
#   ./build.sh           # Debug
#   ./build.sh Release
#   ./build.sh GTest     # Debug + 测试 + 覆盖率插桩
set -eu

BUILD_TYPE="${1:-Debug}"
BUILD_TESTING="OFF"
BUILD_COVERAGE="OFF"
BUILD_DIR="build/${BUILD_TYPE}"

case "${BUILD_TYPE}" in
    Debug|Release)
        ;;
    GTest)
        BUILD_TYPE="Debug"
        BUILD_TESTING="ON"
        BUILD_COVERAGE="ON"
        BUILD_DIR="build/GTest"
        ;;
    *)
        echo "Usage: $0 [Debug|Release|GTest]"
        exit 1
        ;;
esac

# GTest 模式重新配置，确保覆盖率插桩干净
if [ "${BUILD_TESTING}" = "ON" ]; then
    rm -rf "${BUILD_DIR}"
fi

cmake -S . -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DBUILD_TESTING="${BUILD_TESTING}" \
    -DBUILD_COVERAGE="${BUILD_COVERAGE}"

cmake --build "${BUILD_DIR}" -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 8)"
