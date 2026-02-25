#pragma once

#include <napi.h>
#include <cstdint>

namespace vp { class Layer; }

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
    Napi::Value GetAlignment(const Napi::CallbackInfo &info);

    Napi::Value GetVideoFrameRate(const Napi::CallbackInfo &info);
    Napi::Value GetVideoLoaded(const Napi::CallbackInfo &info);
};
