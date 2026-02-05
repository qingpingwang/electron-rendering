// FBO Pool 完整测试套件
// 编译: g++ -std=c++17 -I../src -framework OpenGL test/fbo_pool_test.cpp src/gl/*.cpp -o test_fbo_pool
// 运行: ./test_fbo_pool

#include "../src/gl/fbo_pool.h"
#include "../src/gl/functions.h"
#include <cassert>
#include <chrono>
#include <iostream>
#include <vector>

using namespace vp::gl;

// 测试统计
struct TestStats {
    int passed = 0;
    int failed = 0;
    int total = 0;

    void pass(const std::string &name) {
        std::cout << "  ✓ " << name << std::endl;
        ++passed;
        ++total;
    }

    void fail(const std::string &name, const std::string &reason) {
        std::cout << "  ✗ " << name << " - " << reason << std::endl;
        ++failed;
        ++total;
    }

    void summary() const {
        std::cout << "\n=== Test Summary ===" << std::endl;
        std::cout << "Total:  " << total << std::endl;
        std::cout << "Passed: " << passed << " (" << (total > 0 ? passed * 100 / total : 0)
                  << "%)" << std::endl;
        std::cout << "Failed: " << failed << std::endl;
    }
};

TestStats g_stats;

#define TEST(name) void test_##name()
#define RUN_TEST(name)                                  \
    do {                                                \
        std::cout << "\n[TEST] " << #name << std::endl; \
        test_##name();                                  \
    } while (0)
#define ASSERT(cond, msg)                          \
    do {                                           \
        if (cond) {                                \
            g_stats.pass(msg);                     \
        } else {                                   \
            g_stats.fail(msg, "assertion failed"); \
        }                                          \
    } while (0)

// ========== 测试用例 ==========

// 测试1: 基本获取和归还
TEST(basic_acquire_release) {
    FBOPool pool;

    // 获取 FBO
    FBO fbo1 = pool.acquire(1920, 1080);
    ASSERT(fbo1.isValid(), "Acquire returns valid FBO");
    ASSERT(fbo1.width == 1920 && fbo1.height == 1080, "FBO dimensions correct");
    ASSERT(pool.getTotalCount() == 1, "Total count = 1");
    ASSERT(pool.getUsedCount() == 1, "Used count = 1");
    ASSERT(pool.getIdleCount() == 0, "Idle count = 0");

    // 归还 FBO
    pool.release(fbo1);
    ASSERT(pool.getTotalCount() == 1, "Total count still 1 after release");
    ASSERT(pool.getUsedCount() == 0, "Used count = 0 after release");
    ASSERT(pool.getIdleCount() == 1, "Idle count = 1 after release");

    pool.clear();
}

// 测试2: 相同尺寸复用
TEST(same_size_reuse) {
    FBOPool pool;

    // 获取第一个
    FBO fbo1 = pool.acquire(1920, 1080);
    GLuint id1 = fbo1.fbo;
    pool.release(fbo1);

    // 获取相同尺寸，应该复用
    FBO fbo2 = pool.acquire(1920, 1080);
    ASSERT(fbo2.fbo == id1, "Same size FBO reused (ID matches)");
    ASSERT(pool.getTotalCount() == 1, "Total count = 1 (reused)");

    pool.clear();
}

// 测试3: 不同尺寸分别缓存
TEST(different_sizes) {
    FBOPool pool;

    FBO fbo1 = pool.acquire(1920, 1080);
    FBO fbo2 = pool.acquire(1280, 720);
    FBO fbo3 = pool.acquire(640, 480);

    ASSERT(pool.getTotalCount() == 3, "3 different sizes = 3 FBOs");
    ASSERT(fbo1.fbo != fbo2.fbo && fbo2.fbo != fbo3.fbo, "All FBOs have different IDs");

    pool.release(fbo1);
    pool.release(fbo2);
    pool.release(fbo3);

    ASSERT(pool.getIdleCount() == 3, "All 3 FBOs idle");

    pool.clear();
}

// 测试4: 多个相同尺寸（池扩展）
TEST(multiple_same_size) {
    FBOPool pool;

    // 同时获取 3 个相同尺寸
    FBO fbo1 = pool.acquire(1920, 1080);
    FBO fbo2 = pool.acquire(1920, 1080);
    FBO fbo3 = pool.acquire(1920, 1080);

    ASSERT(pool.getTotalCount() == 3, "Pool expanded to 3 FBOs");
    ASSERT(pool.getUsedCount() == 3, "All 3 in use");
    ASSERT(fbo1.fbo != fbo2.fbo && fbo2.fbo != fbo3.fbo, "3 unique FBO IDs");

    // 保存 ID（因为 release 会重置 FBO）
    GLuint id1 = fbo1.fbo;
    GLuint id2 = fbo2.fbo;

    // 归还后复用
    pool.release(fbo1);
    pool.release(fbo2);

    FBO fbo4 = pool.acquire(1920, 1080);
    FBO fbo5 = pool.acquire(1920, 1080);

    ASSERT(pool.getTotalCount() == 3, "Reused, total still 3");
    ASSERT((fbo4.fbo == id1 || fbo4.fbo == id2), "FBO4 reuses fbo1 or fbo2");

    pool.clear();
}

