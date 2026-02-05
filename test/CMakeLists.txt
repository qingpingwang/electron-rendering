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

# 通用链接库
set(COMMON_LIBRARIES
    PkgConfig::FFMPEG
    "-framework OpenGL"
    "-framework CoreFoundation"
    "-framework CoreGraphics"
    "-framework AppKit"
)

# ==================== 测试 1: FBO Pool 测试 ====================
add_executable(fbo_pool_test
    fbo_pool_test/main.cpp
    $<TARGET_OBJECTS:video_player_core>
)

target_link_libraries(fbo_pool_test PRIVATE
    video_player_core
    ${COMMON_LIBRARIES}
)

set_target_properties(fbo_pool_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/fbo_pool_test"
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

set_target_properties(resource_loader_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/resource_loader"
)

# ==================== 自定义目标：运行所有测试 ====================
add_custom_target(run_tests
    COMMAND echo "=========================================="
    COMMAND echo "Running FBO Pool Test..."
    COMMAND echo "=========================================="
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/fbo_pool_test/fbo_pool_test || true
    COMMAND echo ""
    COMMAND echo "=========================================="
    COMMAND echo "Running Resource Loader Test..."
    COMMAND echo "=========================================="
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/resource_loader/resource_loader_test ${CMAKE_SOURCE_DIR}/test/resources/ || true
    DEPENDS fbo_pool_test resource_loader_test
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
