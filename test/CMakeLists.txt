cmake_minimum_required(VERSION 3.12)

# 测试目录的 CMake 配置

# 通用配置
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找依赖
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavformat
    libavcodec
    libavutil
    libswscale
)

# 注意：测试直接使用父目录的 video_player_core OBJECT 库
# 无需重复编译源文件！

# Skia 路径配置（测试也需要链接 Skia）
set(SKIA_ROOT ${CMAKE_SOURCE_DIR}/third_party/skia)
set(SKIA_LIB ${SKIA_ROOT}/out/Release/libskia.a)

# 通用链接库
set(COMMON_LIBRARIES
    PkgConfig::FFMPEG
    "-framework OpenGL"
    "-framework CoreFoundation"
    "-framework CoreGraphics"
    "-framework CoreText"
    "-framework AppKit"
)

if(EXISTS ${SKIA_LIB})
    list(APPEND COMMON_LIBRARIES ${SKIA_LIB})
endif()

# ==================== 测试 1: FBO Pool 测试 ====================
add_executable(fbo_pool_test
    fbo_pool_test/main.cpp
    $<TARGET_OBJECTS:video_player_core>
)

target_link_libraries(fbo_pool_test PRIVATE
    video_player_core
    ${COMMON_LIBRARIES}
)

# ==================== 测试 2: Resource Loader 测试 ====================
add_executable(resource_loader_test
    resource_loader/main.cpp
    $<TARGET_OBJECTS:video_player_core>
)

target_link_libraries(resource_loader_test PRIVATE
    video_player_core
    ${COMMON_LIBRARIES}
)

# ==================== 测试 3: Decoder 测试 ====================
add_executable(decoder_test
    decoder_test/main.cpp
    $<TARGET_OBJECTS:video_player_core>
)

target_link_libraries(decoder_test PRIVATE
    video_player_core
    ${COMMON_LIBRARIES}
)

# ==================== 测试 4: Video Render 测试 ====================
add_executable(video_render_test
    video_render_test/main.cpp
    $<TARGET_OBJECTS:video_player_core>
)

target_link_libraries(video_render_test PRIVATE
    video_player_core
    ${COMMON_LIBRARIES}
)

# ==================== 测试 5: Skia 验证 Demo ====================
# 查找 GLFW
pkg_check_modules(GLFW REQUIRED glfw3)

# 检查 Skia 是否已编译
if(NOT EXISTS ${SKIA_LIB})
    message(WARNING "❌ Skia 未编译，跳过 skia_demo")
else()
    add_executable(skia_demo
        skia_demo/main.cpp
    )

    target_include_directories(skia_demo PRIVATE
        ${SKIA_ROOT}
        ${GLFW_INCLUDE_DIRS}
    )

    target_link_directories(skia_demo PRIVATE
        ${GLFW_LIBRARY_DIRS}
    )

    target_link_libraries(skia_demo PRIVATE
        ${SKIA_LIB}
        ${GLFW_LDFLAGS}
    )

    target_compile_definitions(skia_demo PRIVATE
        SK_GL
        SK_RELEASE
    )

    # macOS 系统框架
    if(APPLE)
        find_library(CORE_FOUNDATION CoreFoundation REQUIRED)
        find_library(CORE_GRAPHICS CoreGraphics REQUIRED)
        find_library(CORE_TEXT CoreText REQUIRED)
        find_library(OPENGL_FRAMEWORK OpenGL REQUIRED)
        
        target_link_libraries(skia_demo PRIVATE
            ${CORE_FOUNDATION}
            ${CORE_GRAPHICS}
            ${CORE_TEXT}
            ${OPENGL_FRAMEWORK}
        )
    endif()

    message(STATUS "✅ Skia demo 已配置")
endif()

# ==================== 自定义目标：运行所有测试 ====================
add_custom_target(run_tests
    COMMAND echo "=========================================="
    COMMAND echo "Running FBO Pool Test..."
    COMMAND echo "=========================================="
    COMMAND fbo_pool_test || true
    COMMAND echo ""
    COMMAND echo "=========================================="
    COMMAND echo "Running Resource Loader Test..."
    COMMAND echo "=========================================="
    COMMAND resource_loader_test ${CMAKE_SOURCE_DIR}/test || true
    COMMAND echo ""
    COMMAND echo "=========================================="
    COMMAND echo "Running Decoder Test..."
    COMMAND echo "=========================================="
    COMMAND decoder_test || true
    COMMAND echo ""
    COMMAND echo "=========================================="
    COMMAND echo "Running Video Render Test..."
    COMMAND echo "=========================================="
    COMMAND video_render_test || true
    COMMAND echo ""
    DEPENDS fbo_pool_test resource_loader_test decoder_test video_render_test
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/test
)
