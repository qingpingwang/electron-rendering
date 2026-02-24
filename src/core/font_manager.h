#pragma once

#include "include/core/SkRefCnt.h"
#include <string>
#include <unordered_map>

class SkFontMgr;
class SkTypeface;

namespace vp {

class FontManager {
public:
    static FontManager &getInstance();

    // 从系统字体匹配
    sk_sp<SkTypeface> matchTypeface(const std::string &family);

    // 从文件加载（自动缓存）
    sk_sp<SkTypeface> loadFromFile(const std::string &path);

    // 优先文件路径，fallback 到字体名，再 fallback 到系统默认
    sk_sp<SkTypeface> resolve(const std::string &path, const std::string &family);

    FontManager(const FontManager &) = delete;
    FontManager &operator=(const FontManager &) = delete;

private:
    FontManager();
    ~FontManager();

    sk_sp<SkFontMgr> font_mgr_;
    std::unordered_map<std::string, sk_sp<SkTypeface>> file_cache_;
};

} // namespace vp
