#include "root_node.h"
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <cstring>
#include <algorithm>

using json = nlohmann::json;

namespace vp
{

    RootNode::RootNode()
    {
        renderer_ = std::make_unique<GLRenderer>();
    }

    RootNode::~RootNode()
    {
        cleanup();
    }

    bool RootNode::init()
    {
        return renderer_->init();
    }

    void RootNode::cleanup()
    {
        cancelPrepare();
        unload();
        if (renderer_)
            renderer_->cleanup();
    }

    // ========== 缓存 ==========

    int64_t RootNode::getHalfFrameMs() const
    {
        return (frame_rate_ > 0) ? static_cast<int64_t>(500.0 / frame_rate_) : 20;
    }

    bool RootNode::isCacheHit(int64_t time_ms) const
    {
        if (cache_time_ms_ < 0)
            return false;
        return std::abs(time_ms - cache_time_ms_) <= getHalfFrameMs();
    }

    // ========== 渲染 ==========

    bool RootNode::renderFrame(int64_t time_ms, uint8_t *out_buffer)
    {
        if (!renderer_ || layers_.empty())
            return false;

        for (auto &layer : layers_)
        {
            // 检查取消标志
            if (cancel_flag_)
                return false;

            layer->setCurrentTime(time_ms);
            layer->draw(*renderer_);
        }

        // 最后再检查一次
        if (cancel_flag_)
            return false;

        size_t size = static_cast<size_t>(width_) * height_ * 4;
        renderer_->readPixels(out_buffer, static_cast<int>(size));
        return true;
    }

    void RootNode::startPrepareNextFrame(int64_t next_time_ms)
    {
        // 超出时长不准备
        if (next_time_ms > duration_ms_)
            return;

        // 已经在缓存中不准备
        if (isCacheHit(next_time_ms))
            return;

        // 取消并等待上一次完成
        cancelPrepare();

        // 立即设置缓存时间（标记正在准备）
        {
            std::lock_guard<std::mutex> lock(cache_mutex_);
            cache_time_ms_ = next_time_ms;
        }

        // 启动新线程
        cancel_flag_ = false;
        preparing_ = true;
        prepare_thread_ = std::thread([this, next_time_ms]()
                                      {
            bool success = renderFrame(next_time_ms, cache_data_.data());
            if (!success)
            {
                // 渲染失败，重置缓存时间
                std::lock_guard<std::mutex> lock(cache_mutex_);
                cache_time_ms_ = -1;
            }
            preparing_ = false; });
    }

    void RootNode::cancelPrepare()
    {
        // 设置取消标志
        cancel_flag_ = true;

        // 等待线程完成（如果有）
        if (prepare_thread_.joinable())
            prepare_thread_.join();

        // 重置状态
        preparing_ = false;
    }

    // ========== 加载 ==========

    bool RootNode::loadFromJson(const std::string &json_str)
    {
        unload();

        try
        {
            json config = json::parse(json_str);

            if (config.contains("canvas_config"))
            {
                auto &canvas = config["canvas_config"];
                width_ = canvas.value("width", 0);
                height_ = canvas.value("height", 0);
            }

            std::unordered_map<std::string, std::string> video_materials;
            if (config.contains("materials") && config["materials"].contains("videos"))
            {
                for (const auto &mat : config["materials"]["videos"])
                {
                    std::string id = mat.value("id", "");
                    std::string path = mat.value("path", "");
                    if (!id.empty() && !path.empty())
                        video_materials[id] = path;
                }
            }

            if (!config.contains("tracks") || !config["tracks"].is_array())
                return false;

            for (const auto &track : config["tracks"])
            {
                if (track.value("type", "") != "video" || !track.contains("segments"))
                    continue;

                for (const auto &segment : track["segments"])
                {
                    auto it = video_materials.find(segment.value("material_id", ""));
                    if (it == video_materials.end())
                        continue;

                    auto layer = std::make_unique<VideoLayer>(segment.value("id", "video"));
                    if (!layer->load(it->second))
                        continue;

                    if (width_ == 0 || height_ == 0)
                    {
                        width_ = layer->getWidth();
                        height_ = layer->getHeight();
                    }

                    if (layer->getDurationMs() > duration_ms_)
                    {
                        duration_ms_ = layer->getDurationMs();
                        frame_rate_ = layer->getFrameRate();
                    }

                    layers_.push_back(std::move(layer));
                }
            }

            if (layers_.empty() || width_ == 0 || height_ == 0)
            {
                unload();
                return false;
            }

            if (!renderer_->setSize(width_, height_))
            {
                unload();
                return false;
            }

            cache_data_.resize(static_cast<size_t>(width_) * height_ * 4);
            return true;
        }
        catch (...)
        {
            unload();
            return false;
        }
    }

    void RootNode::unload()
    {
        cancelPrepare();
        layers_.clear();
        cache_data_.clear();
        cache_time_ms_ = -1;
        current_time_ms_ = 0;
        width_ = height_ = 0;
        duration_ms_ = 0;
        frame_rate_ = 0.0;
    }

    // ========== 外部接口 ==========

    void RootNode::setCurrentTime(int64_t time_ms)
    {
        current_time_ms_ = std::clamp(time_ms, int64_t(0), duration_ms_);
    }

    bool RootNode::draw(uint8_t *buffer, size_t buffer_size)
    {
        if (!renderer_ || layers_.empty() || !buffer)
            return false;

        size_t required = static_cast<size_t>(width_) * height_ * 4;
        if (buffer_size < required)
            return false;

        // 检查缓存（包括正在准备的）
        if (isCacheHit(current_time_ms_))
        {
            // 命中：等待准备完成，拷贝缓存
            if (prepare_thread_.joinable())
                prepare_thread_.join();
            std::lock_guard<std::mutex> lock(cache_mutex_);
            std::memcpy(buffer, cache_data_.data(), required);
            fprintf(stderr, "[RootNode] cache hit, time=%lldms\n", current_time_ms_);
        }
        else
        {
            // 未命中：取消异步任务，直接渲染
            cancelPrepare();
            cancel_flag_ = false;
            renderFrame(current_time_ms_, buffer);
            fprintf(stderr, "[RootNode] cache miss, time=%lldms\n", current_time_ms_);
        }

        // 启动异步准备下一帧
        int64_t next_time = current_time_ms_ + static_cast<int64_t>(1000.0 / (frame_rate_ > 0 ? frame_rate_ : 25.0));
        startPrepareNextFrame(next_time);

        return true;
    }

    std::string RootNode::getGPUInfo() const
    {
        return renderer_ ? renderer_->getGPUInfo() : "N/A";
    }

} // namespace vp
