#include "root_node.h"
#include <algorithm>
#include <cstring>
#include <nlohmann/json.hpp>
#include <unordered_map>

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
    return gl::initContext(gl_ctx_);
}

void RootNode::cleanup() {
    cancelPrepare();
    unload();

    gl::destroyQuadMesh(quad_);
    shader_.reset();
    
    // 清空 FBO 池（会自动清理所有 FBO，包括 render_fbo_）
    fbo_pool_.clear();
    
    gl::destroyContext(gl_ctx_);
}

// ========== 缓存 ==========

int64_t RootNode::getHalfFrameMs() const {
    return (frame_rate_ > 0) ? static_cast<int64_t>(500.0 / frame_rate_) : 20;
}

bool RootNode::isCacheHit(int64_t time_ms) const {
    if (cache_time_ms_ < 0)
        return false;
    return std::abs(time_ms - cache_time_ms_) <= getHalfFrameMs();
}

// ========== 渲染 ==========

bool RootNode::renderFrame(int64_t time_ms, uint8_t *out_buffer) {
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

void RootNode::startPrepareNextFrame(int64_t next_time_ms) {
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
            cache_time_ms_ = -1;
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

bool RootNode::loadFromJson(const std::string &json_str) {
    unload();

    try {
        json config = json::parse(json_str);

        // 加载项目基本信息
        id_ = config.value("id", "");
        duration_ms_ = config.value("duration", 0);
        frame_rate_ = config.value("fps", 30.0);

        // 加载 Canvas 配置
        if (config.contains("canvas_config")) {
            auto &canvas_json = config["canvas_config"];
            canvas_.width = canvas_json.value("width", 0);
            canvas_.height = canvas_json.value("height", 0);
            canvas_.ratio = canvas_json.value("ratio", "");
        }

        // 加载素材
        materials_.clear();
        if (config.contains("materials") && config["materials"].contains("videos")) {
            for (const auto &mat : config["materials"]["videos"]) {
                auto material = std::make_unique<VideoMaterial>();
                if (material->load(mat)) {
                    materials_[material->getId()] = std::move(material);
                }
            }
        }

        if (!config.contains("tracks") || !config["tracks"].is_array())
            return false;

        // 创建图层
        for (const auto &track : config["tracks"]) {
            if (track.value("type", "") != "video" || !track.contains("segments"))
                continue;

            for (const auto &segment : track["segments"]) {
                auto layer = std::make_unique<VideoLayer>(this);
                if (!layer->load(segment))
                    continue;

                // 如果未配置画布尺寸，从第一个图层获取
                if (canvas_.width == 0 || canvas_.height == 0) {
                    canvas_.width = layer->getWidth();
                    canvas_.height = layer->getHeight();
                }

                // 如果未配置帧率，从第一个图层获取
                if (frame_rate_ == 0.0) {
                    frame_rate_ = layer->getFrameRate();
                }

                // 如果未配置时长，自动计算所有图层的最大结束时间
                if (duration_ms_ == 0 && layer->getEndTime() > duration_ms_) {
                    duration_ms_ = layer->getEndTime();
                }

                layers_.push_back(std::move(layer));
            }
        }

        if (layers_.empty() || canvas_.width == 0 || canvas_.height == 0) {
            unload();
            return false;
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
            return false;
        }

        cache_data_.resize(static_cast<size_t>(canvas_.width) * canvas_.height * 4);
        return true;
    } catch (...) {
        unload();
        return false;
    }
}

void RootNode::unload() {
    cancelPrepare();
    layers_.clear();
    cache_data_.clear();
    cache_time_ms_ = -1;
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
        render_fbo_ = gl::FBO(); // 重置为无效
    }
}

// ========== 外部接口 ==========

void RootNode::setCurrentTime(int64_t time_ms) {
    current_time_ms_ = std::clamp(time_ms, int64_t(0), duration_ms_);
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
    int64_t next_time = current_time_ms_ + static_cast<int64_t>(1000.0 / (frame_rate_ > 0 ? frame_rate_ : 25.0));
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

int64_t RootNode::getDurationMs() const {
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

int64_t RootNode::getCurrentTime() const {
    return current_time_ms_;
}

gl::FBOPool *RootNode::getFBOPool() {
    return &fbo_pool_;
}

const gl::FBO &RootNode::getRenderFBO() const {
    return render_fbo_;
}

Material *RootNode::getMaterial(const std::string &material_id) const {
    auto it = materials_.find(material_id);
    if (it != materials_.end())
        return it->second.get();
    return nullptr;
}

} // namespace vp
