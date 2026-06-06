#include <gtest/gtest.h>

#include "../src/gl/fbo_pool.h"
#include "../src/gl/functions.h"

#include <chrono>
#include <string>
#include <vector>

using namespace vp::gl;

namespace {

// 所有 FBO 测试共享一个 GL 上下文（EGL/ANGLE pbuffer，离屏）。
class FBOPoolTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        ASSERT_TRUE(initContext(s_ctx_)) << "failed to init GL context";
        makeCurrent(s_ctx_);
    }

    static void TearDownTestSuite() {
        destroyContext(s_ctx_);
    }

    static GLContext s_ctx_;
};

GLContext FBOPoolTest::s_ctx_;

// 基本获取和归还
TEST_F(FBOPoolTest, BasicAcquireRelease) {
    FBOPool pool;

    FBO fbo1 = pool.acquire(1920, 1080);
    EXPECT_TRUE(fbo1.isValid());
    EXPECT_EQ(fbo1.width, 1920);
    EXPECT_EQ(fbo1.height, 1080);
    EXPECT_EQ(pool.getTotalCount(), 1u);
    EXPECT_EQ(pool.getUsedCount(), 1u);
    EXPECT_EQ(pool.getIdleCount(), 0u);

    pool.release(fbo1);
    EXPECT_EQ(pool.getTotalCount(), 1u);
    EXPECT_EQ(pool.getUsedCount(), 0u);
    EXPECT_EQ(pool.getIdleCount(), 1u);

    pool.clear();
}

// 相同尺寸复用
TEST_F(FBOPoolTest, SameSizeReuse) {
    FBOPool pool;

    FBO fbo1 = pool.acquire(1920, 1080);
    GLuint id1 = fbo1.fbo;
    pool.release(fbo1);

    FBO fbo2 = pool.acquire(1920, 1080);
    EXPECT_EQ(fbo2.fbo, id1);
    EXPECT_EQ(pool.getTotalCount(), 1u);

    pool.clear();
}

// 不同尺寸分别缓存
TEST_F(FBOPoolTest, DifferentSizes) {
    FBOPool pool;

    FBO fbo1 = pool.acquire(1920, 1080);
    FBO fbo2 = pool.acquire(1280, 720);
    FBO fbo3 = pool.acquire(640, 480);

    EXPECT_EQ(pool.getTotalCount(), 3u);
    EXPECT_NE(fbo1.fbo, fbo2.fbo);
    EXPECT_NE(fbo2.fbo, fbo3.fbo);

    pool.release(fbo1);
    pool.release(fbo2);
    pool.release(fbo3);
    EXPECT_EQ(pool.getIdleCount(), 3u);

    pool.clear();
}

// 多个相同尺寸（池扩展）
TEST_F(FBOPoolTest, MultipleSameSize) {
    FBOPool pool;

    FBO fbo1 = pool.acquire(1920, 1080);
    FBO fbo2 = pool.acquire(1920, 1080);
    FBO fbo3 = pool.acquire(1920, 1080);

    EXPECT_EQ(pool.getTotalCount(), 3u);
    EXPECT_EQ(pool.getUsedCount(), 3u);
    EXPECT_NE(fbo1.fbo, fbo2.fbo);
    EXPECT_NE(fbo2.fbo, fbo3.fbo);

    GLuint id1 = fbo1.fbo;
    GLuint id2 = fbo2.fbo;

    pool.release(fbo1);
    pool.release(fbo2);

    FBO fbo4 = pool.acquire(1920, 1080);
    FBO fbo5 = pool.acquire(1920, 1080);
    (void)fbo5;

    EXPECT_EQ(pool.getTotalCount(), 3u);
    EXPECT_TRUE(fbo4.fbo == id1 || fbo4.fbo == id2);

    pool.clear();
}

// 幂等归还
TEST_F(FBOPoolTest, IdempotentRelease) {
    FBOPool pool;

    FBO fbo = pool.acquire(1920, 1080);
    GLuint fbo_id = fbo.fbo;

    pool.release(fbo);
    EXPECT_EQ(pool.getIdleCount(), 1u);
    EXPECT_FALSE(fbo.isValid());

    pool.release(fbo);
    EXPECT_EQ(pool.getIdleCount(), 1u);

    pool.release(fbo);
    EXPECT_EQ(pool.getIdleCount(), 1u);

    FBO fbo2 = pool.acquire(1920, 1080);
    EXPECT_EQ(fbo2.fbo, fbo_id);

    pool.clear();
}

// 空闲淘汰（MAX_IDLE_TIME = 20）
TEST_F(FBOPoolTest, IdleCleanup) {
    FBOPool pool;

    FBO fbo1 = pool.acquire(1920, 1080);
    pool.release(fbo1);

    for (int i = 0; i < 21; ++i) {
        FBO temp = pool.acquire(1280, 720);
        pool.release(temp);
    }

    EXPECT_EQ(pool.getTotalCount(), 1u);

    pool.clear();
}

