cmake_minimum_required(VERSION 3.15)
project(video_player)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 设置默认构建类型（如果未指定）
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type (Debug or Release)" FORCE)
endif()

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15")
endif()

# 覆盖率插桩（仅 GTest 模式开启，gcov + gcovr）
option(BUILD_COVERAGE "Enable gcov coverage instrumentation (only for tests)" OFF)

# ========== 依赖 ==========

# Node.js / N-API
execute_process(
    COMMAND node -p "require('node-addon-api').include"
    OUTPUT_VARIABLE NODE_ADDON_API_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
string(REPLACE "\"" "" NODE_ADDON_API_DIR ${NODE_ADDON_API_DIR})

execute_process(
    COMMAND node -p "require('node-api-headers').include_dir"
    OUTPUT_VARIABLE NODE_API_HEADERS_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
string(REPLACE "\"" "" NODE_API_HEADERS_DIR ${NODE_API_HEADERS_DIR})

# FFmpeg
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED
    libavcodec
    libavformat
    libavutil
    libswscale
)

# ========== 三方库 ==========

# Skia 2D Graphics Library（预编译静态库 + 模块）
set(SKIA_ROOT ${CMAKE_SOURCE_DIR}/third_party/skia)
set(SKIA_OUT ${SKIA_ROOT}/out/Release)
set(SKIA_LIB ${SKIA_OUT}/libskia.a)

set(SKIA_PARAGRAPH_LIB ${SKIA_OUT}/libskparagraph.a)
set(SKIA_SHAPER_LIB ${SKIA_OUT}/libskshaper.a)
set(SKIA_UNICODE_CORE_LIB ${SKIA_OUT}/libskunicode_core.a)
set(SKIA_UNICODE_ICU_LIB ${SKIA_OUT}/libskunicode_icu.a)
set(SKIA_HARFBUZZ_LIB ${SKIA_OUT}/libharfbuzz.a)
set(SKIA_ICU_LIB ${SKIA_OUT}/libicu.a)

if(NOT EXISTS ${SKIA_LIB})
    message(WARNING "⚠️ Skia 未编译 (${SKIA_LIB})，TextLayer 将不可用")
endif()

# ANGLE / EGL + GLES3
# macOS：使用 Skia 构建产出的 ANGLE Metal 后端
# Linux/Windows：使用系统 EGL/GLES
if(APPLE)
    set(ANGLE_INCLUDE_DIR "${SKIA_ROOT}/third_party/externals/angle2/include")
    set(ANGLE_LIB_DIR "${SKIA_OUT}")
    if(NOT EXISTS "${ANGLE_LIB_DIR}/libEGL.dylib")
        message(FATAL_ERROR "ANGLE not found at ${ANGLE_LIB_DIR}. Run scripts/build_skia.sh first.")
    endif()
else()
    pkg_check_modules(SYSTEM_EGL REQUIRED egl glesv2)
endif()

# ========== 源文件 ==========

# 使用递归扫描自动收集源文件（使用 CMake 内置变量）
# CONFIGURE_DEPENDS: 每次构建时检测文件变化（CMake 3.12+）
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/*.cpp"
)

# 排除 N-API 绑定层和备份文件
list(FILTER CORE_SOURCES EXCLUDE REGEX ".*/addon\\.cpp$")
list(FILTER CORE_SOURCES EXCLUDE REGEX ".*/addon/.*")
list(FILTER CORE_SOURCES EXCLUDE REGEX ".*_backup.*")
list(FILTER CORE_SOURCES EXCLUDE REGEX ".*\\.bak$")

# ========== 核心库（OBJECT 库，编译一次，多处使用）==========

# 创建 OBJECT 库：核心渲染引擎
add_library(video_player_core OBJECT
    ${CORE_SOURCES}
)

# 核心库的编译配置
target_include_directories(video_player_core PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party
    ${FFMPEG_INCLUDE_DIRS}
    ${SKIA_ROOT}
    $<$<PLATFORM_ID:Darwin>:${ANGLE_INCLUDE_DIR}>
    $<$<NOT:$<PLATFORM_ID:Darwin>>:${SYSTEM_EGL_INCLUDE_DIRS}>
)

target_compile_definitions(video_player_core PUBLIC
    SK_GL
    SK_RELEASE
    VP_RESOURCES_DIR="${CMAKE_SOURCE_DIR}/resources"
)

target_compile_options(video_player_core PRIVATE 
    -Wall -Wextra -Wno-unused-parameter
)

# ========== Node.js 插件（主目标）==========

add_library(${PROJECT_NAME} SHARED
    src/addon.cpp
    src/addon/root_wrap.cpp
    src/addon/group_layer_wrap.cpp
    src/addon/layer_wrap.cpp
    $<TARGET_OBJECTS:video_player_core>
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    PREFIX ""
    SUFFIX ".node"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/Release"
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${NODE_ADDON_API_DIR}
    ${NODE_API_HEADERS_DIR}
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    NAPI_VERSION=8
    NAPI_DISABLE_CPP_EXCEPTIONS
)

# 链接核心库（继承其配置）
target_link_libraries(${PROJECT_NAME} PRIVATE video_player_core)

# ---- 平台特定依赖 ----
if(APPLE)
    find_library(COCOA_LIB Cocoa REQUIRED)
    find_library(CF_LIB CoreFoundation REQUIRED)
    find_library(COREVIDEO_LIB CoreVideo REQUIRED)
    find_library(IOSURFACE_LIB IOSurface REQUIRED)
    find_library(CORE_GRAPHICS_LIB CoreGraphics REQUIRED)
    find_library(CORE_TEXT_LIB CoreText REQUIRED)

    target_link_libraries(${PROJECT_NAME} PRIVATE
        "${ANGLE_LIB_DIR}/libEGL.dylib"
        "${ANGLE_LIB_DIR}/libGLESv2.dylib"
        ${COCOA_LIB} ${CF_LIB}
        ${COREVIDEO_LIB} ${IOSURFACE_LIB}
        ${CORE_GRAPHICS_LIB} ${CORE_TEXT_LIB}
    )
    set_target_properties(${PROJECT_NAME} PROPERTIES
        LINK_FLAGS "-undefined dynamic_lookup"
    )
elseif(WIN32 OR UNIX)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${SYSTEM_EGL_LIBRARIES})
    target_link_directories(${PROJECT_NAME} PRIVATE ${SYSTEM_EGL_LIBRARY_DIRS})
