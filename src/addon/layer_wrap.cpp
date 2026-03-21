#include "layer_wrap.h"
#include "root_wrap.h"
#include "../core/root_node.h"
#include "../layer/base/layer.h"
#include "../layer/video/video_layer.h"
#include "../layer/material/material.h"

static Napi::FunctionReference g_constructor;

Napi::Function LayerWrap::GetClass(Napi::Env env) {
    auto cls = DefineClass(env, "Layer", {
                                             InstanceAccessor("id", &LayerWrap::GetId, nullptr),
                                             InstanceAccessor("type", &LayerWrap::GetType, nullptr),
                                             InstanceAccessor("startTime", &LayerWrap::GetStartTime, nullptr),
                                             InstanceAccessor("endTime", &LayerWrap::GetEndTime, nullptr),
                                             InstanceAccessor("durationMs", &LayerWrap::GetDurationMs, nullptr),
                                             InstanceAccessor("active", &LayerWrap::GetActive, nullptr),
                                             InstanceAccessor("text", &LayerWrap::GetText, &LayerWrap::SetText),
                                             InstanceAccessor("alignment", &LayerWrap::GetAlignment, &LayerWrap::SetAlignment),
                                             InstanceAccessor("styleRunCount", &LayerWrap::GetStyleRunCount, nullptr),
                                             InstanceMethod("getStyleRun", &LayerWrap::JsGetStyleRun),
                                             InstanceMethod("setStyleRunFontSize", &LayerWrap::JsSetStyleRunFontSize),
                                             InstanceMethod("setStyleRunLetterSpacing", &LayerWrap::JsSetStyleRunLetterSpacing),
                                             InstanceMethod("setStyleRunLineHeight", &LayerWrap::JsSetStyleRunLineHeight),
                                             InstanceMethod("setStyleRunFill", &LayerWrap::JsSetStyleRunFill),
                                             InstanceMethod("setStyleRunStrokeWidth", &LayerWrap::JsSetStyleRunStrokeWidth),
                                             InstanceMethod("setStyleRunStrokeColor", &LayerWrap::JsSetStyleRunStrokeColor),
                                             InstanceAccessor("videoFrameRate", &LayerWrap::GetVideoFrameRate, nullptr),
                                             InstanceAccessor("videoLoaded", &LayerWrap::GetVideoLoaded, nullptr),
                                             InstanceAccessor("visible", &LayerWrap::GetVisible, &LayerWrap::SetVisible),
                                             InstanceAccessor("muted", &LayerWrap::GetMuted, &LayerWrap::SetMuted),
                                             InstanceAccessor("volume", &LayerWrap::GetVolume, nullptr),
                                             InstanceAccessor("audioPath", &LayerWrap::GetAudioPath, nullptr),
                                             InstanceAccessor("audioName", &LayerWrap::GetAudioName, nullptr),
                                             InstanceAccessor("sourceStart", &LayerWrap::GetSourceStart, nullptr),
                                             InstanceAccessor("sourceDuration", &LayerWrap::GetSourceDuration, nullptr),
                                             InstanceAccessor("alpha", &LayerWrap::GetAlpha, &LayerWrap::SetAlpha),
                                             InstanceAccessor("rotation", &LayerWrap::GetRotation, &LayerWrap::SetRotation),
                                             InstanceAccessor("scaleX", &LayerWrap::GetScaleX, &LayerWrap::SetScaleX),
                                             InstanceAccessor("scaleY", &LayerWrap::GetScaleY, &LayerWrap::SetScaleY),
                                             InstanceAccessor("transformX", &LayerWrap::GetTransformX, &LayerWrap::SetTransformX),
                                             InstanceAccessor("transformY", &LayerWrap::GetTransformY, &LayerWrap::SetTransformY),
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

LayerWrap::LayerWrap(const Napi::CallbackInfo &info) :
    Napi::ObjectWrap<LayerWrap>(info) {
}

vp::Layer *LayerWrap::getLayer(Napi::Env env) {
    auto *root = RootWrap::Unwrap(root_ref_.Value().As<Napi::Object>());
    if (!root || root->gen() != gen_) {
        Napi::Error::New(env, "layer reference invalidated").ThrowAsJavaScriptException();
        return nullptr;
    }
    return layer_;
}

// ========== Common Getters ==========

Napi::Value LayerWrap::GetId(const Napi::CallbackInfo &info) {
    auto *l = getLayer(info.Env());
    return l ? Napi::String::New(info.Env(), l->getId()) : info.Env().Undefined();
}

Napi::Value LayerWrap::GetType(const Napi::CallbackInfo &info) {
    auto *l = getLayer(info.Env());
    if (!l) return info.Env().Undefined();

    switch (l->getMaterialType()) {
    case vp::MATERIAL_TYPE_TEXT: return Napi::String::New(info.Env(), "text");
    case vp::MATERIAL_TYPE_VIDEO: return Napi::String::New(info.Env(), "video");
    case vp::MATERIAL_TYPE_AUDIO: return Napi::String::New(info.Env(), "audio");
    default: return Napi::String::New(info.Env(), "unknown");
    }
}

Napi::Value LayerWrap::GetStartTime(const Napi::CallbackInfo &info) {
    auto *l = getLayer(info.Env());
    return l ? Napi::Number::New(info.Env(), static_cast<double>(l->getStartTime())) : info.Env().Undefined();
}

Napi::Value LayerWrap::GetEndTime(const Napi::CallbackInfo &info) {
    auto *l = getLayer(info.Env());
    return l ? Napi::Number::New(info.Env(), static_cast<double>(l->getEndTime())) : info.Env().Undefined();
}

Napi::Value LayerWrap::GetDurationMs(const Napi::CallbackInfo &info) {
    auto *l = getLayer(info.Env());
    return l ? Napi::Number::New(info.Env(), static_cast<double>(l->getDurationMs())) : info.Env().Undefined();
}

Napi::Value LayerWrap::GetActive(const Napi::CallbackInfo &info) {
    auto *l = getLayer(info.Env());
    if (!l) return info.Env().Undefined();
    auto *rw = RootWrap::Unwrap(root_ref_.Value().As<Napi::Object>());
    return Napi::Boolean::New(info.Env(), l->isActive(rw->root()->getCurrentTime()));
}

// ========== Text Layer Getters ==========

Napi::Value LayerWrap::GetText(const Napi::CallbackInfo &info) {
    auto *l = getLayer(info.Env());
    if (!l || l->getMaterialType() != vp::MATERIAL_TYPE_TEXT)
        return info.Env().Undefined();

    auto *tm = dynamic_cast<vp::TextMaterial *>(l->getMaterial());
    return tm ? Napi::String::New(info.Env(), tm->getText()) : info.Env().Undefined();
}

Napi::Value LayerWrap::GetAlignment(const Napi::CallbackInfo &info) {
    auto *l = getLayer(info.Env());
    if (!l || l->getMaterialType() != vp::MATERIAL_TYPE_TEXT)
        return info.Env().Undefined();

    auto *tm = dynamic_cast<vp::TextMaterial *>(l->getMaterial());
    if (!tm) return info.Env().Undefined();

    switch (tm->getAlignment()) {
    case vp::TEXT_ALIGN_CENTER: return Napi::String::New(info.Env(), "center");
    case vp::TEXT_ALIGN_RIGHT: return Napi::String::New(info.Env(), "right");
    default: return Napi::String::New(info.Env(), "left");
    }
}

// ========== Video Layer Getters ==========

Napi::Value LayerWrap::GetVideoFrameRate(const Napi::CallbackInfo &info) {
    auto *l = getLayer(info.Env());
    if (!l || l->getMaterialType() != vp::MATERIAL_TYPE_VIDEO)
        return info.Env().Undefined();

    auto *vl = dynamic_cast<vp::VideoLayer *>(l);
    return vl ? Napi::Number::New(info.Env(), vl->getFrameRate()) : info.Env().Undefined();
}

Napi::Value LayerWrap::GetVideoLoaded(const Napi::CallbackInfo &info) {
    auto *l = getLayer(info.Env());
    if (!l || l->getMaterialType() != vp::MATERIAL_TYPE_VIDEO)
        return info.Env().Undefined();

    auto *vl = dynamic_cast<vp::VideoLayer *>(l);
    return vl ? Napi::Boolean::New(info.Env(), vl->isLoaded()) : info.Env().Undefined();
}

// ========== Visible (read/write) ==========

Napi::Value LayerWrap::GetVisible(const Napi::CallbackInfo &info) {
    auto *l = getLayer(info.Env());
    return l ? Napi::Boolean::New(info.Env(), l->isVisible()) : info.Env().Undefined();
}

void LayerWrap::SetVisible(const Napi::CallbackInfo &info, const Napi::Value &value) {
    auto *l = getLayer(info.Env());
    if (l && value.IsBoolean())
        l->setVisible(value.As<Napi::Boolean>().Value());
}

// ========== Muted (read/write) ==========

Napi::Value LayerWrap::GetMuted(const Napi::CallbackInfo &info) {
    auto *l = getLayer(info.Env());
    return l ? Napi::Boolean::New(info.Env(), l->isMuted()) : info.Env().Undefined();
}

void LayerWrap::SetMuted(const Napi::CallbackInfo &info, const Napi::Value &value) {
    auto *l = getLayer(info.Env());
    if (l && value.IsBoolean())
        l->setMuted(value.As<Napi::Boolean>().Value());
}

// ========== Common Getters (all layers) ==========

Napi::Value LayerWrap::GetVolume(const Napi::CallbackInfo &info) {
    auto *l = getLayer(info.Env());
    return l ? Napi::Number::New(info.Env(), l->getVolume()) : info.Env().Undefined();
}

// ========== Audio Layer Getters ==========

Napi::Value LayerWrap::GetAudioPath(const Napi::CallbackInfo &info) {
    auto *l = getLayer(info.Env());
    if (!l || l->getMaterialType() != vp::MATERIAL_TYPE_AUDIO)
        return info.Env().Undefined();

    auto *mat = dynamic_cast<vp::AudioMaterial *>(l->getMaterial());
    return mat ? Napi::String::New(info.Env(), mat->getPath()) : info.Env().Undefined();
}

Napi::Value LayerWrap::GetAudioName(const Napi::CallbackInfo &info) {
    auto *l = getLayer(info.Env());
    if (!l || l->getMaterialType() != vp::MATERIAL_TYPE_AUDIO)
        return info.Env().Undefined();

    auto *mat = dynamic_cast<vp::AudioMaterial *>(l->getMaterial());
    return mat ? Napi::String::New(info.Env(), mat->getName()) : info.Env().Undefined();
}

Napi::Value LayerWrap::GetSourceStart(const Napi::CallbackInfo &info) {
    auto *l = getLayer(info.Env());
    return l ? Napi::Number::New(info.Env(), static_cast<double>(l->getSourceRange().start)) : info.Env().Undefined();
}

Napi::Value LayerWrap::GetSourceDuration(const Napi::CallbackInfo &info) {
    auto *l = getLayer(info.Env());
    return l ? Napi::Number::New(info.Env(), static_cast<double>(l->getSourceRange().duration)) : info.Env().Undefined();
}

// ========== Clip Properties (read/write) ==========

#define LAYER_FLOAT_ACCESSOR(Name, getter, setter)                          \
    Napi::Value LayerWrap::Get##Name(const Napi::CallbackInfo &info) {      \
        auto *l = getLayer(info.Env());                                     \
        return l ? Napi::Number::New(info.Env(), l->getter()) : info.Env().Undefined(); \
    }                                                                       \
    void LayerWrap::Set##Name(const Napi::CallbackInfo &info, const Napi::Value &value) { \
        auto *l = getLayer(info.Env());                                     \
        if (l && value.IsNumber())                                          \
            l->setter(static_cast<float>(value.As<Napi::Number>().FloatValue())); \
    }

LAYER_FLOAT_ACCESSOR(Alpha, getAlpha, setAlpha)
LAYER_FLOAT_ACCESSOR(Rotation, getRotation, setRotation)
LAYER_FLOAT_ACCESSOR(ScaleX, getScaleX, setScaleX)
LAYER_FLOAT_ACCESSOR(ScaleY, getScaleY, setScaleY)
LAYER_FLOAT_ACCESSOR(TransformX, getTransformX, setTransformX)
LAYER_FLOAT_ACCESSOR(TransformY, getTransformY, setTransformY)

#undef LAYER_FLOAT_ACCESSOR

// ========== Text Setters ==========

void LayerWrap::SetText(const Napi::CallbackInfo &info, const Napi::Value &value) {
    auto *l = getLayer(info.Env());
    if (!l || l->getMaterialType() != vp::MATERIAL_TYPE_TEXT || !value.IsString()) return;
    auto *tm = dynamic_cast<vp::TextMaterial *>(l->getMaterial());
    if (tm) tm->setText(value.As<Napi::String>().Utf8Value());
}

void LayerWrap::SetAlignment(const Napi::CallbackInfo &info, const Napi::Value &value) {
    auto *l = getLayer(info.Env());
    if (!l || l->getMaterialType() != vp::MATERIAL_TYPE_TEXT || !value.IsString()) return;
    auto *tm = dynamic_cast<vp::TextMaterial *>(l->getMaterial());
    if (!tm) return;
    std::string a = value.As<Napi::String>().Utf8Value();
    if (a == "center") tm->setAlignment(vp::TEXT_ALIGN_CENTER);
    else if (a == "right") tm->setAlignment(vp::TEXT_ALIGN_RIGHT);
    else tm->setAlignment(vp::TEXT_ALIGN_LEFT);
}

// ========== Style Run Accessors ==========

Napi::Value LayerWrap::GetStyleRunCount(const Napi::CallbackInfo &info) {
    auto *l = getLayer(info.Env());
    if (!l || l->getMaterialType() != vp::MATERIAL_TYPE_TEXT)
        return Napi::Number::New(info.Env(), 0);
    auto *tm = dynamic_cast<vp::TextMaterial *>(l->getMaterial());
    return Napi::Number::New(info.Env(), tm ? static_cast<double>(tm->getRunCount()) : 0);
}

Napi::Value LayerWrap::JsGetStyleRun(const Napi::CallbackInfo &info) {
    auto env = info.Env();
    auto *l = getLayer(env);
    if (!l || l->getMaterialType() != vp::MATERIAL_TYPE_TEXT || info.Length() < 1 || !info[0].IsNumber())
        return env.Undefined();

    auto *tm = dynamic_cast<vp::TextMaterial *>(l->getMaterial());
    if (!tm) return env.Undefined();

    size_t idx = static_cast<size_t>(info[0].As<Napi::Number>().Uint32Value());
    const auto &runs = tm->getStyleRuns();
    if (idx >= runs.size()) return env.Undefined();

    const auto &r = runs[idx];
    auto obj = Napi::Object::New(env);
    obj.Set("rangeStart", Napi::Number::New(env, r.range_start));
    obj.Set("rangeEnd", Napi::Number::New(env, r.range_end));
    obj.Set("fontSize", Napi::Number::New(env, r.font_size));
    obj.Set("letterSpacing", Napi::Number::New(env, r.letter_spacing));
    obj.Set("lineHeight", Napi::Number::New(env, r.line_height));

    auto fill = Napi::Object::New(env);
    fill.Set("r", Napi::Number::New(env, r.fill.r));
    fill.Set("g", Napi::Number::New(env, r.fill.g));
    fill.Set("b", Napi::Number::New(env, r.fill.b));
    fill.Set("a", Napi::Number::New(env, r.fill.a));
    obj.Set("fill", fill);

    auto strokeArr = Napi::Array::New(env, r.strokes.size());
    for (size_t i = 0; i < r.strokes.size(); ++i) {
        auto so = Napi::Object::New(env);
        so.Set("width", Napi::Number::New(env, r.strokes[i].width));
        so.Set("r", Napi::Number::New(env, r.strokes[i].color.r));
        so.Set("g", Napi::Number::New(env, r.strokes[i].color.g));
        so.Set("b", Napi::Number::New(env, r.strokes[i].color.b));
        so.Set("a", Napi::Number::New(env, r.strokes[i].color.a));
        strokeArr.Set(i, so);
    }
    obj.Set("strokes", strokeArr);

    return obj;
}

#define TEXT_RUN_SETTER(Name, method)                                              \
    Napi::Value LayerWrap::JsSetStyleRun##Name(const Napi::CallbackInfo &info) {  \
        auto *l = getLayer(info.Env());                                           \
        if (!l || l->getMaterialType() != vp::MATERIAL_TYPE_TEXT                  \
            || info.Length() < 2 || !info[0].IsNumber() || !info[1].IsNumber())   \
            return Napi::Boolean::New(info.Env(), false);                         \
        auto *tm = dynamic_cast<vp::TextMaterial *>(l->getMaterial());            \
        size_t idx = static_cast<size_t>(info[0].As<Napi::Number>().Uint32Value()); \
        bool ok = tm && tm->method(idx, info[1].As<Napi::Number>().FloatValue()); \
        return Napi::Boolean::New(info.Env(), ok);                                \
    }

