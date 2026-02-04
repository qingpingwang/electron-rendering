#include "fbo_pool.h"
#include "functions.h"
#include <algorithm>

namespace vp {
namespace gl {

// FBO 缓存池阈值
static const int MAX_MISS_TIME = 10; // 最大未命中帧数（一级阈值）
static const int MAX_IDLE_TIME = 20; // 最大空闲帧数（二级阈值）

FBOPool::FBOPool() {
}

FBOPool::~FBOPool() {
    clear();
}

std::string FBOPool::makeKey(int width, int height, GLenum internal_format, GLenum format,
                             GLenum type) const {
    return std::to_string(width) + "_" + std::to_string(height) + "_" +
           std::to_string(internal_format) + "_" + std::to_string(format) + "_" +
           std::to_string(type);
}

FBO FBOPool::acquire(int width, int height, GLenum internal_format, GLenum format, GLenum type) {
    std::string key = makeKey(width, height, internal_format, format, type);

    // 如果该规格的缓存不存在，创建新的缓存
    if (caches_.count(key) == 0) {
        FBOCache new_cache;
        new_cache.key = key;
        caches_[key] = new_cache;
    }

    FBOCache &cache = caches_[key];
    cache.miss_time = 0; // 重置未命中计数

    // 处理其他缓存的未命中计数
    for (auto it = caches_.begin(); it != caches_.end();) {
        if (it->first == key) {
            ++it;
            continue;
        }

        // 增加未命中计数
        if (++(it->second.miss_time) >= MAX_MISS_TIME) {
            // 超过阈值，清理未使用的 FBO
            it->second.releaseUnused();
            it->second.miss_time = 0;
        }

        // 如果该缓存已空，移除
        if (it->second.entries.empty()) {
            it = caches_.erase(it);
        } else {
            ++it;
        }
    }

    // 从缓存中获取或创建 FBO
    return cache.acquire(width, height, internal_format, format, type, MAX_IDLE_TIME);
}

void FBOPool::release(FBO &fbo) {
    if (!fbo.isValid())
        return;

    // 归还前清理 FBO（避免残留数据）
    if (fbo.isValid()) {
        bindFBO(fbo);
        cleanColor(0, 0, 0, 0);
        unbindFBO();
    }

    std::string key = makeKey(fbo.width, fbo.height, fbo.internal_format, fbo.format, fbo.type);

    auto it = caches_.find(key);
    if (it == caches_.end())
        return;

    FBOCache &cache = it->second;

    // 查找对应的 FBO 条目
    auto entry_it =
        std::find_if(cache.entries.begin(), cache.entries.end(),
                     [&fbo](const FBOEntry &entry) { return entry.fbo.fbo == fbo.fbo; });

    if (entry_it != cache.entries.end()) {
        // 标记为未使用
        entry_it->used = false;
        entry_it->idle_time = 0;
    }
}

void FBOPool::clear() {
    for (auto &pair : caches_) {
        pair.second.releaseAll();
    }
    caches_.clear();
}

size_t FBOPool::getTotalCount() const {
    size_t count = 0;
    for (const auto &pair : caches_) {
        count += pair.second.entries.size();
    }
    return count;
}

size_t FBOPool::getUsedCount() const {
    size_t count = 0;
    for (const auto &pair : caches_) {
        for (const auto &entry : pair.second.entries) {
            if (entry.used)
                ++count;
        }
    }
    return count;
}

size_t FBOPool::getIdleCount() const {
    return getTotalCount() - getUsedCount();
}

// ========== FBOCache 实现 ==========

FBO FBOPool::FBOCache::acquire(int width, int height, GLenum internal_format, GLenum format,
                                GLenum type, int max_idle_time) {
    // 查找未使用的 FBO
    auto it = std::find_if(entries.begin(), entries.end(),
                           [](const FBOEntry &entry) { return !entry.used; });

    FBO result;

    if (it == entries.end()) {
        // 没有可用的，创建新的
        FBO new_fbo = createFBO(width, height, internal_format, format, type);
        if (!new_fbo.isValid()) {
            return FBO(); // 创建失败
        }

        FBOEntry entry;
        entry.fbo = new_fbo;
        entry.used = true;
        entry.idle_time = 0;
        entries.push_back(entry);

        result = new_fbo;
    } else {
        // 从缓存中取
        result = it->fbo;
        it->used = true;
        it->idle_time = 0;
    }

    // 处理其他未使用的 FBO 的空闲时间
    for (auto entry_it = entries.begin(); entry_it != entries.end();) {
        if (!entry_it->used && ++(entry_it->idle_time) >= max_idle_time) {
            // 超过空闲阈值，释放
            entry_it->destroy();
            entry_it = entries.erase(entry_it);
        } else {
            ++entry_it;
        }
    }

    return result;
}

void FBOPool::FBOCache::releaseUnused() {
    for (auto it = entries.begin(); it != entries.end();) {
        if (!it->used) {
            it->destroy();
            it = entries.erase(it);
        } else {
            ++it;
        }
    }
}

void FBOPool::FBOCache::releaseAll() {
    for (auto &entry : entries) {
        entry.destroy();
    }
    entries.clear();
}

// ========== FBOEntry 实现 ==========

void FBOPool::FBOEntry::destroy() {
    if (fbo.isValid()) {
        destroyFBO(fbo);
    }
}

} // namespace gl
} // namespace vp
