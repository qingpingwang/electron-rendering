#pragma once

#include <filesystem>
#include <string>

namespace vp {

// 拼接资源最终路径：
// - path 为空 / 绝对路径 / base 为空 → 原样返回 path
// - 否则 → base / path
inline std::string resolvePath(const std::string &base, const std::string &path) {
    if (path.empty() || base.empty() || std::filesystem::path(path).is_absolute())
        return path;
    return (std::filesystem::path(base) / path).string();
}

} // namespace vp
