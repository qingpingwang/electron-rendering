#pragma once

#include <napi.h>
#include <memory>
#include <cstdint>
#include "core/root_node.h"
#include <array>
#include <thread>
#include <cmath>
#include <cstring>
#include <mutex>
#include <stdexcept>

// N-API frame handoff state: two CPU buffers and serialized SDK access.
// Every pixel, layer, material and hit test is produced by nle_sdk::RootNode.
class NativeRootState {
    nle_sdk::RootNode sdk_;
    struct Frame {
        std::vector<uint8_t> bytes;
        nle_sdk::TimeMs time = -1;
        bool ok = false;
    };
    std::array<Frame, 2> frames_;
    size_t write_ = 0;
    std::thread prepare_;
    nle_sdk::TimeMs current_ = 0, displayed_ = -1;
    std::string error_;
    bool initialized_ = false;
    void join() {
        if (prepare_.joinable()) {
            prepare_.join();
        }
    }
    bool same(nle_sdk::TimeMs a, nle_sdk::TimeMs b) const {
        return a >= 0 && b >= 0 && std::abs(a - b) < 500.0 / (sdk_.getFrameRate() > 0 ? sdk_.getFrameRate() : 30);
    }
    void clearCache() {
        for (auto &f : frames_) {
            f.time = -1;
        }
    }
    Frame &render(nle_sdk::TimeMs time) {
        auto &f = frames_[write_];
        write_ = (write_ + 1) % frames_.size();
        f.time = -1;
        f.ok = false;
        try {
            if (!sdk_.makeCurrent()) {
                throw std::runtime_error("SDK GL context unavailable");
            }
            f.bytes.resize(static_cast<size_t>(sdk_.getWidth()) * sdk_.getHeight() * 4);
            sdk_.setCurrentTime(time);
            f.ok = sdk_.draw(f.bytes.data(), f.bytes.size());
            if (!f.ok) {
                error_ = sdk_.getErrorMessage();
            }
            f.time = time;
        } catch (const std::exception &e) {
            error_ = e.what();
        }
        sdk_.releaseCurrent();
        return f;
    }

public:
    NativeRootState() = default;
    ~NativeRootState() {
        join();
        sdk_.makeCurrent();
    }
    bool init();
    // Serializes existing synchronous property access against the prefetch thread.
    nle_sdk::RootNode *access() {
        join();
        clearCache();
        if (initialized_ && !sdk_.makeCurrent()) {
            throw std::runtime_error("SDK GL context unavailable");
        }
        sdk_.setCurrentTime(current_);
        return &sdk_;
    }
    bool load(const nlohmann::json &config, const std::string &base);
    void unload() {
        join();
        clearCache();
        if (initialized_) {
            sdk_.makeCurrent();
            sdk_.unload();
            sdk_.releaseCurrent();
        }
        current_ = 0;
        displayed_ = -1;
    }
    void cleanup() {
        join();
        if (initialized_) {
            sdk_.makeCurrent();
            sdk_.cleanup();
            sdk_.releaseCurrent();
            initialized_ = false;
        }
        clearCache();
    }
    void setCurrentTime(nle_sdk::TimeMs t) {
        current_ = t;
    }
    nle_sdk::TimeMs getCurrentTime() const {
        return current_;
    }
    bool isSameFrame(nle_sdk::TimeMs t) const {
        return same(t, displayed_);
    }
    int draw(uint8_t *out, size_t size, bool force, bool next) {
        join();
        const size_t required = static_cast<size_t>(getWidth()) * getHeight() * 4;
        if (!isLoaded() || !out || size < required) {
            return -1;
        }
        if (force) {
            clearCache();
        }
        Frame *frame = nullptr;
        for (auto &f : frames_) {
            if (same(f.time, current_) && f.ok) {
                frame = &f;
                break;
            }
        }
        const bool hit = frame;
        if (!frame) {
            frame = &render(current_);
        }
        const bool ok = frame->ok;
        if (ok) {
            std::memcpy(out, frame->bytes.data(), required);
            displayed_ = current_;
        }
        // Release even after property access/cache hit, before moving context to another thread.
        sdk_.releaseCurrent();
        if (ok && next) {
            const auto time = current_ + static_cast<nle_sdk::TimeMs>(std::llround(1000.0 / (getFrameRate() > 0 ? getFrameRate() : 30)));
            if (time < getDurationMs()) {
                prepare_ = std::thread([this, time] { render(time); });
            }
        }
        return ok ? (hit ? 0 : 1) : -2;
    }
    int getWidth() const {
        return sdk_.getWidth();
    }
    int getHeight() const {
        return sdk_.getHeight();
    }
    nle_sdk::TimeMs getDurationMs() const {
        return sdk_.getDurationMs();
    }
    double getFrameRate() const {
        return sdk_.getFrameRate();
    }
    const std::string &getId() const {
        return sdk_.getId();
    }
    bool isLoaded() const {
        return sdk_.isLoaded();
    }
    std::string getGPUInfo() {
        return access()->getGPUInfo();
    }
    std::string getErrorMessage() {
        join();
        return error_.empty() ? sdk_.getErrorMessage() : error_;
    }
    nlohmann::json dump() {
        return access()->dump();
    }
    auto getGroups() {
        return access()->getGroups();
    }
    auto findLayerById(const std::string &id) {
        return access()->findLayerById(id);
    }
    auto getAudioInfos() {
        return access()->getAudioInfos();
    }
    bool setMaterialFloatParam(const std::string &id, const std::string &name, float value) {
        return access()->setMaterialParameter(id, name, value);
    }
    bool setMaterialVecParam(const std::string &id, const std::string &name, const std::vector<float> &value) {
        return access()->setMaterialParameter(id, name, value);
    }
    bool setMaterialBoolParam(const std::string &id, const std::string &name, bool value) {
        return access()->setMaterialParameter(id, name, value);
    }
    auto hitTestJson(float x, float y) {
        return access()->hitTestJson(x, y);
    }
    auto getLayerBoundingBoxJson(const std::string &id) {
        return access()->getLayerBoundingBoxJson(id);
    }
};

