#pragma once

#include "include/core/SkRefCnt.h"
#include "modules/skparagraph/include/FontCollection.h"
#include "modules/skunicode/include/SkUnicode.h"
#include <string>
#include <unordered_map>

class SkFontMgr;
class SkTypeface;

namespace vp {

class FontManager {
public:
    static FontManager &getInstance();

    sk_sp<skia::textlayout::FontCollection> getFontCollection() const { return font_collection_; }
    sk_sp<SkUnicode> getUnicode() const { return unicode_; }

    // 按文件路径加载字体（自动缓存），用于 TextStyle::setTypeface
    sk_sp<SkTypeface> loadFromFile(const std::string &path);

    // 优先文件路径，fallback 到字体名，再 fallback 到系统默认
    sk_sp<SkTypeface> resolve(const std::string &path, const std::string &family);

    FontManager(const FontManager &) = delete;
    FontManager &operator=(const FontManager &) = delete;

private:
    FontManager();
    ~FontManager();

    sk_sp<SkFontMgr> font_mgr_;
    sk_sp<skia::textlayout::FontCollection> font_collection_;
    sk_sp<SkUnicode> unicode_;
    std::unordered_map<std::string, sk_sp<SkTypeface>> file_cache_;
};

} // namespace vp
