#include "layer_wrap.h"
#include "root_wrap.h"
#include "../layer/layer.h"
#include "../layer/video_layer.h"
#include "../material/material.h"

static Napi::FunctionReference g_constructor;

Napi::Function LayerWrap::GetClass(Napi::Env env) {
    auto cls = DefineClass(env, "Layer", {
        InstanceAccessor("name", &LayerWrap::GetName, nullptr),
        InstanceAccessor("type", &LayerWrap::GetType, nullptr),
        InstanceAccessor("startTime", &LayerWrap::GetStartTime, nullptr),
        InstanceAccessor("endTime", &LayerWrap::GetEndTime, nullptr),
        InstanceAccessor("durationMs", &LayerWrap::GetDurationMs, nullptr),
        InstanceAccessor("active", &LayerWrap::GetActive, nullptr),
        InstanceAccessor("text", &LayerWrap::GetText, nullptr),
        InstanceAccessor("alignment", &LayerWrap::GetAlignment, nullptr),
        InstanceAccessor("videoFrameRate", &LayerWrap::GetVideoFrameRate, nullptr),
        InstanceAccessor("videoLoaded", &LayerWrap::GetVideoLoaded, nullptr),
    });

    g_constructor = Napi::Persistent(cls);
    g_constructor.SuppressDestruct();
    return cls;
}

Napi::Object LayerWrap::NewInstance(Napi::Env env, vp::Layer *layer,
                                     Napi::Object root_obj, uint32_t gen) {
    Napi::Object obj = g_constructor.New({});
    auto *wrap = Napi::ObjectWrap<LayerWrap>::Unwrap(obj);
    wrap->layer_ = layer;
    wrap->root_ref_ = Napi::Persistent(root_obj);
    wrap->gen_ = gen;
    return obj;
}

LayerWrap::LayerWrap(const Napi::CallbackInfo &info)
    : Napi::ObjectWrap<LayerWrap>(info) {}

vp::Layer *LayerWrap::getLayer(Napi::Env env) {
    auto *root = RootWrap::Unwrap(root_ref_.Value().As<Napi::Object>());
    if (!root || root->gen() != gen_) {
        Napi::Error::New(env, "layer reference invalidated").ThrowAsJavaScriptException();
        return nullptr;
    }
    return layer_;
}

// ========== Common Getters ==========

Napi::Value LayerWrap::GetName(const Napi::CallbackInfo &info) {
    auto *l = getLayer(info.Env());
    return l ? Napi::String::New(info.Env(), l->getName()) : info.Env().Undefined();
}

Napi::Value LayerWrap::GetType(const Napi::CallbackInfo &info) {
    auto *l = getLayer(info.Env());
    if (!l) return info.Env().Undefined();

    switch (l->getMaterialType()) {
    case vp::MATERIAL_TYPE_TEXT:  return Napi::String::New(info.Env(), "text");
    case vp::MATERIAL_TYPE_VIDEO: return Napi::String::New(info.Env(), "video");
    default:                      return Napi::String::New(info.Env(), "unknown");
    }
}

Napi::Value LayerWrap::GetStartTime(const Napi::CallbackInfo &info) {
    auto *l = getLayer(info.Env());
    return l ? Napi::Number::New(info.Env(), static_cast<double>(l->getStartTime()))
             : info.Env().Undefined();
}

Napi::Value LayerWrap::GetEndTime(const Napi::CallbackInfo &info) {
    auto *l = getLayer(info.Env());
    return l ? Napi::Number::New(info.Env(), static_cast<double>(l->getEndTime()))
             : info.Env().Undefined();
}

Napi::Value LayerWrap::GetDurationMs(const Napi::CallbackInfo &info) {
    auto *l = getLayer(info.Env());
    return l ? Napi::Number::New(info.Env(), static_cast<double>(l->getDurationMs()))
             : info.Env().Undefined();
}

Napi::Value LayerWrap::GetActive(const Napi::CallbackInfo &info) {
    auto *l = getLayer(info.Env());
    return l ? Napi::Boolean::New(info.Env(), l->isActive())
             : info.Env().Undefined();
}

// ========== Text Layer Getters ==========

Napi::Value LayerWrap::GetText(const Napi::CallbackInfo &info) {
    auto *l = getLayer(info.Env());
    if (!l || l->getMaterialType() != vp::MATERIAL_TYPE_TEXT)
        return info.Env().Undefined();

    auto *tm = dynamic_cast<vp::TextMaterial *>(l->getMaterial());
    return tm ? Napi::String::New(info.Env(), tm->getText())
              : info.Env().Undefined();
}

Napi::Value LayerWrap::GetAlignment(const Napi::CallbackInfo &info) {
    auto *l = getLayer(info.Env());
    if (!l || l->getMaterialType() != vp::MATERIAL_TYPE_TEXT)
        return info.Env().Undefined();

    auto *tm = dynamic_cast<vp::TextMaterial *>(l->getMaterial());
    if (!tm) return info.Env().Undefined();

    switch (tm->getAlignment()) {
    case vp::TEXT_ALIGN_CENTER: return Napi::String::New(info.Env(), "center");
    case vp::TEXT_ALIGN_RIGHT:  return Napi::String::New(info.Env(), "right");
    default:                    return Napi::String::New(info.Env(), "left");
    }
}

// ========== Video Layer Getters ==========

Napi::Value LayerWrap::GetVideoFrameRate(const Napi::CallbackInfo &info) {
    auto *l = getLayer(info.Env());
    if (!l || l->getMaterialType() != vp::MATERIAL_TYPE_VIDEO)
        return info.Env().Undefined();

    auto *vl = dynamic_cast<vp::VideoLayer *>(l);
    return vl ? Napi::Number::New(info.Env(), vl->getFrameRate())
              : info.Env().Undefined();
}

Napi::Value LayerWrap::GetVideoLoaded(const Napi::CallbackInfo &info) {
    auto *l = getLayer(info.Env());
    if (!l || l->getMaterialType() != vp::MATERIAL_TYPE_VIDEO)
        return info.Env().Undefined();

    auto *vl = dynamic_cast<vp::VideoLayer *>(l);
    return vl ? Napi::Boolean::New(info.Env(), vl->isLoaded())
              : info.Env().Undefined();
}