namespace nle_sdk {
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
    NativeRootState *root() const {
        root_->access();
        return root_.get();
    }

private:
    std::unique_ptr<NativeRootState> root_;
    uint32_t gen_ = 0;

    // 复用的像素缓冲区（V8 管理，避免每帧 malloc/GC）
    Napi::Reference<Napi::ArrayBuffer> pixel_ab_;

    // 通过 SDK 公共接口获取和释放上下文。
    bool acquireGL();
    void releaseGL();
    struct ScopedGLContext {
        RootWrap *self;
        bool acquired;
        explicit ScopedGLContext(RootWrap *w);
        ~ScopedGLContext();
        ScopedGLContext(const ScopedGLContext &) = delete;
        ScopedGLContext &operator=(const ScopedGLContext &) = delete;
    };

    Napi::Value Init(const Napi::CallbackInfo &info);
    Napi::Value Load(const Napi::CallbackInfo &info);
    Napi::Value ExportConfig(const Napi::CallbackInfo &info);
    Napi::Value Unload(const Napi::CallbackInfo &info);
    Napi::Value Cleanup(const Napi::CallbackInfo &info);
    Napi::Value SetCurrentTime(const Napi::CallbackInfo &info);
    Napi::Value IsSameFrame(const Napi::CallbackInfo &info);
    Napi::Value Draw(const Napi::CallbackInfo &info);
    Napi::Value GetGroups(const Napi::CallbackInfo &info);
    Napi::Value FindLayerById(const Napi::CallbackInfo &info);
    Napi::Value GetAudioInfos(const Napi::CallbackInfo &info);

    // 素材参数控制（特效/转场 uniform）
    Napi::Value SetMaterialFloatParam(const Napi::CallbackInfo &info);
    Napi::Value SetMaterialVecParam(const Napi::CallbackInfo &info);
    Napi::Value SetMaterialBoolParam(const Napi::CallbackInfo &info);

    Napi::Value GetWidth(const Napi::CallbackInfo &info);
    Napi::Value GetHeight(const Napi::CallbackInfo &info);
    Napi::Value GetDurationMs(const Napi::CallbackInfo &info);
    Napi::Value GetFrameRate(const Napi::CallbackInfo &info);
    Napi::Value GetLoaded(const Napi::CallbackInfo &info);
    Napi::Value GetGpuInfo(const Napi::CallbackInfo &info);
    Napi::Value GetId(const Napi::CallbackInfo &info);
};
