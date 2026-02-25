#include <napi.h>
#include "addon/root_wrap.h"
#include "addon/layer_wrap.h"
#include "decoder/video_decoder.h"

static Napi::Value CreateRoot(const Napi::CallbackInfo &info) {
    return RootWrap::NewInstance(info.Env());
}

static Napi::Value GetVideoInfo(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "expected video file path").ThrowAsJavaScriptException();
        return env.Null();
    }

    std::string video_path = info[0].As<Napi::String>().Utf8Value();

    vp::VideoDecoder decoder;
    if (!decoder.open(video_path)) {
        Napi::Object result = Napi::Object::New(env);
        result.Set("success", Napi::Boolean::New(env, false));
        result.Set("error", Napi::String::New(env, "cannot open video file"));
        return result;
    }

    Napi::Object result = Napi::Object::New(env);
    result.Set("success", Napi::Boolean::New(env, true));
    result.Set("width", Napi::Number::New(env, decoder.getWidth()));
    result.Set("height", Napi::Number::New(env, decoder.getHeight()));
    result.Set("durationMs", Napi::Number::New(env, decoder.getDurationMs()));
    result.Set("frameRate", Napi::Number::New(env, decoder.getFrameRate()));
    result.Set("hasAlpha", Napi::Boolean::New(env, decoder.hasAlpha()));

    return result;
}

Napi::Object InitModule(Napi::Env env, Napi::Object exports) {
    RootWrap::GetClass(env);
    LayerWrap::GetClass(env);

    exports.Set("createRoot", Napi::Function::New(env, CreateRoot));
    exports.Set("getVideoInfo", Napi::Function::New(env, GetVideoInfo));
    return exports;
}

NODE_API_MODULE(video_player, InitModule)