TEXT_RUN_SETTER(FontSize, setRunFontSize)
TEXT_RUN_SETTER(LetterSpacing, setRunLetterSpacing)
TEXT_RUN_SETTER(LineHeight, setRunLineHeight)

#undef TEXT_RUN_SETTER

Napi::Value LayerWrap::JsSetStyleRunFill(const Napi::CallbackInfo &info) {
    auto env = info.Env();
    auto *l = getLayer(env);
    if (!l || l->getMaterialType() != vp::MATERIAL_TYPE_TEXT || info.Length() < 5)
        return Napi::Boolean::New(env, false);
    auto *tm = dynamic_cast<vp::TextMaterial *>(l->getMaterial());
    if (!tm) return Napi::Boolean::New(env, false);
    size_t idx = static_cast<size_t>(info[0].As<Napi::Number>().Uint32Value());
    bool ok = tm->setRunFill(idx,
        info[1].As<Napi::Number>().FloatValue(),
        info[2].As<Napi::Number>().FloatValue(),
        info[3].As<Napi::Number>().FloatValue(),
        info[4].As<Napi::Number>().FloatValue());
    return Napi::Boolean::New(env, ok);
}

// setStyleRunStrokeWidth(runIdx, strokeIdx, width)
Napi::Value LayerWrap::JsSetStyleRunStrokeWidth(const Napi::CallbackInfo &info) {
    auto env = info.Env();
    auto *l = getLayer(env);
    if (!l || l->getMaterialType() != vp::MATERIAL_TYPE_TEXT || info.Length() < 3)
        return Napi::Boolean::New(env, false);
    auto *tm = dynamic_cast<vp::TextMaterial *>(l->getMaterial());
    if (!tm) return Napi::Boolean::New(env, false);
    size_t ri = static_cast<size_t>(info[0].As<Napi::Number>().Uint32Value());
    size_t si = static_cast<size_t>(info[1].As<Napi::Number>().Uint32Value());
    return Napi::Boolean::New(env, tm->setRunStrokeWidth(ri, si, info[2].As<Napi::Number>().FloatValue()));
}

// setStyleRunStrokeColor(runIdx, strokeIdx, r, g, b, a)
Napi::Value LayerWrap::JsSetStyleRunStrokeColor(const Napi::CallbackInfo &info) {
    auto env = info.Env();
    auto *l = getLayer(env);
    if (!l || l->getMaterialType() != vp::MATERIAL_TYPE_TEXT || info.Length() < 6)
        return Napi::Boolean::New(env, false);
    auto *tm = dynamic_cast<vp::TextMaterial *>(l->getMaterial());
    if (!tm) return Napi::Boolean::New(env, false);
    size_t ri = static_cast<size_t>(info[0].As<Napi::Number>().Uint32Value());
    size_t si = static_cast<size_t>(info[1].As<Napi::Number>().Uint32Value());
    return Napi::Boolean::New(env, tm->setRunStrokeColor(ri, si,
        info[2].As<Napi::Number>().FloatValue(),
        info[3].As<Napi::Number>().FloatValue(),
        info[4].As<Napi::Number>().FloatValue(),
        info[5].As<Napi::Number>().FloatValue()));
}
