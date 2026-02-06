#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace vp {

// 可加载资源的基类 - 统一 load 接口和错误处理
class Loadable {
public:
    virtual ~Loadable() = default;

    // 纯虚 load 函数 - 所有子类必须实现
    // base_path 参数可选（默认为空），不需要的子类可以忽略
    virtual bool load(const nlohmann::json &config, const std::string &base_path = "") = 0;

    // 获取错误信息
    const std::string &getErrorMessage() const {
        return error_message_;
    }

protected:
    // 子类设置错误信息
    void setError(const std::string &msg) {
        error_message_ = msg;
    }

    // 清空错误
    void clearError() {
        error_message_.clear();
    }

private:
    std::string error_message_;
};

} // namespace vp
