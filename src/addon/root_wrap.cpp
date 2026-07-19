#include "root_wrap.h"
#include "group_layer_wrap.h"
#include "layer_wrap.h"
#include "../core/root_node.h"
#include "../gl/functions.h"
#include "../gl/types.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <filesystem>

static Napi::FunctionReference g_constructor;

// setMaterial*Param 会下发 glUniform；经 friend 访问 RootNode::gl_ctx_，不暴露 public API。
bool RootWrap::acquireGL() {
    return root_ && vp::gl::makeCurrent(root_->gl_ctx_);
}

void RootWrap::releaseGL() {
    if (root_)
        vp::gl::releaseCurrent(root_->gl_ctx_);
}

RootWrap::ScopedGLContext::ScopedGLContext(RootWrap *w) : self(w), acquired(w->acquireGL()) {
}

RootWrap::ScopedGLContext::~ScopedGLContext() {
    if (acquired)
        self->releaseGL();
}

Napi::Function RootWrap::GetClass(Napi::Env env) {
    auto cls = DefineClass(env, "Root", {
                                            InstanceMethod("init", &RootWrap::Init),
                                            InstanceMethod("load", &RootWrap::Load),
                                            InstanceMethod("exportConfig", &RootWrap::ExportConfig),
                                            InstanceMethod("unload", &RootWrap::Unload),
                                            InstanceMethod("cleanup", &RootWrap::Cleanup),
                                            InstanceMethod("setCurrentTime", &RootWrap::SetCurrentTime),
                                            InstanceMethod("isSameFrame", &RootWrap::IsSameFrame),
                                            InstanceMethod("draw", &RootWrap::Draw),
                                            InstanceMethod("getGroups", &RootWrap::GetGroups),
                                            InstanceMethod("findLayerById", &RootWrap::FindLayerById),
                                            InstanceMethod("getAudioInfos", &RootWrap::GetAudioInfos),

                                            InstanceMethod("setMaterialFloatParam", &RootWrap::SetMaterialFloatParam),
                                            InstanceMethod("setMaterialVecParam",   &RootWrap::SetMaterialVecParam),
                                            InstanceMethod("setMaterialBoolParam",  &RootWrap::SetMaterialBoolParam),

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

RootWrap::RootWrap(const Napi::CallbackInfo &info) :
    Napi::ObjectWrap<RootWrap>(info),
    root_(std::make_unique<vp::RootNode>()) {
}

RootWrap::~RootWrap() = default;

// ========== Methods ==========

Napi::Value RootWrap::Init(const Napi::CallbackInfo &info) {
    return Napi::Boolean::New(info.Env(), root_->init());
}

Napi::Value RootWrap::Load(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "expected config json string (first arg)").ThrowAsJavaScriptException();
        return env.Null();
    }

    ++gen_;
    // 传入协议文件路径：读取文件、解析 JSON，协议所在目录作为 base_path 传入
    Napi::Object result = Napi::Object::New(env);
    result.Set("success", Napi::Boolean::New(env, false));

    const std::string json_str = info[0].As<Napi::String>().Utf8Value();
    const std::string base_path = info.Length() > 1 ? info[1].As<Napi::String>().Utf8Value() : "";

    try {
        const auto config = nlohmann::json::parse(json_str);

        if (!config.is_object()) {
            result.Set("success", Napi::Boolean::New(env, false));
            result.Set("error", Napi::String::New(env, "config json must be an object"));
            return result;
        }

        bool success = root_->load(config, base_path);
        result.Set("success", Napi::Boolean::New(env, success));
        if (!success) {
            const std::string error = root_->getErrorMessage();
            result.Set("error", Napi::String::New(env, error.empty() ? "load from json failed" : error));
        }
    } catch (const nlohmann::json::parse_error &e) {
        result.Set("error", Napi::String::New(env, e.what()));
    } catch (const nlohmann::detail::type_error &e) {
        result.Set("error", Napi::String::New(env, e.what()));
    } catch (const std::exception &e) {
        result.Set("error", Napi::String::New(env, e.what()));
    } catch (...) {
        result.Set("error", Napi::String::New(env, "unknown error"));
    }
    return result;
}

Napi::Value RootWrap::ExportConfig(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();
    if (!root_->isLoaded())
        return env.Null();

    return Napi::String::New(env, root_->dump().dump(2));
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
    root_->setCurrentTime(static_cast<vp::TimeMs>(info[0].As<Napi::Number>().Int64Value()));
    return env.Undefined();
}

Napi::Value RootWrap::IsSameFrame(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsNumber()) {
        Napi::TypeError::New(env, "expected time in ms").ThrowAsJavaScriptException();
        return env.Null();
    }
    auto time_ms = static_cast<vp::TimeMs>(info[0].As<Napi::Number>().Int64Value());
    return Napi::Boolean::New(env, root_->isSameFrame(time_ms));
}

Napi::Value RootWrap::Draw(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();
    if (!root_->isLoaded())
        return env.Null();

    size_t size = static_cast<size_t>(root_->getWidth()) * root_->getHeight() * 4;
    if (size == 0)
        return env.Null();

    if (pixel_ab_.IsEmpty() || pixel_ab_.Value().ByteLength() != size)
        pixel_ab_ = Napi::Persistent(Napi::ArrayBuffer::New(env, size));

    auto ab = pixel_ab_.Value();
    bool force = info.Length() > 0 && info[0].IsBoolean() && info[0].As<Napi::Boolean>().Value();
    bool prepare_next = !(info.Length() > 1 && info[1].IsBoolean() && !info[1].As<Napi::Boolean>().Value());
    int status = root_->draw(static_cast<uint8_t *>(ab.Data()), size, force, prepare_next);

    // status == -1：参数错误（size 校验已在上面拦掉，这里理论不会出现）。
    // status == -2：真的渲染失败了，buffer 内容不可信，不能当正常帧交给前端。
    if (status == -1)
        return env.Null();

    Napi::Object result = Napi::Object::New(env);
    result.Set("status", Napi::Number::New(env, status));
    if (status == -2) {
        const std::string error = root_->getErrorMessage();
        result.Set("error", Napi::String::New(env, error.empty() ? "draw failed" : error));
        return result;
    }
    result.Set("pixels", Napi::Uint8Array::New(env, size, ab, 0));
    return result;
}

