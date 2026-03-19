#pragma once

#include "../../core/types.h"
#include "../../gl/types.h"
#include "core/loadable.h"
#include <nlohmann/json.hpp>
#include <memory>
#include <string>
#include <vector>

namespace vp {

class Layer;
class Effect;
class RootNode;

class GroupLayer : public Loadable {
public:
    explicit GroupLayer(RootNode *root);
    ~GroupLayer() = default;

    GroupLayer(const GroupLayer &) = delete;
    GroupLayer &operator=(const GroupLayer &) = delete;

    virtual bool load(const nlohmann::json &config, const std::string &base_path = "") override;

    // 序列化当前状态为 JSON（与 load 对称）
    virtual nlohmann::json dump() const override;

    bool draw(const gl::FBO &target, TimeMs time_ms);

    const std::string &getId() const {
        return id_;
    }
    const std::string &getType() const {
        return type_;
    }

    bool isVisible() const {
        return visible_;
    }
    void setVisible(bool v) {
        visible_ = v;
    }

    bool isMuted() const {
        return muted_;
    }
    void setMuted(bool m) {
        muted_ = m;
    }

    const std::vector<std::unique_ptr<Layer>> &getLayers() const {
        return layers_;
    }

private:
    void addLayer(std::unique_ptr<Layer> layer);
    bool renderTransition(Layer *from, Layer *to, Effect *transition,
                          TimeMs time_ms, const gl::FBO &target);

    RootNode *root_;
    std::string id_;
    std::string type_;
    bool visible_ = true;
    bool muted_ = false;
    std::vector<std::unique_ptr<Layer>> layers_;
};

} // namespace vp