// 测试5: 幂等归还
TEST(idempotent_release) {
    FBOPool pool;

    FBO fbo = pool.acquire(1920, 1080);
    GLuint fbo_id = fbo.fbo;
    
    pool.release(fbo);
    ASSERT(pool.getIdleCount() == 1, "Idle = 1 after 1st release");
    ASSERT(!fbo.isValid(), "FBO invalidated after release");

    // 重复归还（fbo 已经无效，应该安全忽略）
    pool.release(fbo);
    ASSERT(pool.getIdleCount() == 1, "Idle still 1 after 2nd release (no-op)");

    pool.release(fbo);
    ASSERT(pool.getIdleCount() == 1, "Idle still 1 after 3rd release (no-op)");
    
    // 验证原 FBO 仍在池中
    FBO fbo2 = pool.acquire(1920, 1080);
    ASSERT(fbo2.fbo == fbo_id, "Original FBO reused");

    pool.clear();
}

// 测试6: 空闲淘汰（MAX_IDLE_TIME = 20）
TEST(idle_cleanup) {
    FBOPool pool;

    FBO fbo1 = pool.acquire(1920, 1080);
    pool.release(fbo1);

    // 模拟 21 帧，每帧获取不同尺寸（让 1920x1080 闲置）
    for (int i = 0; i < 21; ++i) {
        FBO temp = pool.acquire(1280, 720); // 不同尺寸
        pool.release(temp);
    }

    // 1920x1080 应该被清理了（闲置超过 20 帧）
    size_t total = pool.getTotalCount();
    ASSERT(total == 1, "Idle FBO cleaned up (only 1280x720 remains)");

    pool.clear();
}

// 测试7: 未命中淘汰（MAX_MISS_TIME = 10）
TEST(miss_cleanup) {
    FBOPool pool;

    // 创建两种尺寸
    FBO fbo1 = pool.acquire(1920, 1080);
    FBO fbo2 = pool.acquire(1280, 720);
    pool.release(fbo1);
    pool.release(fbo2);

    ASSERT(pool.getTotalCount() == 2, "Initial: 2 FBOs");

    // 11 帧只访问 1920x1080
    for (int i = 0; i < 11; ++i) {
        FBO temp = pool.acquire(1920, 1080);
        pool.release(temp);
    }

    // 1280x720 超过 10 帧未访问，应该被清理
    ASSERT(pool.getTotalCount() == 1, "Miss cleanup: only 1920x1080 remains");

    pool.clear();
}

