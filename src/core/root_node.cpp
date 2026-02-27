#include "root_node.h"
#include "../layer/layer.h"
#include "../layer/video_layer.h"
#include "../layer/text_layer.h"
#include "../gl/shader.h"
#include <algorithm>
#include <cstring>
#include <nlohmann/json.hpp>

#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/gl/GrGLDirectContext.h"
#include "include/gpu/ganesh/gl/GrGLInterface.h"

using json = nlohmann::json;

namespace vp {

static const char commonVertStr[] = {
#include "../gl/shaders/common.vert"
};

static const char commonFragStr[] = {
#include "../gl/shaders/common.frag"
};

RootNode::RootNode() {
}

RootNode::~RootNode() {
    cleanup();
}

bool RootNode::init() {
    if (!gl::initContext(gl_ctx_))
        return false;

    // 在同一个 CGL 上下文上创建 Skia GPU 上下文（用于文字渲染）
    gl::makeCurrent(gl_ctx_);
    auto gl_interface = GrGLMakeNativeInterface();
    if (gl_interface) {
        auto ctx = GrDirectContexts::MakeGL(gl_interface);
        if (ctx) {
            skia_context_ = ctx.release();
        }
    }

    return true;
}

void RootNode::cleanup() {
    cancelPrepare();
    unload();

    gl::destroyQuadMesh(quad_);
    shader_.reset();

    // 清空 FBO 池（会自动清理所有 FBO，包括 render_fbo_）
    fbo_pool_.clear();

    if (skia_context_) {
        skia_context_->abandonContext();
        skia_context_->unref();
        skia_context_ = nullptr;
    }

    gl::destroyContext(gl_ctx_);
}

// ========== 缓存 ==========

TimeMs RootNode::getHalfFrameMs() const {
    return (frame_rate_ > 0) ? static_cast<TimeMs>(500.0 / frame_rate_) : 20;
}

bool RootNode::isCacheHit(TimeMs time_ms) const {
    if (cache_time_ms_ == kInvalidTime)
        return false;
    TimeMs diff = (time_ms >= cache_time_ms_) ? (time_ms - cache_time_ms_) : (cache_time_ms_ - time_ms);
    return diff <= getHalfFrameMs();
}

// ========== 渲染 ==========

bool RootNode::renderFrame(TimeMs time_ms, uint8_t *out_buffer) {
    if (layers_.empty())
        return false;

    gl::makeCurrent(gl_ctx_);

    // 渲染所有图层到 render_fbo_
    gl::bindFBO(render_fbo_);
    gl::cleanColor();

    for (auto &layer : layers_) {
        // 取消标志或绘制失败，直接返回
        if (cancel_flag_ || !layer->draw()) {
            gl::unbindFBO();
            return false;
        }
    }

    gl::unbindFBO();

    if (cancel_flag_)
        return false;

    // 读取像素数据
    if (!gl::readPixels(render_fbo_, out_buffer, static_cast<int>(canvas_.width * canvas_.height * 4)))
        return false;

    return true;
}

void RootNode::startPrepareNextFrame(TimeMs next_time_ms) {
    if (next_time_ms > duration_ms_)
        return;
    if (isCacheHit(next_time_ms))
        return;

    cancelPrepare();

    // 立即设置缓存时间（标记正在准备）
    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        cache_time_ms_ = next_time_ms;
    }

    cancel_flag_ = false;
    preparing_ = true;
    prepare_thread_ = std::thread([this, next_time_ms]() {
        bool success = renderFrame(next_time_ms, cache_data_.data());
        if (!success) {
            std::lock_guard<std::mutex> lock(cache_mutex_);
            cache_time_ms_ = kInvalidTime;
        }
        preparing_ = false;
    });
}

void RootNode::cancelPrepare() {
    cancel_flag_ = true;
    if (prepare_thread_.joinable())
        prepare_thread_.join();
    preparing_ = false;
}

