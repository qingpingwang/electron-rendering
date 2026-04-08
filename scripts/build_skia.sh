#!/bin/bash
# Skia 编译脚本 - 简化版
# 用法: ./scripts/build_skia.sh [--clean]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SKIA_DIR="$PROJECT_ROOT/third_party/skia"
SKIA_LIB_A="$SKIA_DIR/out/Release/libskia.a"
SKIA_LIB_MSVC="$SKIA_DIR/out/Release/skia.lib"
ARCH=$(uname -m)

# 产物路径因工具链而异：Unix/MinGW 多为 .a，Windows MSVC 多为 .lib
skia_out_lib() {
    if [ -f "$SKIA_LIB_A" ]; then echo "$SKIA_LIB_A"
    elif [ -f "$SKIA_LIB_MSVC" ]; then echo "$SKIA_LIB_MSVC"
    fi
}

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
SKIA_LIB=$(skia_out_lib)
if [ -n "$SKIA_LIB" ] && ! $CLEAN_BUILD; then
    SKIA_LIB_DU=$(du -h "$SKIA_LIB" | cut -f1)
    echo "✅ 已编译 ($SKIA_LIB_DU)"
    exit 0
fi

# 检查 submodule
[ ! -e "$SKIA_DIR/.git" ] && {
    echo "❌ Skia submodule 未初始化"
    exit 1
}

cd "$SKIA_DIR"

# 尽早设置，使 gn / ninja 子进程里的 cl 尽量输出英文（部分中文 VS 安装仍可能为中文）。
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) export VSLANG=1033 ;;
esac

# 检查并安装依赖
echo "📦 检查依赖..."
command -v python3 >/dev/null 2>&1 || { echo "❌ 需要 python3"; exit 1; }
command -v ninja >/dev/null 2>&1 || { echo "❌ 需要 ninja"; exit 1; }

# 同步依赖（成功时安静，失败时打印原始输出）
echo "📦 同步依赖..."
set +e
SYNC_DEPS_OUT=$(python3 tools/git-sync-deps 2>&1)
SYNC_DEPS_RC=$?
set -e
[ "$SYNC_DEPS_RC" -ne 0 ] && {
    echo "$SYNC_DEPS_OUT"
    echo "❌ 依赖同步失败 (exit $SYNC_DEPS_RC)"
    exit 1
}

# 配置（成功时安静，失败时打印 gn 输出——避免吞掉错误）
echo "⚙️  配置构建..."
set +e
GN_OUT=$(bin/gn gen out/Release --args="is_official_build=true \
 skia_use_freetype=true \
 skia_use_system_freetype2=false \
 skia_use_system_libpng=false \
 skia_use_system_libjpeg_turbo=false \
 skia_use_system_libwebp=false \
 skia_use_system_zlib=false \
 skia_enable_pdf=false \
 skia_use_system_icu=false \
 skia_use_system_harfbuzz=false \
 skia_use_expat=false \
 skia_enable_svg=true \
 skia_enable_skshaper=true \
 skia_enable_skparagraph=true \
 skia_use_lua=true \
 target_cpu=\"$TARGET_CPU\"" 2>&1)
GN_RC=$?
set -e
[ "$GN_RC" -ne 0 ] && {
    echo "$GN_OUT"
    echo "❌ GN 配置失败 (exit $GN_RC)"
    exit 1
}

# 编译
NCPU=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo "8")
echo "🔨 编译中 ($NCPU 核心)..."

START_TIME=$(date +%s)
ninja -C out/Release -j $NCPU
END_TIME=$(date +%s)

MINUTES=$(((END_TIME - START_TIME) / 60))

# 验证
SKIA_LIB=$(skia_out_lib)
[ -z "$SKIA_LIB" ] && {
    echo "❌ Skia 库未生成（预期: $SKIA_LIB_A 或 $SKIA_LIB_MSVC）"
    exit 1
}

SKIA_LIB_DU=$(du -h "$SKIA_LIB" | cut -f1)
echo "✅ 完成 (${MINUTES}分, $SKIA_LIB_DU)"