// 测试8: 不同格式分别缓存
TEST(different_formats) {
    FBOPool pool;

    FBO fbo1 = pool.acquire(1920, 1080, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
    FBO fbo2 = pool.acquire(1920, 1080, GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE);

    // 相同尺寸但不同格式，应该是不同的 FBO
    ASSERT(pool.getTotalCount() == 2, "Different formats = different FBOs");
    ASSERT(fbo1.fbo != fbo2.fbo, "Different FBO IDs");
    ASSERT(fbo1.internal_format == GL_RGBA8, "FBO1 format correct");
    ASSERT(fbo2.internal_format == GL_RGB8, "FBO2 format correct");

    pool.clear();
}

// 测试9: 边界尺寸
TEST(edge_sizes) {
    FBOPool pool;

    FBO tiny = pool.acquire(1, 1);
    FBO large = pool.acquire(7680, 4320); // 8K

    ASSERT(tiny.isValid(), "1x1 FBO valid");
    ASSERT(large.isValid(), "8K FBO valid");
    ASSERT(pool.getTotalCount() == 2, "2 edge size FBOs created");

    pool.clear();
}

// 测试10: 清空池
TEST(clear_pool) {
    FBOPool pool;

    // 创建多个 FBO
    for (int i = 0; i < 10; ++i) {
        FBO fbo = pool.acquire(100 * (i + 1), 100 * (i + 1));
        if (i % 2 == 0)
            pool.release(fbo);
    }

    ASSERT(pool.getTotalCount() > 0, "Pool has FBOs before clear");

    pool.clear();
    ASSERT(pool.getTotalCount() == 0, "Pool empty after clear");
    ASSERT(pool.getUsedCount() == 0, "Used = 0 after clear");
    ASSERT(pool.getIdleCount() == 0, "Idle = 0 after clear");
}

// 测试11: 获取-归还循环
TEST(acquire_release_loop) {
    FBOPool pool;

    const int loops = 100;
    for (int i = 0; i < loops; ++i) {
        FBO fbo = pool.acquire(1920, 1080);
        ASSERT(fbo.isValid(), "Loop iteration " + std::to_string(i) + " valid");
        pool.release(fbo);
    }

    // 应该只有 1 个 FBO（复用）
    ASSERT(pool.getTotalCount() == 1, "Only 1 FBO after 100 loops (reused)");

    pool.clear();
}

// 测试12: 统计信息准确性
TEST(stats_accuracy) {
    FBOPool pool;

    FBO fbo1 = pool.acquire(1920, 1080);
    FBO fbo2 = pool.acquire(1920, 1080);
    FBO fbo3 = pool.acquire(1280, 720);

    ASSERT(pool.getTotalCount() == 3, "Total = 3");
    ASSERT(pool.getUsedCount() == 3, "Used = 3");
    ASSERT(pool.getIdleCount() == 0, "Idle = 0");

    pool.release(fbo1);
    ASSERT(pool.getUsedCount() == 2, "Used = 2 after 1 release");
    ASSERT(pool.getIdleCount() == 1, "Idle = 1 after 1 release");

    pool.release(fbo2);
    pool.release(fbo3);
    ASSERT(pool.getUsedCount() == 0, "Used = 0 after all released");
    ASSERT(pool.getIdleCount() == 3, "Idle = 3 after all released");

    pool.clear();
}

// 测试13: 性能测试
TEST(performance) {
    FBOPool pool;

    const int iterations = 1000;

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < iterations; ++i) {
        FBO fbo = pool.acquire(1920, 1080);
        pool.release(fbo);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    double avg_us = duration.count() / static_cast<double>(iterations);
    std::cout << "  Performance: " << iterations << " acquire+release in " << duration.count()
              << "us (avg: " << avg_us << "us per op)" << std::endl;

    ASSERT(avg_us < 100.0, "Average acquire+release < 100us");

    pool.clear();
}

// 测试14: 多规格混合访问
TEST(mixed_access_pattern) {
    FBOPool pool;

    struct Size {
        int w, h;
    };
    std::vector<Size> sizes = {{1920, 1080}, {1280, 720}, {640, 480}, {3840, 2160}};

    // 随机访问不同尺寸
    for (int frame = 0; frame < 50; ++frame) {
        Size s = sizes[frame % sizes.size()];
        FBO fbo = pool.acquire(s.w, s.h);
        pool.release(fbo);
    }

    ASSERT(pool.getTotalCount() >= 1, "Pool has at least 1 FBO after mixed access");
    ASSERT(pool.getIdleCount() >= 1, "At least 1 idle FBO");

    pool.clear();
}

// 测试15: 无效 FBO 归还
TEST(invalid_fbo_release) {
    FBOPool pool;

    FBO invalid_fbo; // 默认构造，无效
    ASSERT(!invalid_fbo.isValid(), "Invalid FBO created");

    // 归还无效 FBO 应该安全（不崩溃）
    pool.release(invalid_fbo);
    ASSERT(pool.getTotalCount() == 0, "Releasing invalid FBO doesn't affect pool");
}

// 测试16: 孤立 FBO 销毁（pool clear 后 release）
TEST(orphaned_fbo_destroy) {
    FBOPool pool;

    FBO fbo = pool.acquire(1920, 1080);
    GLuint fbo_id = fbo.fbo;
    
    ASSERT(fbo.isValid(), "FBO acquired");
    ASSERT(pool.getTotalCount() == 1, "Pool has 1 FBO");

    // 清空 pool（但我们还持有 FBO 引用）
    pool.clear();
    ASSERT(pool.getTotalCount() == 0, "Pool cleared");

    // 此时 fbo 仍然有效（OpenGL 资源未销毁）
    ASSERT(fbo.isValid(), "FBO still valid after pool clear");
    ASSERT(fbo.fbo == fbo_id, "FBO ID unchanged");

    // 释放这个孤立的 FBO（应该销毁 OpenGL 资源并重置引用）
    pool.release(fbo);
    ASSERT(!fbo.isValid(), "Orphaned FBO destroyed and invalidated");
    ASSERT(pool.getTotalCount() == 0, "Pool still empty");
}

// ========== 主函数 ==========

int main() {
    std::cout << "==================================" << std::endl;
    std::cout << "    FBO Pool Test Suite" << std::endl;
    std::cout << "==================================" << std::endl;

    // 初始化 OpenGL 上下文
    GLContext ctx;
    if (!initContext(ctx)) {
        std::cerr << "Failed to initialize OpenGL context" << std::endl;
        return 1;
    }
    makeCurrent(ctx);

    std::cout << "GPU: " << getGPUInfo(ctx) << std::endl;

    // 运行所有测试
    RUN_TEST(basic_acquire_release);
    RUN_TEST(same_size_reuse);
    RUN_TEST(different_sizes);
    RUN_TEST(multiple_same_size);
    RUN_TEST(idempotent_release);
    RUN_TEST(idle_cleanup);
    RUN_TEST(miss_cleanup);
    RUN_TEST(different_formats);
    RUN_TEST(edge_sizes);
    RUN_TEST(clear_pool);
    RUN_TEST(acquire_release_loop);
    RUN_TEST(stats_accuracy);
    RUN_TEST(performance);
    RUN_TEST(mixed_access_pattern);
    RUN_TEST(invalid_fbo_release);
    RUN_TEST(orphaned_fbo_destroy);

    // 输出统计
    g_stats.summary();

    // 清理
    destroyContext(ctx);

    std::cout << "\n==================================" << std::endl;

    return g_stats.failed > 0 ? 1 : 0;
}
