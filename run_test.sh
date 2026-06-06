#!/bin/sh
# 运行 GTest 单元测试并生成覆盖率报告（gcov + gcovr）。
# 前置：先执行 `sh build.sh GTest`
set -eu

BUILD_DIR="build/GTest"
TEST_BINARY="${BUILD_DIR}/video_player_tests"
COVERAGE_DIR="coverage_report"
GCOVR="python3 -m gcovr"

# macOS 的 clang 用 llvm-cov 提供 gcov 兼容接口
GCOV_ARGS=""
if [ "$(uname)" = "Darwin" ]; then
    GCOV_ARGS="--gcov-executable 'xcrun llvm-cov gcov'"
fi

if ! $GCOVR --version >/dev/null 2>&1; then
    echo "Error: gcovr is required to generate coverage reports."
    echo "Install it with:"
    echo "  python3 -m pip install --user -r requirements.txt"
    exit 1
fi

if [ ! -x "${TEST_BINARY}" ]; then
    echo "Error: test binary not found: ${TEST_BINARY}"
    echo "Build tests first:"
    echo "  sh build.sh GTest"
    exit 1
fi

rm -rf "${COVERAGE_DIR}"
mkdir -p "${COVERAGE_DIR}"
find "${BUILD_DIR}" -name '*.gcda' -delete

# 运行测试（WORKING_DIRECTORY 为项目根，测试资源路径已由 VP_TEST_DIR 提供绝对路径）
"${TEST_BINARY}"

PROJECT_ROOT="$(pwd)"

# shellcheck disable=SC2086
eval $GCOVR \
    --root "\"${PROJECT_ROOT}\"" \
    --object-directory "\"${BUILD_DIR}\"" \
    --filter "\"${PROJECT_ROOT}/src/.*\"" \
    --exclude "\"${PROJECT_ROOT}/test/.*\"" \
    --exclude "\"${PROJECT_ROOT}/third_party/.*\"" \
    ${GCOV_ARGS} \
    --txt "\"${COVERAGE_DIR}/coverage.txt\"" \
    --html-details "\"${COVERAGE_DIR}/index.html\"" \
    --html-single-page \
    --xml "\"${COVERAGE_DIR}/coverage.xml\"" \
    --print-summary

echo ""
echo "Coverage report: ${COVERAGE_DIR}/index.html"
