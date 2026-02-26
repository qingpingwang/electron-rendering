#include "root_wrap.h"
#include "layer_wrap.h"
#include "../engine/root_node.h"
#include "../layer/layer.h"

#include "../gl/types.h"

static Napi::FunctionReference g_constructor;

Napi::Function RootWrap::GetClass(Napi::Env env) {
    auto cls = DefineClass(env, "Root", {
        InstanceMethod("init", &RootWrap::Init),
        InstanceMethod("load", &RootWrap::Load),
        InstanceMethod("unload", &RootWrap::Unload),
        InstanceMethod("cleanup", &RootWrap::Cleanup),
        InstanceMethod("setCurrentTime", &RootWrap::SetCurrentTime),
        InstanceMethod("draw", &RootWrap::Draw),
        InstanceMethod("getLayers", &RootWrap::GetLayers),

        InstanceAccessor("width", &RootWrap::GetWidth, nullptr),
        InstanceAccessor("height", &RootWrap::GetHeight, nullptr),
        InstanceAccessor("durationMs", &RootWrap::GetDurationMs, nullptr),
        InstanceAccessor("frameRate", &RootWrap::GetFrameRate, nullptr),
        InstanceAccessor("loaded", &RootWrap::GetLoaded, nullptr),
        InstanceAccessor("gpuInfo", &RootWrap::GetGpuInfo, nullptr),
        InstanceAccessor("id", &RootWrap::GetId, nullptr),
    });

    g_constructor = Napi::Persistent(cls);
    g_constructor.SuppressDestruct();
    return cls;
}

Napi::Object RootWrap::NewInstance(Napi::Env env) {
    return g_constructor.New({});
}

RootWrap::RootWrap(const Napi::CallbackInfo &info)
    : Napi::ObjectWrap<RootWrap>(info),
      root_(std::make_unique<vp::RootNode>()) {}

RootWrap::~RootWrap() = default;

// ========== Methods ==========

Napi::Value RootWrap::Init(const Napi::CallbackInfo &info) {
    return Napi::Boolean::New(info.Env(), root_->init());
}

Napi::Value RootWrap::Load(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "expected JSON string").ThrowAsJavaScriptException();
        return env.Null();
    }

    ++gen_;
    std::string error = root_->loadFromJson(info[0].As<Napi::String>().Utf8Value());

    Napi::Object result = Napi::Object::New(env);
    result.Set("success", Napi::Boolean::New(env, error.empty()));
    if (!error.empty())
        result.Set("error", Napi::String::New(env, error));
    return result;
}

Napi::Value RootWrap::Unload(const Napi::CallbackInfo &info) {
    ++gen_;
    root_->unload();
    return info.Env().Undefined();
}

Napi::Value RootWrap::Cleanup(const Napi::CallbackInfo &info) {
    ++gen_;
    root_->cleanup();
    return info.Env().Undefined();
}

Napi::Value RootWrap::SetCurrentTime(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsNumber()) {
        Napi::TypeError::New(env, "expected time in ms").ThrowAsJavaScriptException();
        return env.Null();
    }
    root_->setCurrentTime(info[0].As<Napi::Number>().Int64Value());
    return env.Undefined();
}

Napi::Value RootWrap::Draw(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();
    if (!root_->isLoaded())
        return env.Null();

    size_t size = static_cast<size_t>(root_->getWidth()) * root_->getHeight() * 4;
    if (size == 0)
        return env.Null();

    Napi::ArrayBuffer ab = Napi::ArrayBuffer::New(env, size);
    if (!root_->draw(static_cast<uint8_t *>(ab.Data()), size))
        return env.Null();

    return Napi::Uint8Array::New(env, size, ab, 0);
}

Napi::Value RootWrap::GetLayers(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();
    const auto &layers = root_->getLayers();

    Napi::Array arr = Napi::Array::New(env, layers.size());
    Napi::Object self = info.This().As<Napi::Object>();

    for (size_t i = 0; i < layers.size(); ++i)
        arr.Set(static_cast<uint32_t>(i),
                LayerWrap::NewInstance(env, layers[i].get(), self, gen_));

    return arr;
}

// ========== Getters ==========

Napi::Value RootWrap::GetWidth(const Napi::CallbackInfo &info) {
    return Napi::Number::New(info.Env(), root_->getWidth());
}

Napi::Value RootWrap::GetHeight(const Napi::CallbackInfo &info) {
    return Napi::Number::New(info.Env(), root_->getHeight());
}

Napi::Value RootWrap::GetDurationMs(const Napi::CallbackInfo &info) {
    return Napi::Number::New(info.Env(), static_cast<double>(root_->getDurationMs()));
}

Napi::Value RootWrap::GetFrameRate(const Napi::CallbackInfo &info) {
    return Napi::Number::New(info.Env(), root_->getFrameRate());
}

Napi::Value RootWrap::GetLoaded(const Napi::CallbackInfo &info) {
    return Napi::Boolean::New(info.Env(), root_->isLoaded());
}

Napi::Value RootWrap::GetGpuInfo(const Napi::CallbackInfo &info) {
    return Napi::String::New(info.Env(), root_->getGPUInfo());
}

Napi::Value RootWrap::GetId(const Napi::CallbackInfo &info) {
    return Napi::String::New(info.Env(), root_->getId());
}
