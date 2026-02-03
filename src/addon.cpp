#include <napi.h>
#include "core/root_node.h"
#include <memory>

static std::unique_ptr<vp::RootNode> g_root;

// 初始化
Napi::Value Init(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();
    if (!g_root) {
        g_root = std::make_unique<vp::RootNode>();
    }
    return Napi::Boolean::New(env, g_root->init());
}

// 通过 JSON 字符串加载
Napi::Value Load(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();

    if (!g_root) {
        Napi::Error::New(env, "未初始化").ThrowAsJavaScriptException();
        return env.Null();
    }

    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "需要 JSON 字符串").ThrowAsJavaScriptException();
        return env.Null();
    }

    std::string json_str = info[0].As<Napi::String>().Utf8Value();
    return Napi::Boolean::New(env, g_root->loadFromJson(json_str));
}

// 卸载
Napi::Value Unload(const Napi::CallbackInfo &info) {
    if (g_root)
        g_root->unload();
    return info.Env().Undefined();
}

// 设置当前时间 (毫秒)
Napi::Value SetCurrentTime(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();

    if (!g_root || !g_root->isLoaded()) {
        return env.Undefined();
    }

    if (info.Length() < 1 || !info[0].IsNumber()) {
        Napi::TypeError::New(env, "需要时间参数").ThrowAsJavaScriptException();
        return env.Null();
    }

    int64_t time_ms = info[0].As<Napi::Number>().Int64Value();
    g_root->setCurrentTime(time_ms);
    return env.Undefined();
}

// 绘制并返回像素数据 (零拷贝: glReadPixels 直接写入 JS ArrayBuffer)
Napi::Value Draw(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();

    if (!g_root || !g_root->isLoaded()) {
        return env.Null();
    }

    size_t size = static_cast<size_t>(g_root->getWidth()) * g_root->getHeight() * 4;
    if (size == 0) {
        return env.Null();
    }

    // 创建 JS ArrayBuffer，draw() 内部 glReadPixels 直接写入
    Napi::ArrayBuffer ab = Napi::ArrayBuffer::New(env, size);

    if (!g_root->draw(static_cast<uint8_t *>(ab.Data()), size)) {
        return env.Null();
    }

    return Napi::Uint8Array::New(env, size, ab, 0);
}

// 获取信息
Napi::Value GetInfo(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();
    Napi::Object obj = Napi::Object::New(env);

    if (!g_root) {
        obj.Set("initialized", false);
        return obj;
    }

    obj.Set("initialized", true);
    obj.Set("loaded", g_root->isLoaded());
    obj.Set("id", g_root->getId());
    obj.Set("width", g_root->getWidth());
    obj.Set("height", g_root->getHeight());
    obj.Set("durationMs", g_root->getDurationMs());
    obj.Set("frameRate", g_root->getFrameRate());
    obj.Set("gpu", g_root->getGPUInfo());

    return obj;
}

// 清理
Napi::Value Cleanup(const Napi::CallbackInfo &info) {
    if (g_root) {
        g_root->cleanup();
        g_root.reset();
    }
    return info.Env().Undefined();
}

Napi::Object InitModule(Napi::Env env, Napi::Object exports) {
    exports.Set("init", Napi::Function::New(env, Init));
    exports.Set("load", Napi::Function::New(env, Load));
    exports.Set("unload", Napi::Function::New(env, Unload));
    exports.Set("setCurrentTime", Napi::Function::New(env, SetCurrentTime));
    exports.Set("draw", Napi::Function::New(env, Draw));
    exports.Set("getInfo", Napi::Function::New(env, GetInfo));
    exports.Set("cleanup", Napi::Function::New(env, Cleanup));
    return exports;
}

NODE_API_MODULE(video_player, InitModule)
