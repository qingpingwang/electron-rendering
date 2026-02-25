#include "font_manager.h"
#include "include/core/SkData.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkFontStyle.h"
#include "include/core/SkStream.h"
#include "include/core/SkTypeface.h"
#include "modules/skunicode/include/SkUnicode_icu.h"

#ifdef __APPLE__
#define SK_BUILD_FOR_MAC
#include "include/ports/SkFontMgr_mac_ct.h"
#endif

namespace vp {

FontManager &FontManager::getInstance() {
    static FontManager instance;
    return instance;
}

FontManager::FontManager() {
#ifdef __APPLE__
    font_mgr_ = SkFontMgr_New_CoreText(nullptr);
#endif

    font_collection_ = sk_make_sp<skia::textlayout::FontCollection>();
    font_collection_->setDefaultFontManager(font_mgr_);

    unicode_ = SkUnicodes::ICU::Make();
}

FontManager::~FontManager() = default;

sk_sp<SkTypeface> FontManager::loadFromFile(const std::string &path) {
    if (path.empty())
        return nullptr;

    auto it = file_cache_.find(path);
    if (it != file_cache_.end())
        return it->second;

    if (!font_mgr_)
        return nullptr;

    auto typeface = font_mgr_->makeFromFile(path.c_str(), 0);
    if (typeface)
        file_cache_[path] = typeface;

    return typeface;
}

sk_sp<SkTypeface> FontManager::resolve(const std::string &path, const std::string &family) {
    if (!path.empty()) {
        auto tf = loadFromFile(path);
        if (tf) return tf;
    }
    if (!family.empty() && font_mgr_) {
        auto tf = font_mgr_->matchFamilyStyle(family.c_str(), SkFontStyle());
        if (tf) return tf;
    }
    if (font_mgr_)
        return font_mgr_->matchFamilyStyle(nullptr, SkFontStyle());
    return nullptr;
}

} // namespace vp
