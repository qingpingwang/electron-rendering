#pragma once

#include <napi.h>
#include <memory>
#include <cstdint>

namespace vp {
class RootNode;
}

class RootWrap : public Napi::ObjectWrap<RootWrap> {
public:
    static Napi::Function GetClass(Napi::Env env);
    static Napi::Object NewInstance(Napi::Env env);

    RootWrap(const Napi::CallbackInfo &info);
    ~RootWrap() override;

    uint32_t gen() const {
        return gen_;
    }
    vp::RootNode *root() const {
        return root_.get();
    }

private:
    std::unique_ptr<vp::RootNode> root_;
    uint32_t gen_ = 0;

    // 复用的像素缓冲区（V8 管理，避免每帧 malloc/GC）
    Napi::Reference<Napi::ArrayBuffer> pixel_ab_;

    Napi::Value Init(const Napi::CallbackInfo &info);
    Napi::Value Load(const Napi::CallbackInfo &info);
    Napi::Value ExportConfig(const Napi::CallbackInfo &info);
    Napi::Value Unload(const Napi::CallbackInfo &info);
    Napi::Value Cleanup(const Napi::CallbackInfo &info);
    Napi::Value SetCurrentTime(const Napi::CallbackInfo &info);
    Napi::Value IsSameFrame(const Napi::CallbackInfo &info);
    Napi::Value Draw(const Napi::CallbackInfo &info);
    Napi::Value GetGroups(const Napi::CallbackInfo &info);
    Napi::Value GetAudioInfos(const Napi::CallbackInfo &info);

    Napi::Value GetWidth(const Napi::CallbackInfo &info);
    Napi::Value GetHeight(const Napi::CallbackInfo &info);
    Napi::Value GetDurationMs(const Napi::CallbackInfo &info);
    Napi::Value GetFrameRate(const Napi::CallbackInfo &info);
    Napi::Value GetLoaded(const Napi::CallbackInfo &info);
    Napi::Value GetGpuInfo(const Napi::CallbackInfo &info);
    Napi::Value GetId(const Napi::CallbackInfo &info);
};
