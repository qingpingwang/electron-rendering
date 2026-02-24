#pragma once

#include "types.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace vp {
namespace gl {

// FBO 缓存池 - 管理不同尺寸/格式的 FBO，避免重复创建/销毁
class FBOPool {
public:
    FBOPool();
    ~FBOPool();

    FBOPool(const FBOPool &) = delete;
    FBOPool &operator=(const FBOPool &) = delete;

    // 获取或创建 FBO（从池中借用）
    FBO acquire(int width, int height, GLenum internal_format = GL_RGBA8, GLenum format = GL_RGBA,
                GLenum type = GL_UNSIGNED_BYTE);

    // 归还 FBO 到池中（支持重复归还，幂等操作）
    void release(FBO &fbo);

    // 清理所有缓存
    void clear();

    // 获取统计信息
    size_t getTotalCount() const;
    size_t getUsedCount() const;
    size_t getIdleCount() const;

private:
    struct FBOEntry {
        FBO fbo;
        bool used = true;
        int idle_time = 0;

        void destroy();
    };

    struct FBOCache {
        std::vector<FBOEntry> entries;
        int miss_time = 0;
        std::string key;

        // 获取或创建 FBO
        FBO acquire(int width, int height, GLenum internal_format, GLenum format, GLenum type,
                    int max_idle_time);

        // 清理未使用的 FBO
        void releaseUnused();

        // 清理所有 FBO
        void releaseAll();
    };

    std::string makeKey(int width, int height, GLenum internal_format, GLenum format,
                        GLenum type) const;

    std::map<std::string, FBOCache> caches_;
};

}
} // namespace vp::gl
