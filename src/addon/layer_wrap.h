#pragma once

#include <napi.h>
#include <cstdint>

namespace vp {
class Layer;
}

class LayerWrap : public Napi::ObjectWrap<LayerWrap> {
public:
    static Napi::Function GetClass(Napi::Env env);
    static Napi::Object NewInstance(Napi::Env env, vp::Layer *layer,
                                    Napi::Object root_obj, uint32_t gen);

    LayerWrap(const Napi::CallbackInfo &info);

private:
    vp::Layer *layer_ = nullptr;
    Napi::ObjectReference root_ref_;
    uint32_t gen_ = 0;

    vp::Layer *getLayer(Napi::Env env);

    Napi::Value GetName(const Napi::CallbackInfo &info);
    Napi::Value GetType(const Napi::CallbackInfo &info);
    Napi::Value GetStartTime(const Napi::CallbackInfo &info);
    Napi::Value GetEndTime(const Napi::CallbackInfo &info);
    Napi::Value GetDurationMs(const Napi::CallbackInfo &info);
    Napi::Value GetActive(const Napi::CallbackInfo &info);

    Napi::Value GetText(const Napi::CallbackInfo &info);
    void SetText(const Napi::CallbackInfo &info, const Napi::Value &value);
    Napi::Value GetAlignment(const Napi::CallbackInfo &info);
    void SetAlignment(const Napi::CallbackInfo &info, const Napi::Value &value);

    Napi::Value GetStyleRunCount(const Napi::CallbackInfo &info);
    Napi::Value JsGetStyleRun(const Napi::CallbackInfo &info);
    Napi::Value JsSetStyleRunFontSize(const Napi::CallbackInfo &info);
    Napi::Value JsSetStyleRunLetterSpacing(const Napi::CallbackInfo &info);
    Napi::Value JsSetStyleRunLineHeight(const Napi::CallbackInfo &info);
    Napi::Value JsSetStyleRunFill(const Napi::CallbackInfo &info);
    Napi::Value JsSetStyleRunStrokeWidth(const Napi::CallbackInfo &info);
    Napi::Value JsSetStyleRunStrokeColor(const Napi::CallbackInfo &info);

    Napi::Value GetVideoFrameRate(const Napi::CallbackInfo &info);
    Napi::Value GetVideoLoaded(const Napi::CallbackInfo &info);

    Napi::Value GetVisible(const Napi::CallbackInfo &info);
    void SetVisible(const Napi::CallbackInfo &info, const Napi::Value &value);

    Napi::Value GetMuted(const Napi::CallbackInfo &info);
    void SetMuted(const Napi::CallbackInfo &info, const Napi::Value &value);

    Napi::Value GetVolume(const Napi::CallbackInfo &info);
    Napi::Value GetAudioPath(const Napi::CallbackInfo &info);
    Napi::Value GetAudioName(const Napi::CallbackInfo &info);
    Napi::Value GetSourceStart(const Napi::CallbackInfo &info);
    Napi::Value GetSourceDuration(const Napi::CallbackInfo &info);

    Napi::Value GetAlpha(const Napi::CallbackInfo &info);
    void SetAlpha(const Napi::CallbackInfo &info, const Napi::Value &value);
    Napi::Value GetRotation(const Napi::CallbackInfo &info);
    void SetRotation(const Napi::CallbackInfo &info, const Napi::Value &value);
    Napi::Value GetScaleX(const Napi::CallbackInfo &info);
    void SetScaleX(const Napi::CallbackInfo &info, const Napi::Value &value);
    Napi::Value GetScaleY(const Napi::CallbackInfo &info);
    void SetScaleY(const Napi::CallbackInfo &info, const Napi::Value &value);
    Napi::Value GetTransformX(const Napi::CallbackInfo &info);
    void SetTransformX(const Napi::CallbackInfo &info, const Napi::Value &value);
    Napi::Value GetTransformY(const Napi::CallbackInfo &info);
    void SetTransformY(const Napi::CallbackInfo &info, const Napi::Value &value);
};