// ========== 加载 ==========

std::string RootNode::loadFromJson(const std::string &json_str) {
    unload();

    try {
        json config = json::parse(json_str);

        // 加载项目基本信息
        id_ = config.value("id", "");
        duration_ms_ = config.value("duration", 0);
        frame_rate_ = config.value("fps", 30.0);

        // 加载 Canvas 配置
        if (!config.contains("canvas_config")) {
            return "canvas_config is required";
        }
        auto &canvas_json = config["canvas_config"];
        canvas_.width = canvas_json.value("width", 0);
        canvas_.height = canvas_json.value("height", 0);
        canvas_.ratio = canvas_json.value("ratio", "");

        // 加载素材
        for (int i = 0; i < MATERIAL_TYPE_COUNT; i++) {
            materials_[i].clear();
        }

        if (config.contains("materials")) {
            const auto &materials_json = config["materials"];

            using Factory = std::unique_ptr<Material> (*)();
            const struct {
                const char *key;
                MaterialType type;
                Factory create;
            } loaders[] = {
                {"videos", MATERIAL_TYPE_VIDEO, []() -> std::unique_ptr<Material> { return std::make_unique<VideoMaterial>(); }},
                {"effects", MATERIAL_TYPE_EFFECT, []() -> std::unique_ptr<Material> { return std::make_unique<EffectMaterial>(); }},
                {"texts", MATERIAL_TYPE_TEXT, []() -> std::unique_ptr<Material> { return std::make_unique<TextMaterial>(); }},
                {"transitions", MATERIAL_TYPE_TRANSITION, []() -> std::unique_ptr<Material> { return std::make_unique<TransitionMaterial>(); }},
            };

            for (const auto &loader : loaders) {
                if (!materials_json.contains(loader.key))
                    continue;
                const auto &arr = materials_json[loader.key];
                materials_[loader.type].reserve(arr.size());
                for (const auto &mat : arr) {
                    auto material = loader.create();
                    if (!material->load(mat))
                        return std::string("load ") + loader.key + " failed: " + material->getErrorMessage();
                    materials_[loader.type].emplace_back(std::move(material));
                }
            }
        }

        if (!config.contains("tracks") || !config["tracks"].is_array())
            return "tracks is required";

        // 创建图层
        for (const auto &track : config["tracks"]) {
            std::string track_type = track.value("type", "");
            if (!track.contains("segments"))
                continue;

            using LayerFactory = std::unique_ptr<Layer> (*)(RootNode *);
            static const struct {
                const char *type;
                LayerFactory create;
            } layer_types[] = {
                {"video", [](RootNode *r) -> std::unique_ptr<Layer> { return std::make_unique<VideoLayer>(r); }},
                {"text", [](RootNode *r) -> std::unique_ptr<Layer> { return std::make_unique<TextLayer>(r); }},
            };

            LayerFactory factory = nullptr;
            for (const auto &lt : layer_types) {
                if (track_type == lt.type) {
                    factory = lt.create;
                    break;
                }
            }
            if (!factory)
                continue;

            for (const auto &segment : track["segments"]) {
                auto layer = factory(this);
                if (!layer->load(segment))
                    return "load layer failed: " + layer->getErrorMessage();
                layers_.emplace_back(std::move(layer));
            }
        }

        if (layers_.empty() || canvas_.width == 0 || canvas_.height == 0) {
            unload();
            return "layers are empty";
        }

        // 创建 OpenGL 资源
        gl::makeCurrent(gl_ctx_);

        // 从 FBO 池获取主渲染 FBO（不释放，生命周期与 RootNode 一致）
        render_fbo_ = fbo_pool_.acquire(canvas_.width, canvas_.height);

        // 创建着色器
        shader_ = std::make_unique<gl::Shader>(commonVertStr, commonFragStr);

        quad_ = gl::createQuadMesh();

        if (!render_fbo_.isValid() || !shader_->isValid() || !quad_.isValid()) {
            unload();
            return "create OpenGL resources failed";
        }

        cache_data_.resize(static_cast<size_t>(canvas_.width) * canvas_.height * 4);
        return "";
    } catch (...) {
        unload();
        return "load from json failed";
    }
}