// 未命中淘汰（MAX_MISS_TIME = 10）
TEST_F(FBOPoolTest, MissCleanup) {
    FBOPool pool;

    FBO fbo1 = pool.acquire(1920, 1080);
    FBO fbo2 = pool.acquire(1280, 720);
    pool.release(fbo1);
    pool.release(fbo2);

    EXPECT_EQ(pool.getTotalCount(), 2u);

    for (int i = 0; i < 11; ++i) {
        FBO temp = pool.acquire(1920, 1080);
        pool.release(temp);
    }

    EXPECT_EQ(pool.getTotalCount(), 1u);

    pool.clear();
}

// 不同格式分别缓存
TEST_F(FBOPoolTest, DifferentFormats) {
    FBOPool pool;

    FBO fbo1 = pool.acquire(1920, 1080, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
    FBO fbo2 = pool.acquire(1920, 1080, GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE);

    EXPECT_EQ(pool.getTotalCount(), 2u);
    EXPECT_NE(fbo1.fbo, fbo2.fbo);
    EXPECT_EQ(fbo1.internal_format, static_cast<GLenum>(GL_RGBA8));
    EXPECT_EQ(fbo2.internal_format, static_cast<GLenum>(GL_RGB8));

    pool.clear();
}

// 边界尺寸
TEST_F(FBOPoolTest, EdgeSizes) {
    FBOPool pool;

    FBO tiny = pool.acquire(1, 1);
    FBO large = pool.acquire(7680, 4320);

    EXPECT_TRUE(tiny.isValid());
    EXPECT_TRUE(large.isValid());
    EXPECT_EQ(pool.getTotalCount(), 2u);

    pool.clear();
}

// 清空池
TEST_F(FBOPoolTest, ClearPool) {
    FBOPool pool;

    for (int i = 0; i < 10; ++i) {
        FBO fbo = pool.acquire(100 * (i + 1), 100 * (i + 1));
        if (i % 2 == 0)
            pool.release(fbo);
    }

    EXPECT_GT(pool.getTotalCount(), 0u);

    pool.clear();
    EXPECT_EQ(pool.getTotalCount(), 0u);
    EXPECT_EQ(pool.getUsedCount(), 0u);
    EXPECT_EQ(pool.getIdleCount(), 0u);
}

// 获取-归还循环
TEST_F(FBOPoolTest, AcquireReleaseLoop) {
    FBOPool pool;

    const int loops = 100;
    for (int i = 0; i < loops; ++i) {
        FBO fbo = pool.acquire(1920, 1080);
        ASSERT_TRUE(fbo.isValid()) << "loop iteration " << i;
        pool.release(fbo);
    }

    EXPECT_EQ(pool.getTotalCount(), 1u);

    pool.clear();
}

// 统计信息准确性
TEST_F(FBOPoolTest, StatsAccuracy) {
    FBOPool pool;

    FBO fbo1 = pool.acquire(1920, 1080);
    FBO fbo2 = pool.acquire(1920, 1080);
    FBO fbo3 = pool.acquire(1280, 720);

    EXPECT_EQ(pool.getTotalCount(), 3u);
    EXPECT_EQ(pool.getUsedCount(), 3u);
    EXPECT_EQ(pool.getIdleCount(), 0u);

    pool.release(fbo1);
    EXPECT_EQ(pool.getUsedCount(), 2u);
    EXPECT_EQ(pool.getIdleCount(), 1u);

    pool.release(fbo2);
    pool.release(fbo3);
    EXPECT_EQ(pool.getUsedCount(), 0u);
    EXPECT_EQ(pool.getIdleCount(), 3u);

    pool.clear();
}

// 性能：1000 次 acquire+release 平均 < 100us
TEST_F(FBOPoolTest, Performance) {
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

    EXPECT_LT(avg_us, 100.0);

    pool.clear();
}

// 多规格混合访问
TEST_F(FBOPoolTest, MixedAccessPattern) {
    FBOPool pool;

    struct Size {
        int w, h;
    };
    std::vector<Size> sizes = {{1920, 1080}, {1280, 720}, {640, 480}, {3840, 2160}};

    for (int frame = 0; frame < 50; ++frame) {
        Size s = sizes[frame % sizes.size()];
        FBO fbo = pool.acquire(s.w, s.h);
        pool.release(fbo);
    }

    EXPECT_GE(pool.getTotalCount(), 1u);
    EXPECT_GE(pool.getIdleCount(), 1u);

    pool.clear();
}

// 无效 FBO 归还
TEST_F(FBOPoolTest, InvalidFboRelease) {
    FBOPool pool;

    FBO invalid_fbo;
    EXPECT_FALSE(invalid_fbo.isValid());

    pool.release(invalid_fbo);
    EXPECT_EQ(pool.getTotalCount(), 0u);
}

// 孤立 FBO 销毁（pool clear 后 release）
TEST_F(FBOPoolTest, OrphanedFboDestroy) {
    FBOPool pool;

    FBO fbo = pool.acquire(1920, 1080);
    GLuint fbo_id = fbo.fbo;

    EXPECT_TRUE(fbo.isValid());
    EXPECT_EQ(pool.getTotalCount(), 1u);

    pool.clear();
    EXPECT_EQ(pool.getTotalCount(), 0u);

    EXPECT_TRUE(fbo.isValid());
    EXPECT_EQ(fbo.fbo, fbo_id);

    pool.release(fbo);
    EXPECT_FALSE(fbo.isValid());
    EXPECT_EQ(pool.getTotalCount(), 0u);
}

} // namespace
