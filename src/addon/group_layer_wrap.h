#pragma once

#include <napi.h>
#include <cstdint>

namespace vp { class GroupLayer; }

class GroupLayerWrap : public Napi::ObjectWrap<GroupLayerWrap> {
public:
    static Napi::Function GetClass(Napi::Env env);
    static Napi::Object NewInstance(Napi::Env env, vp::GroupLayer *group,
                                     Napi::Object root_obj, uint32_t gen);

    GroupLayerWrap(const Napi::CallbackInfo &info);

private:
    vp::GroupLayer *group_ = nullptr;
    Napi::ObjectReference root_ref_;
    uint32_t gen_ = 0;

    vp::GroupLayer *getGroup(Napi::Env env);

    Napi::Value GetId(const Napi::CallbackInfo &info);
    Napi::Value GetType(const Napi::CallbackInfo &info);

    Napi::Value GetVisible(const Napi::CallbackInfo &info);
    void SetVisible(const Napi::CallbackInfo &info, const Napi::Value &value);

    Napi::Value GetMuted(const Napi::CallbackInfo &info);
    void SetMuted(const Napi::CallbackInfo &info, const Napi::Value &value);

    Napi::Value GetLayers(const Napi::CallbackInfo &info);
};
