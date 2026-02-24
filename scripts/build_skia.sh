#!/bin/bash
# Skia 编译脚本 - 简化版
# 用法: ./scripts/build_skia.sh [--clean]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SKIA_DIR="$PROJECT_ROOT/third_party/skia"
ARCH=$(uname -m)

# 处理参数
CLEAN_BUILD=false
[[ "$1" == "--clean" ]] && CLEAN_BUILD=true
[[ "$1" == "--help" || "$1" == "-h" ]] && {
    echo "用法: $0 [--clean|--help]"
    exit 0
}

# 检测架构
[[ "$ARCH" == "arm64" ]] && TARGET_CPU="arm64" || TARGET_CPU="x64"

echo "🔨 编译 Skia ($TARGET_CPU)"

# 清理
$CLEAN_BUILD && [ -d "$SKIA_DIR/out" ] && rm -rf "$SKIA_DIR/out"

# 检查已编译
if [ -f "$SKIA_DIR/out/Release/libskia.a" ] && ! $CLEAN_BUILD; then
    echo "✅ 已编译 ($(du -h $SKIA_DIR/out/Release/libskia.a | cut -f1))"
    exit 0
fi

# 检查 submodule
[ ! -e "$SKIA_DIR/.git" ] && {
    echo "❌ Skia submodule 未初始化"
    exit 1
}

cd "$SKIA_DIR"

# 检查并安装依赖
echo "📦 检查依赖..."
command -v python3 >/dev/null 2>&1 || { echo "❌ 需要 python3"; exit 1; }
command -v ninja >/dev/null 2>&1 || { echo "❌ 需要 ninja"; exit 1; }

# 同步依赖（静默）
echo "📦 同步依赖..."
python3 tools/git-sync-deps > /dev/null 2>&1 || {
    echo "❌ 依赖同步失败"
    exit 1
}

# 配置（静默）
echo "⚙️  配置构建..."
bin/gn gen out/Release --args="is_official_build=true \
 skia_use_freetype=true \
 skia_use_system_freetype2=false \
 skia_use_system_libpng=false \
 skia_use_system_libjpeg_turbo=false \
 skia_use_system_libwebp=false \
 skia_use_system_zlib=false \
 skia_enable_pdf=false \
 skia_use_system_icu=false \
 skia_use_system_harfbuzz=false \
 target_cpu=\"$TARGET_CPU\"" > /dev/null 2>&1

# 编译
NCPU=$(sysctl -n hw.ncpu 2>/dev/null || echo "8")
echo "🔨 编译中 ($NCPU 核心)..."

START_TIME=$(date +%s)
ninja -C out/Release -j $NCPU
END_TIME=$(date +%s)

MINUTES=$(((END_TIME - START_TIME) / 60))

# 验证
[ ! -f "$SKIA_DIR/out/Release/libskia.a" ] && {
    echo "❌ libskia.a 未生成"
    exit 1
}

echo "✅ 完成 (${MINUTES}分, $(du -h $SKIA_DIR/out/Release/libskia.a | cut -f1))"