void RootNode::unload() {
    cancelPrepare();
    layers_.clear();
    cache_data_.clear();
    cache_time_ms_ = kInvalidTime;
    current_time_ms_ = 0;
    id_.clear();
    canvas_ = CanvasConfig{};
    duration_ms_ = 0;
    frame_rate_ = 0.0;

    gl::destroyQuadMesh(quad_);
    shader_.reset();

    // render_fbo_ 由 FBO Pool 管理，归还到池中
    if (render_fbo_.isValid()) {
        fbo_pool_.release(render_fbo_);
    }
}

// ========== 外部接口 ==========

void RootNode::setCurrentTime(TimeMs time_ms) {
    current_time_ms_ = std::clamp(time_ms, TimeMs(0), duration_ms_);
}

bool RootNode::draw(uint8_t *buffer, size_t buffer_size) {
    if (layers_.empty() || !buffer)
        return false;

    size_t required = static_cast<size_t>(canvas_.width) * canvas_.height * 4;
    if (buffer_size < required)
        return false;

    // 检查缓存
    if (isCacheHit(current_time_ms_)) {
        // 命中：等待准备完成，拷贝缓存
        if (prepare_thread_.joinable())
            prepare_thread_.join();
        std::lock_guard<std::mutex> lock(cache_mutex_);
        std::memcpy(buffer, cache_data_.data(), required);
    } else {
        // 未命中：取消异步任务，直接渲染
        cancelPrepare();
        cancel_flag_ = false;
        renderFrame(current_time_ms_, buffer);
    }

    // 启动异步准备下一帧
    TimeMs next_time = current_time_ms_ + static_cast<TimeMs>(1000.0 / (frame_rate_ > 0 ? frame_rate_ : 25.0));
    startPrepareNextFrame(next_time);

    return true;
}

// ========== Getter 方法 ==========

int RootNode::getWidth() const {
    return canvas_.width;
}

int RootNode::getHeight() const {
    return canvas_.height;
}

TimeMs RootNode::getDurationMs() const {
    return duration_ms_;
}

double RootNode::getFrameRate() const {
    return frame_rate_;
}

const std::string &RootNode::getId() const {
    return id_;
}

const CanvasConfig &RootNode::getCanvas() const {
    return canvas_;
}

bool RootNode::isLoaded() const {
    return !layers_.empty();
}

std::string RootNode::getGPUInfo() const {
    return gl::getGPUInfo(gl_ctx_);
}

gl::Shader *RootNode::getShader() const {
    return shader_.get();
}

const gl::QuadMesh *RootNode::getQuad() const {
    return &quad_;
}

TimeMs RootNode::getCurrentTime() const {
    return current_time_ms_;
}

GrDirectContext *RootNode::getSkiaContext() const {
    return skia_context_;
}

gl::FBOPool *RootNode::getFBOPool() {
    return &fbo_pool_;
}

const gl::FBO &RootNode::getRenderFBO() const {
    return render_fbo_;
}

Material *RootNode::getMaterial(MaterialType type, const std::string &material_id) const {
    const auto &materials = getMaterialsByType(type);
    auto it = std::find_if(materials.begin(), materials.end(),
                           [&material_id](const std::unique_ptr<Material> &mat) {
                               return mat->getId() == material_id;
                           });
    return it != materials.end() ? it->get() : nullptr;
}

const std::vector<std::unique_ptr<Material>> &RootNode::getMaterialsByType(MaterialType type) const {
    return materials_[type];
}

const std::vector<std::unique_ptr<Layer>> &RootNode::getLayers() const {
    return layers_;
}

} // namespace vp
