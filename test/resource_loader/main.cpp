#include "../../src/core/root_node.h"
#include "../../src/resource/render_resource.h"
#include <filesystem>
#include <iostream>
#include <vector>

namespace fs = std::filesystem;

// 测试用例结构
struct TestCase {
    std::string name;
    std::string path;
    std::string error;
};

// 扫描目录下的所有特效配置
std::vector<TestCase> scanTestResources(const std::string &base_path) {
    std::vector<TestCase> tests;

    if (!fs::exists(base_path)) {
        std::cerr << "Resources path not found: " << base_path << std::endl;
        return tests;
    }

    std::cout << "Scanning: " << base_path << std::endl;

    for (const auto &entry : fs::directory_iterator(base_path)) {
        if (entry.is_directory()) {
            fs::path config_path = entry.path() / "config.json";

            if (fs::exists(config_path)) {
                TestCase test;
                test.name = entry.path().filename().string();
                test.path = entry.path().string();
                tests.push_back(test);
            }
        }
    }

    return tests;
}

// 加载并验证单个特效资源
bool testLoadResource(vp::RootNode *root, TestCase &test) {
    std::cout << "\n[TEST] " << test.name << std::endl;
    std::cout << "  Path: " << test.path << std::endl;

    try {
        auto resource = std::make_unique<vp::RenderResource>(root);

        if (!resource->loadFromFolder(test.path)) {
            test.error = "Failed to load resource";
            return false;
        }

        // 输出基本信息
        std::cout << "  ✓ Loaded successfully" << std::endl;
        std::cout << "    ID: " << resource->getId() << std::endl;
        std::cout << "    Name: " << resource->getName() << std::endl;
        std::cout << "    Format: " << resource->getFormat() << std::endl;

        return true;
    } catch (const std::exception &e) {
        test.error = std::string("Exception: ") + e.what();
        return false;
    }
}

int main(int argc, char *argv[]) {
    std::cout << std::flush;

    std::cout << "========================================" << std::endl;
    std::cout << "  Render Resource Loader Test" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;

    // 1. 确定测试基础路径
    fs::path test_base_path;
    if (argc > 1) {
        test_base_path = fs::path(argv[1]);
    } else {
        test_base_path = "../../../test/resources";
    }

    std::cout << "Test base path: \"" << test_base_path.string() << "\"" << std::endl;
    std::cout << std::endl;

    // 2. 初始化 RootNode
    auto root = std::make_unique<vp::RootNode>();
    if (!root->init()) {
        std::cerr << "Failed to initialize RootNode (OpenGL context required)" << std::endl;
        return 1;
    }

    std::cout << "✓ RootNode initialized successfully" << std::endl;
    std::cout << "  GPU: " << root->getGPUInfo() << std::endl;
    std::cout << std::endl;

    // 3. 扫描测试资源
    std::cout << "[SCAN] Scanning test resources..." << std::endl;
    auto tests = scanTestResources(test_base_path.string());

    if (tests.empty()) {
        std::cout << "Found 0 test case(s)" << std::endl;
        std::cerr << "No test cases found!" << std::endl;
        return 1;
    }

    std::cout << "Found " << tests.size() << " test case(s)" << std::endl;

    // 4. 逐个测试加载
    int passed = 0;
    int failed = 0;

    for (auto &test : tests) {
        if (testLoadResource(root.get(), test)) {
            passed++;
        } else {
            failed++;
            std::cout << "  ✗ Failed: " << test.error << std::endl;
        }
    }

    // 5. 输出总结
    std::cout << "\n========================================" << std::endl;
    std::cout << "  Test Summary" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Total:  " << tests.size() << std::endl;
    std::cout << "Passed: " << passed << std::endl;

    if (failed > 0) {
        std::cout << "Failed: " << failed << std::endl;
    }

    std::cout << "\nTest Cases:" << std::endl;
    for (const auto &test : tests) {
        if (test.error.empty()) {
            std::cout << "  ✓ " << test.name << std::endl;
        } else {
            std::cout << "  ✗ " << test.name << " (" << test.error << ")" << std::endl;
        }
    }

    std::cout << std::endl;

    return (failed == 0) ? 0 : 1;
}
