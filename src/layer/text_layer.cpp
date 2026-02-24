#include "text_layer.h"
#include "../core/font_manager.h"
#include "../engine/root_node.h"
#include "../material/material.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMetrics.h"
#include "include/core/SkPaint.h"
#include "include/core/SkSurface.h"
#include "include/core/SkTypeface.h"
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/GrBackendSurface.h"
#include "include/gpu/ganesh/gl/GrGLBackendSurface.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"
#include "include/gpu/GpuTypes.h"
#include "include/gpu/ganesh/GrTypes.h"

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#endif

using json = nlohmann::json;

namespace vp {

// UTF-8 字符索引 -> 字节区间提取
static std::string utf8Substr(const std::string &str, int char_start, int char_end) {
    size_t byte_start = 0, byte_end = 0;
    int idx = 0;
    size_t i = 0;

    while (i < str.size() && idx < char_end) {
        if (idx == char_start) byte_start = i;
        unsigned char c = str[i];
        if (c < 0x80)
            i += 1;
        else if ((c >> 5) == 0x06)
            i += 2;
        else if ((c >> 4) == 0x0E)
            i += 3;
        else if ((c >> 3) == 0x1E)
            i += 4;
        else
            i += 1;
        idx++;
    }
    if (idx == char_start) byte_start = i;
    byte_end = i;

    if (byte_start >= str.size()) return "";
    return str.substr(byte_start, byte_end - byte_start);
}

static uint32_t toSkColor(float r, float g, float b, float a) {
    auto clamp = [](float v) -> uint8_t {
        return static_cast<uint8_t>(std::max(0.0f, std::min(1.0f, v)) * 255.0f);
    };
    return SkColorSetARGB(clamp(a), clamp(r), clamp(g), clamp(b));
}

TextLayer::TextLayer(RootNode *root) :
    Layer(root) {
}

TextLayer::~TextLayer() = default;

bool TextLayer::load(const json &config, const std::string &base_path) {
    if (!Layer::load(config, base_path))
        return false;

    text_material_ = static_cast<TextMaterial *>(material_);
    if (!text_material_) {
        setError("text material not found");
        return false;
    }

    return true;
}

bool TextLayer::renderContent(const gl::FBO &fbo) {
    GrDirectContext *ctx = root_->getSkiaContext();
    if (!ctx || !text_material_)
        return true;

    const auto &runs = text_material_->getStyleRuns();
    const std::string &text = text_material_->getText();
    if (text.empty() || runs.empty())
        return true;

    ctx->resetContext();

    GrGLFramebufferInfo fbInfo;
    fbInfo.fFBOID = fbo.fbo;
    fbInfo.fFormat = GL_RGBA8;

    auto backendRT = GrBackendRenderTargets::MakeGL(
        fbo.width, fbo.height, 0, 8, fbInfo);

    auto surface = SkSurfaces::WrapBackendRenderTarget(
        ctx, backendRT,
        kTopLeft_GrSurfaceOrigin,
        kRGBA_8888_SkColorType,
        nullptr, nullptr);

    if (!surface)
        return false;

    SkCanvas *canvas = surface->getCanvas();
    auto &fm = FontManager::getInstance();

    // 测量总宽度用于对齐
    float total_width = 0;
    for (const auto &sr : runs) {
        std::string sub = utf8Substr(text, sr.range_start, sr.range_end);
        SkFont font(fm.resolve(sr.font_path, sr.font_id), sr.font_size);
        total_width += font.measureText(sub.c_str(), sub.size(), SkTextEncoding::kUTF8);
    }

    float x = 0;
    TextAlignment align = text_material_->getAlignment();
    if (align == TEXT_ALIGN_CENTER)
        x = (fbo.width - total_width) * 0.5f;
    else if (align == TEXT_ALIGN_RIGHT)
        x = fbo.width - total_width;

    // 垂直居中：取第一个 run 的 metrics 做基线
    SkFont first_font(fm.resolve(runs[0].font_path, runs[0].font_id), runs[0].font_size);
    SkFontMetrics metrics;
    first_font.getMetrics(&metrics);
    float y = (fbo.height - (metrics.fDescent - metrics.fAscent)) * 0.5f - metrics.fAscent;

    SkPaint paint;
    paint.setAntiAlias(true);

    for (const auto &sr : runs) {
        std::string sub = utf8Substr(text, sr.range_start, sr.range_end);
        if (sub.empty()) continue;

        SkFont font(fm.resolve(sr.font_path, sr.font_id), sr.font_size);
        font.setEdging(SkFont::Edging::kAntiAlias);

        paint.setColor(toSkColor(sr.color_r, sr.color_g, sr.color_b, sr.alpha));
        canvas->drawString(sub.c_str(), x, y, font, paint);
        x += font.measureText(sub.c_str(), sub.size(), SkTextEncoding::kUTF8);
    }

    ctx->flushAndSubmit();
    return true;
}

} // namespace vp
