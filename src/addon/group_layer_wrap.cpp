#include "group_layer_wrap.h"
#include "layer_wrap.h"
#include "root_wrap.h"
#include "../layer/group/group_layer.h"
#include "../layer/base/layer.h"

static Napi::FunctionReference g_constructor;

Napi::Function GroupLayerWrap::GetClass(Napi::Env env) {
    auto cls = DefineClass(env, "GroupLayer", {
        InstanceAccessor("id", &GroupLayerWrap::GetId, nullptr),
        InstanceAccessor("type", &GroupLayerWrap::GetType, nullptr),
        InstanceAccessor("visible", &GroupLayerWrap::GetVisible, &GroupLayerWrap::SetVisible),
        InstanceAccessor("muted", &GroupLayerWrap::GetMuted, &GroupLayerWrap::SetMuted),
        InstanceAccessor("layers", &GroupLayerWrap::GetLayers, nullptr),
    });

    g_constructor = Napi::Persistent(cls);
    g_constructor.SuppressDestruct();
    return cls;
}

Napi::Object GroupLayerWrap::NewInstance(Napi::Env env, vp::GroupLayer *group,
                                          Napi::Object root_obj, uint32_t gen) {
    Napi::Object obj = g_constructor.New({});
    auto *wrap = Napi::ObjectWrap<GroupLayerWrap>::Unwrap(obj);
    wrap->group_ = group;
    wrap->root_ref_ = Napi::Persistent(root_obj);
    wrap->gen_ = gen;
    return obj;
}

GroupLayerWrap::GroupLayerWrap(const Napi::CallbackInfo &info)
    : Napi::ObjectWrap<GroupLayerWrap>(info) {}

vp::GroupLayer *GroupLayerWrap::getGroup(Napi::Env env) {
    auto *root = RootWrap::Unwrap(root_ref_.Value().As<Napi::Object>());
    if (!root || root->gen() != gen_) {
        Napi::Error::New(env, "group reference invalidated").ThrowAsJavaScriptException();
        return nullptr;
    }
    return group_;
}

Napi::Value GroupLayerWrap::GetId(const Napi::CallbackInfo &info) {
    auto *g = getGroup(info.Env());
    return g ? Napi::String::New(info.Env(), g->getId()) : info.Env().Undefined();
}

Napi::Value GroupLayerWrap::GetType(const Napi::CallbackInfo &info) {
    auto *g = getGroup(info.Env());
    return g ? Napi::String::New(info.Env(), g->getType()) : info.Env().Undefined();
}

Napi::Value GroupLayerWrap::GetVisible(const Napi::CallbackInfo &info) {
    auto *g = getGroup(info.Env());
    return g ? Napi::Boolean::New(info.Env(), g->isVisible()) : info.Env().Undefined();
}

void GroupLayerWrap::SetVisible(const Napi::CallbackInfo &info, const Napi::Value &value) {
    auto *g = getGroup(info.Env());
    if (g && value.IsBoolean())
        g->setVisible(value.As<Napi::Boolean>().Value());
}

Napi::Value GroupLayerWrap::GetMuted(const Napi::CallbackInfo &info) {
    auto *g = getGroup(info.Env());
    return g ? Napi::Boolean::New(info.Env(), g->isMuted()) : info.Env().Undefined();
}

void GroupLayerWrap::SetMuted(const Napi::CallbackInfo &info, const Napi::Value &value) {
    auto *g = getGroup(info.Env());
    if (g && value.IsBoolean())
        g->setMuted(value.As<Napi::Boolean>().Value());
}

Napi::Value GroupLayerWrap::GetLayers(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();
    auto *g = getGroup(env);
    if (!g) return env.Undefined();

    const auto &layers = g->getLayers();
    Napi::Array arr = Napi::Array::New(env, layers.size());
    Napi::Object self_root = root_ref_.Value().As<Napi::Object>();

    for (size_t i = 0; i < layers.size(); ++i)
        arr.Set(static_cast<uint32_t>(i),
                LayerWrap::NewInstance(env, layers[i].get(), self_root, gen_));

    return arr;
}
