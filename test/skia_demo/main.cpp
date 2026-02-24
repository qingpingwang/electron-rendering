/*
 * Skia GPU Demo - 渲染到 GLFW 窗口，关闭时保存最后一帧
 */

#ifdef __APPLE__
#define SK_BUILD_FOR_MAC
#endif

#include "include/core/SkCanvas.h"
#include "include/core/SkSurface.h"
#include "include/core/SkImage.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkPaint.h"
#include "include/core/SkColor.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkShader.h"
#include "include/core/SkTypeface.h"
#include "include/effects/SkGradient.h"
#include "include/gpu/GpuTypes.h"
#include "include/gpu/ganesh/GrBackendSurface.h"
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/GrTypes.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"
#include "include/gpu/ganesh/gl/GrGLBackendSurface.h"
#include "include/gpu/ganesh/gl/GrGLDirectContext.h"
#include "include/gpu/ganesh/gl/GrGLInterface.h"
#include "include/gpu/ganesh/gl/GrGLTypes.h"
#include "include/encode/SkPngEncoder.h"
#include "include/ports/SkFontMgr_mac_ct.h"

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#endif
#include <GLFW/glfw3.h>
#include <cstdio>
#include <cstring>
#include <thread>

constexpr int WIDTH = 800;
constexpr int HEIGHT = 600;

static sk_sp<SkTypeface> gTypeface;

void initTypeface() {
    auto mgr = SkFontMgr_New_CoreText(nullptr);
    gTypeface = mgr->matchFamilyStyle("Helvetica", SkFontStyle());
    if (!gTypeface) gTypeface = mgr->matchFamilyStyle(nullptr, SkFontStyle());
}

void drawScene(SkCanvas *canvas) {
    canvas->clear(SkColorSetARGB(255, 200, 220, 255));

    SkPaint rectPaint;
    rectPaint.setColor(SK_ColorRED);
    rectPaint.setAntiAlias(true);
    canvas->drawRect(SkRect::MakeXYWH(50, 50, 200, 150), rectPaint);

    SkPaint circlePaint;
    circlePaint.setColor(SkColorSetARGB(128, 0, 255, 0));
    circlePaint.setAntiAlias(true);
    canvas->drawCircle(400, 300, 100, circlePaint);

    SkFont font(gTypeface, 48);
    SkPaint textPaint;
    textPaint.setColor(SK_ColorBLACK);
    textPaint.setAntiAlias(true);
    const char *text = "Skia Works!";
    canvas->drawSimpleText(text, strlen(text), SkTextEncoding::kUTF8,
                           100, 400, font, textPaint);

    SkPoint pts[] = {{300, 450}, {700, 450}};
    SkColor4f gradColors[] = {
        SkColor4f::FromColor(SK_ColorYELLOW),
        SkColor4f::FromColor(SK_ColorMAGENTA)};
    SkGradient::Colors colors(gradColors, SkTileMode::kClamp);
    SkGradient gradient(colors, {});
    sk_sp<SkShader> shader = SkShaders::LinearGradient(pts, gradient);

    SkPaint gradientPaint;
    gradientPaint.setShader(shader);
    canvas->drawRect(SkRect::MakeXYWH(300, 450, 400, 100), gradientPaint);
}

void drawHUD(SkCanvas *canvas, double elapsed, double fps) {
    SkPaint bgPaint;
    bgPaint.setColor(SkColorSetARGB(180, 0, 0, 0));
    canvas->drawRect(SkRect::MakeXYWH(10, 10, 220, 70), bgPaint);

    SkFont font(gTypeface, 20);
    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(SK_ColorWHITE);

    char buf[64];
    snprintf(buf, sizeof(buf), "Time: %.1fs", elapsed);
    canvas->drawSimpleText(buf, strlen(buf), SkTextEncoding::kUTF8, 20, 38, font, paint);

    if (fps >= 30)
        paint.setColor(SkColorSetRGB(0, 255, 100));
    else if (fps >= 15)
        paint.setColor(SK_ColorYELLOW);
    else
        paint.setColor(SK_ColorRED);

    snprintf(buf, sizeof(buf), "FPS:  %.1f", fps);
    canvas->drawSimpleText(buf, strlen(buf), SkTextEncoding::kUTF8, 20, 66, font, paint);
}

bool saveAsPNG(SkSurface *surface, GrDirectContext *ctx, const char *filename) {
    sk_sp<SkImage> image = surface->makeImageSnapshot();
    if (!image) return false;

    sk_sp<SkData> data = SkPngEncoder::Encode(ctx, image.get(), {});
    if (!data) return false;

    FILE *fp = fopen(filename, "wb");
    if (!fp) return false;
    fwrite(data->data(), 1, data->size(), fp);
    fclose(fp);
    printf("✅ Saved: %s\n", filename);
    return true;
}

sk_sp<SkSurface> createWindowSurface(GrDirectContext *ctx, int w, int h) {
    GrGLint fbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);

    GrGLFramebufferInfo fbInfo;
    fbInfo.fFBOID = (GrGLuint)fbo;
    fbInfo.fFormat = GL_RGBA8;

    auto backendRT = GrBackendRenderTargets::MakeGL(w, h, 0, 8, fbInfo);

    return SkSurfaces::WrapBackendRenderTarget(
        ctx, backendRT, kBottomLeft_GrSurfaceOrigin,
        kRGBA_8888_SkColorType, SkColorSpace::MakeSRGB(), nullptr);
}

int main() {
    printf("=== Skia Demo ===\n\n");

    if (!glfwInit()) {
        fprintf(stderr, "❌ GLFW init failed\n");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    GLFWwindow *window = glfwCreateWindow(WIDTH, HEIGHT, "Skia Demo", nullptr, nullptr);
    if (!window) {
        fprintf(stderr, "❌ Window creation failed\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(0);

    printf("✅ OpenGL: %s / %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));

    auto glInterface = GrGLMakeNativeInterface();
    auto grContext = GrDirectContexts::MakeGL(glInterface);
    if (!grContext) {
        fprintf(stderr, "❌ Skia context failed\n");
        glfwTerminate();
        return 1;
    }

    initTypeface();

    int fbWidth, fbHeight;
    glfwGetFramebufferSize(window, &fbWidth, &fbHeight);

    auto surface = createWindowSurface(grContext.get(), fbWidth, fbHeight);
    if (!surface) {
        fprintf(stderr, "❌ Surface creation failed\n");
        glfwTerminate();
        return 1;
    }
    printf("✅ Surface: %dx%d\n", fbWidth, fbHeight);

    SkCanvas *canvas = surface->getCanvas();
    float scaleX = (float)fbWidth / WIDTH;
    float scaleY = (float)fbHeight / HEIGHT;

    printf("✅ Close the window to save & exit.\n");

    double startTime = glfwGetTime();
    int frameCount = 0;
    double fps = 0.0;
    double lastFpsTime = startTime;

    constexpr double FPS_UPDATE_INTERVAL = 0.1;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        double now = glfwGetTime();
        frameCount++;

        if (now - lastFpsTime >= FPS_UPDATE_INTERVAL) {
            fps = frameCount / (now - lastFpsTime);
            frameCount = 0;
            lastFpsTime = now;
        }

        canvas->save();
        canvas->scale(scaleX, scaleY);
        drawScene(canvas);
        drawHUD(canvas, now - startTime, fps);
        canvas->restore();

        grContext->flushAndSubmit();
        glfwSwapBuffers(window);
    }

    saveAsPNG(surface.get(), grContext.get(), "skia_demo_output.png");

    surface.reset();
    grContext.reset();
    glfwTerminate();
    return 0;
}