endif()

# ---- 公共依赖（所有平台） ----
target_link_libraries(${PROJECT_NAME} PRIVATE ${FFMPEG_LIBRARIES})
target_link_directories(${PROJECT_NAME} PRIVATE ${FFMPEG_LIBRARY_DIRS})

if(EXISTS ${SKIA_LIB})
    target_link_libraries(${PROJECT_NAME} PRIVATE
        ${SKIA_PARAGRAPH_LIB}
        ${SKIA_SHAPER_LIB}
        ${SKIA_UNICODE_ICU_LIB}
        ${SKIA_UNICODE_CORE_LIB}
        ${SKIA_HARFBUZZ_LIB}
        ${SKIA_ICU_LIB}
        ${SKIA_LIB}
    )
endif()

target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wno-unused-parameter)

# ========== 测试 ==========
# 启用方式：cmake -DBUILD_TESTING=ON -DBUILD_COVERAGE=ON  （或 ./build.sh GTest）
# 输出：${CMAKE_BINARY_DIR}/video_player_tests

include(CTest)   # 注册标准 BUILD_TESTING 选项 + ctest 支持

if(BUILD_TESTING)
    if(BUILD_COVERAGE)
        # 覆盖率插桩也作用于核心库（测试时重新链接）
        target_compile_options(video_player_core PRIVATE -O0 -g --coverage)
        target_link_options(video_player_core PUBLIC --coverage)
    endif()

    # ---- googletest（submodule 源码构建，产物隔离到 build 树）----
    add_subdirectory(
        ${CMAKE_SOURCE_DIR}/third_party/googletest
        ${CMAKE_BINARY_DIR}/googletest
        EXCLUDE_FROM_ALL
    )

    # ---- 测试源文件（test/*_test.cpp 扁平结构，无子目录）----
    file(GLOB VP_TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/test/*_test.cpp"
    )

    add_executable(video_player_tests
        ${VP_TEST_SOURCES}
        $<TARGET_OBJECTS:video_player_core>
    )

    # 输出目录：build/<BuildType>/video_player_tests
    set_target_properties(video_player_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    )

    target_link_libraries(video_player_tests PRIVATE
        video_player_core
        GTest::gtest_main
    )

    # 测试数据目录（媒体文件、JSON 配置）+ 资源目录（特效包）
    target_compile_definitions(video_player_tests PRIVATE
        VP_TEST_DIR="${CMAKE_SOURCE_DIR}/test"
        VP_RESOURCES_DIR="${CMAKE_SOURCE_DIR}/resources"
        VP_TEST_BINARY_DIR="${CMAKE_BINARY_DIR}"
    )

    # ---- 平台依赖（与主目标一致）----
    if(APPLE)
        target_link_libraries(video_player_tests PRIVATE
            "${ANGLE_LIB_DIR}/libEGL.dylib"
            "${ANGLE_LIB_DIR}/libGLESv2.dylib"
            ${COCOA_LIB} ${CF_LIB}
            ${COREVIDEO_LIB} ${IOSURFACE_LIB}
            ${CORE_GRAPHICS_LIB} ${CORE_TEXT_LIB}
        )
        set_target_properties(video_player_tests PROPERTIES
            BUILD_RPATH "${ANGLE_LIB_DIR}"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        )
    elseif(WIN32 OR UNIX)
        target_link_libraries(video_player_tests PRIVATE ${SYSTEM_EGL_LIBRARIES})
        target_link_directories(video_player_tests PRIVATE ${SYSTEM_EGL_LIBRARY_DIRS})
    endif()

    # FFmpeg
    target_link_libraries(video_player_tests PRIVATE ${FFMPEG_LIBRARIES})
    target_link_directories(video_player_tests PRIVATE ${FFMPEG_LIBRARY_DIRS})

    # Skia
    if(EXISTS ${SKIA_LIB})
        target_link_libraries(video_player_tests PRIVATE
            ${SKIA_PARAGRAPH_LIB}
            ${SKIA_SHAPER_LIB}
            ${SKIA_UNICODE_ICU_LIB}
            ${SKIA_UNICODE_CORE_LIB}
            ${SKIA_HARFBUZZ_LIB}
            ${SKIA_ICU_LIB}
            ${SKIA_LIB}
        )
    endif()

    if(BUILD_COVERAGE)
        target_link_options(video_player_tests PRIVATE --coverage)
    endif()

    # ---- CTest 注册 ----
    # PRE_TEST 模式：在 ctest 运行前才发现用例，不产生 cmake_test_discovery_*.json 副产物。
    include(GoogleTest)
    gtest_discover_tests(video_player_tests
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DISCOVERY_MODE PRE_TEST
    )
endif()