Napi::Value RootWrap::GetGroups(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();
    const auto &groups = root_->getGroups();

    Napi::Array arr = Napi::Array::New(env, groups.size());
    Napi::Object self = info.This().As<Napi::Object>();

    for (size_t i = 0; i < groups.size(); ++i)
        arr.Set(static_cast<uint32_t>(i),
                GroupLayerWrap::NewInstance(env, groups[i].get(), self, gen_));

    return arr;
}

Napi::Value RootWrap::FindLayerById(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "expected layer id string").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    if (!root_->isLoaded()) {
        Napi::Error::New(env, "项目未加载").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    const std::string id = info[0].As<Napi::String>().Utf8Value();
    Napi::Object self = info.This().As<Napi::Object>();
    vp::Layer *layer = root_->findLayerById(id);
    if (!layer) {
        Napi::Error::New(env, "未找到 id 为 \"" + id + "\" 的图层").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    return LayerWrap::NewInstance(env, layer, self, gen_);
}

static Napi::Value jsonToNapi(Napi::Env env, const nlohmann::json &j) {
    if (j.is_object()) {
        Napi::Object obj = Napi::Object::New(env);
        for (auto &[key, val] : j.items())
            obj.Set(key, jsonToNapi(env, val));
        return obj;
    }
    if (j.is_string()) return Napi::String::New(env, j.get<std::string>());
    if (j.is_number_float()) return Napi::Number::New(env, j.get<double>());
    if (j.is_number_unsigned()) return Napi::Number::New(env, static_cast<double>(j.get<uint64_t>()));
    if (j.is_number_integer()) return Napi::Number::New(env, static_cast<double>(j.get<int64_t>()));
    if (j.is_boolean()) return Napi::Boolean::New(env, j.get<bool>());
    if (j.is_array()) {
        Napi::Array arr = Napi::Array::New(env, j.size());
        for (size_t i = 0; i < j.size(); ++i)
            arr.Set(static_cast<uint32_t>(i), jsonToNapi(env, j[i]));
        return arr;
    }
    return env.Null();
}

Napi::Value RootWrap::GetAudioInfos(const Napi::CallbackInfo &info) {
    return jsonToNapi(info.Env(), root_->getAudioInfos());
}

// ========== Material Param Setters ==========

// setMaterialFloatParam(materialId: string, name: string, value: number) → boolean
Napi::Value RootWrap::SetMaterialFloatParam(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();
    if (info.Length() < 3 || !info[0].IsString() || !info[1].IsString() || !info[2].IsNumber()) {
        Napi::TypeError::New(env, "expected (materialId: string, name: string, value: number)").ThrowAsJavaScriptException();
        return env.Null();
    }
    ScopedGLContext ctx_guard(this);
    if (!ctx_guard.acquired)
        return Napi::Boolean::New(env, false);
    bool ok = root_->setMaterialFloatParam(
        info[0].As<Napi::String>().Utf8Value(),
        info[1].As<Napi::String>().Utf8Value(),
        info[2].As<Napi::Number>().FloatValue());
    return Napi::Boolean::New(env, ok);
}

// setMaterialVecParam(materialId: string, name: string, values: number[]) → boolean
Napi::Value RootWrap::SetMaterialVecParam(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();
    if (info.Length() < 3 || !info[0].IsString() || !info[1].IsString() || !info[2].IsArray()) {
        Napi::TypeError::New(env, "expected (materialId: string, name: string, values: number[])").ThrowAsJavaScriptException();
        return env.Null();
    }
    auto arr = info[2].As<Napi::Array>();
    std::vector<float> values;
    values.reserve(arr.Length());
    for (uint32_t i = 0; i < arr.Length(); ++i)
        values.push_back(arr.Get(i).As<Napi::Number>().FloatValue());
    ScopedGLContext ctx_guard(this);
    if (!ctx_guard.acquired)
        return Napi::Boolean::New(env, false);
    bool ok = root_->setMaterialVecParam(
        info[0].As<Napi::String>().Utf8Value(),
        info[1].As<Napi::String>().Utf8Value(),
        values);
    return Napi::Boolean::New(env, ok);
}

// setMaterialBoolParam(materialId: string, name: string, value: boolean) → boolean
Napi::Value RootWrap::SetMaterialBoolParam(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();
    if (info.Length() < 3 || !info[0].IsString() || !info[1].IsString() || !info[2].IsBoolean()) {
        Napi::TypeError::New(env, "expected (materialId: string, name: string, value: boolean)").ThrowAsJavaScriptException();
        return env.Null();
    }
    ScopedGLContext ctx_guard(this);
    if (!ctx_guard.acquired)
        return Napi::Boolean::New(env, false);
    bool ok = root_->setMaterialBoolParam(
        info[0].As<Napi::String>().Utf8Value(),
        info[1].As<Napi::String>().Utf8Value(),
        info[2].As<Napi::Boolean>().Value());
    return Napi::Boolean::New(env, ok);
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
