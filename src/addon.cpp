#include <napi.h>
#include "addon/root_wrap.h"
#include "addon/group_layer_wrap.h"
#include "addon/layer_wrap.h"
#include "media/moov_helper.h"

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

    nle_sdk::MoovHelper decoder;
    if (!decoder.loadFromFile(video_path) || !decoder.hasVideoTrack()) {
        Napi::Object result = Napi::Object::New(env);
        result.Set("success", Napi::Boolean::New(env, false));
        result.Set("error", Napi::String::New(env, "cannot open video file"));
        return result;
    }

    Napi::Object result = Napi::Object::New(env);
    result.Set("success", Napi::Boolean::New(env, true));
    result.Set("width", Napi::Number::New(env, decoder.width()));
    result.Set("height", Napi::Number::New(env, decoder.height()));
    result.Set("durationUs", Napi::Number::New(env, decoder.durationUs()));
    result.Set("frameRate", Napi::Number::New(env, decoder.frameRate()));
    result.Set("hasAlpha", Napi::Boolean::New(env, false));

    return result;
}

Napi::Object InitModule(Napi::Env env, Napi::Object exports) {
    RootWrap::GetClass(env);
    GroupLayerWrap::GetClass(env);
    LayerWrap::GetClass(env);

    exports.Set("createRoot", Napi::Function::New(env, CreateRoot));
    exports.Set("getVideoInfo", Napi::Function::New(env, GetVideoInfo));
    return exports;
}

NODE_API_MODULE(video_player, InitModule)
