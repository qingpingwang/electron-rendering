#include "text_layer.h"
#include "../../core/font_manager.h"
#include "../../core/root_node.h"
#include "../material/material.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkPaint.h"
#include "include/core/SkSurface.h"
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/GrBackendSurface.h"
#include "include/gpu/ganesh/gl/GrGLBackendSurface.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"
#include "include/gpu/GpuTypes.h"
#include "include/gpu/ganesh/GrTypes.h"
#include "modules/skparagraph/include/Paragraph.h"
#include "modules/skparagraph/include/ParagraphBuilder.h"
#include "modules/skparagraph/include/ParagraphStyle.h"
#include "modules/skparagraph/include/TextStyle.h"
#include "include/effects/SkImageFilters.h"

#include <cmath>

namespace {
constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
}

#include "../../gl/types.h"

using json = nlohmann::json;
using namespace skia::textlayout;

namespace vp {

static SkColor toSkColor(const Color4f &c) {
    auto clamp = [](float v) -> uint8_t {
        return static_cast<uint8_t>(std::max(0.0f, std::min(1.0f, v)) * 255.0f);
    };
    return SkColorSetARGB(clamp(c.a), clamp(c.r), clamp(c.g), clamp(c.b));
}

static size_t utf8CharToByteOffset(const std::string &str, int char_idx) {
    size_t i = 0;
    int idx = 0;
    while (i < str.size() && idx < char_idx) {
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
    return i;
}

static TextAlign toSkTextAlign(TextAlignment align) {
    switch (align) {
    case TEXT_ALIGN_CENTER: return TextAlign::kCenter;
    case TEXT_ALIGN_RIGHT: return TextAlign::kRight;
    default: return TextAlign::kLeft;
    }
}

// StyleFn 签名: void(TextStyle&, const TextStyleRun&, size_t run_idx)
template <typename StyleFn>
static std::unique_ptr<Paragraph> buildParagraph(
    const std::vector<TextStyleRun> &runs,
    const std::string &text,
    TextAlignment alignment,
    SkScalar layout_width,
    StyleFn &&styleFn) {
    auto &fm = FontManager::getInstance();
    auto fc = fm.getFontCollection();
    auto unicode = fm.getUnicode();
    if (!fc || !unicode) return nullptr;

    ParagraphStyle ps;
    ps.setTextAlign(toSkTextAlign(alignment));
    auto builder = ParagraphBuilder::make(ps, fc, unicode);

    for (size_t ri = 0; ri < runs.size(); ++ri) {
        const auto &sr = runs[ri];
        size_t bs = utf8CharToByteOffset(text, sr.range_start);
        size_t be = utf8CharToByteOffset(text, sr.range_end);
        if (bs >= be || bs >= text.size()) continue;

        skia::textlayout::TextStyle style;
        style.setFontSize(sr.font_size);
        style.setLetterSpacing(sr.letter_spacing);
        style.setHeight(sr.line_height);
        style.setHeightOverride(sr.line_height != 1.0f);
        auto tf = fm.resolve(sr.font_path, sr.font_id);
        if (tf) style.setTypeface(tf);

        styleFn(style, sr, ri);

        builder->pushStyle(style);
        builder->addText(text.c_str() + bs, be - bs);
        builder->pop();
    }

    auto p = builder->Build();
    p->layout(layout_width);
    return p;
}

static void applyStrokeStyle(skia::textlayout::TextStyle &style,
                             const TextStyleRun &sr, size_t stroke_idx) {
    if (stroke_idx < sr.strokes.size()) {
        const auto &st = sr.strokes[stroke_idx];
        SkPaint paint;
        paint.setStyle(SkPaint::kStroke_Style);
        paint.setStrokeWidth(st.width * sr.font_size * 2.0f);
        paint.setStrokeJoin(SkPaint::kRound_Join);
        paint.setStrokeCap(SkPaint::kRound_Cap);
        paint.setColor(toSkColor(st.color));
        paint.setAntiAlias(true);
        style.setForegroundPaint(paint);
    } else {
        style.setColor(SK_ColorTRANSPARENT);
    }
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

bool TextLayer::renderContent(const gl::FBO &fbo, TimeMs /* time_ms */) {
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
    auto backendRT = GrBackendRenderTargets::MakeGL(fbo.width, fbo.height, 0, 8, fbInfo);
    auto surface = SkSurfaces::WrapBackendRenderTarget(
        ctx, backendRT, kTopLeft_GrSurfaceOrigin, kRGBA_8888_SkColorType, nullptr, nullptr);
    if (!surface) return false;

    SkCanvas *canvas = surface->getCanvas();

    // ---- clip_ 变换: 归一化坐标 → 像素 ----
    float cx = fbo.width * 0.5f;
    float cy = fbo.height * 0.5f;
    float tx = clip_.transform_x * cx;
    float ty = clip_.transform_y * cy;
    float sx = clip_.flip_h ? -clip_.scale_x : clip_.scale_x;
    float sy = clip_.flip_v ? -clip_.scale_y : clip_.scale_y;

    canvas->save();

    if (clip_.alpha < 1.0f) {
        SkPaint alphaPaint;
        alphaPaint.setAlphaf(std::max(0.0f, std::min(1.0f, clip_.alpha)));
        canvas->saveLayer(nullptr, &alphaPaint);
    }

    canvas->translate(cx + tx, cy + ty);
    canvas->rotate(-clip_.rotation);
    canvas->scale(sx, sy);
    canvas->translate(-cx, -cy);

    // ---- 文字内容绘制 ----
    TextAlignment alignment = text_material_->getAlignment();
    SkScalar w = static_cast<SkScalar>(fbo.width);

    size_t max_strokes = 0;
    for (const auto &sr : runs)
        max_strokes = std::max(max_strokes, sr.strokes.size());

    auto fillStyle = [](skia::textlayout::TextStyle &s, const TextStyleRun &sr, size_t) {
        s.setColor(toSkColor(sr.fill));
    };

    auto probe = buildParagraph(runs, text, alignment, w, fillStyle);
    if (!probe) {
        if (clip_.alpha < 1.0f) canvas->restore();
        canvas->restore();
        ctx->flushAndSubmit();
        return false;
    }
    float y = (fbo.height - probe->getHeight()) * 0.5f;

    auto paintLayers = [&](int visible_run) {
        auto maskStyle = [visible_run](auto applyVisible) {
            return [=](skia::textlayout::TextStyle &s, const TextStyleRun &sr, size_t ri) {
                if (visible_run >= 0 && ri != static_cast<size_t>(visible_run))
                    s.setColor(SK_ColorTRANSPARENT);
                else
                    applyVisible(s, sr, ri);
            };
        };

        for (size_t i = max_strokes; i > 0; --i) {
            size_t si = i - 1;
            auto p = buildParagraph(runs, text, alignment, w,
                                    maskStyle([si](skia::textlayout::TextStyle &s, const TextStyleRun &sr, size_t) {
                                        applyStrokeStyle(s, sr, si);
                                    }));
            if (p) p->paint(canvas, 0, y);
        }

        auto fill = buildParagraph(runs, text, alignment, w, maskStyle(fillStyle));
        if (fill) fill->paint(canvas, 0, y);
    };

    // 1. 逐 run 阴影
    for (size_t ri = 0; ri < runs.size(); ++ri) {
        for (const auto &sh : runs[ri].shadows) {
            float rad = sh.angle * kDegToRad;
            SkScalar sigma = sh.diffuse * runs[ri].font_size * 2.0f;

            SkPaint lp;
            lp.setImageFilter(SkImageFilters::DropShadowOnly(
                sh.distance * std::cos(rad), -sh.distance * std::sin(rad),
                sigma, sigma, toSkColor(sh.color), nullptr));

            canvas->saveLayer(nullptr, &lp);
            paintLayers(static_cast<int>(ri));
            canvas->restore();
        }
    }

    // 2. 实际内容
    paintLayers(-1);

    if (clip_.alpha < 1.0f) canvas->restore();
    canvas->restore();

    ctx->flushAndSubmit();
    return true;
}

} // namespace vp
