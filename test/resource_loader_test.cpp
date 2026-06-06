#include <gtest/gtest.h>

#include "../src/core/root_node.h"
#include "../src/resource/render_resource.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

const std::string kResourcesDir = VP_RESOURCES_DIR;

// 共享 RootNode（持有 GL + Skia 上下文），避免每个用例重复初始化。
class ResourceLoaderTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        root_ = new vp::RootNode();
        ASSERT_TRUE(root_->init()) << "failed to init RootNode (GL context required)";
    }

    static void TearDownTestSuite() {
        delete root_;
        root_ = nullptr;
    }

    static std::vector<std::string> scanResourceDirs() {
        std::vector<std::string> dirs;
        if (!fs::exists(kResourcesDir))
            return dirs;
        for (const auto &entry : fs::directory_iterator(kResourcesDir)) {
            if (entry.is_directory() && fs::exists(entry.path() / "config.json"))
                dirs.push_back(entry.path().string());
        }
        return dirs;
    }

    static vp::RootNode *root_;
};

vp::RootNode *ResourceLoaderTest::root_ = nullptr;

// 测试资源目录存在且至少有一个特效包。
TEST_F(ResourceLoaderTest, ResourcesDirHasEffects) {
    ASSERT_TRUE(fs::exists(kResourcesDir)) << "resources dir missing: " << kResourcesDir;
    auto dirs = scanResourceDirs();
    EXPECT_FALSE(dirs.empty()) << "no effect package found under " << kResourcesDir;
}

// 逐个加载每个特效包，全部应成功并带非空 id。
TEST_F(ResourceLoaderTest, LoadAllEffectPackages) {
    auto dirs = scanResourceDirs();
    ASSERT_FALSE(dirs.empty());

    for (const auto &dir : dirs) {
        auto resource = std::make_unique<vp::RenderResource>(root_);
        EXPECT_TRUE(resource->loadFromFolder(dir)) << "failed to load: " << dir;
        EXPECT_FALSE(resource->getId().empty()) << "empty id for: " << dir;
    }
}

} // namespace
