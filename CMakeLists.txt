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
    add_definitions(-DGL_SILENCE_DEPRECATION)
endif()

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

# glm (header-only math library, via submodule)
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/glm)

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

# GLAD（非 Apple 平台的 GL 函数加载器）
set(GLAD_ROOT ${CMAKE_SOURCE_DIR}/third_party/glad)

# ========== 源文件 ==========

# 使用递归扫描自动收集源文件（使用 CMake 内置变量）
# CONFIGURE_DEPENDS: 每次构建时检测文件变化（CMake 3.12+）
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/*.cpp"
)

# 第三方库源文件（按需显式添加）
set(THIRD_PARTY_SOURCES
    ${CMAKE_SOURCE_DIR}/third_party/stb_image/stb_image_impl.cpp
    ${CMAKE_SOURCE_DIR}/third_party/stb_image/stb_image_write_impl.cpp
)

# GLAD 源文件（.c，仅非 Apple 平台）
if(NOT APPLE)
    set(GLAD_SOURCES ${GLAD_ROOT}/src/glad.c)
endif()

# 排除 N-API 绑定层和备份文件
list(FILTER CORE_SOURCES EXCLUDE REGEX ".*/addon\\.cpp$")
list(FILTER CORE_SOURCES EXCLUDE REGEX ".*/addon/.*")
list(FILTER CORE_SOURCES EXCLUDE REGEX ".*_backup.*")
list(FILTER CORE_SOURCES EXCLUDE REGEX ".*\\.bak$")

# ========== 核心库（OBJECT 库，编译一次，多处使用）==========

# 创建 OBJECT 库：核心渲染引擎
add_library(video_player_core OBJECT
    ${CORE_SOURCES}
    ${THIRD_PARTY_SOURCES}
    ${GLAD_SOURCES}
)

# 核心库的编译配置
target_include_directories(video_player_core PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party
    ${FFMPEG_INCLUDE_DIRS}
    ${SKIA_ROOT}
    $<$<NOT:$<PLATFORM_ID:Darwin>>:${GLAD_ROOT}/include>
)

target_compile_definitions(video_player_core PUBLIC
    $<$<PLATFORM_ID:Darwin>:GL_SILENCE_DEPRECATION>
    SK_GL
    SK_RELEASE
)

target_link_libraries(video_player_core PUBLIC glm::glm)

target_compile_options(video_player_core PRIVATE 
    -Wall -Wextra -Wno-unused-parameter
)

# 第三方库禁用所有警告
set_source_files_properties(${THIRD_PARTY_SOURCES} PROPERTIES
    COMPILE_FLAGS "-w"
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
    find_library(OPENGL_LIB OpenGL REQUIRED)
    find_library(COCOA_LIB Cocoa REQUIRED)
    find_library(CF_LIB CoreFoundation REQUIRED)
    find_library(COREVIDEO_LIB CoreVideo REQUIRED)
    find_library(IOSURFACE_LIB IOSurface REQUIRED)
    find_library(CORE_GRAPHICS_LIB CoreGraphics REQUIRED)
    find_library(CORE_TEXT_LIB CoreText REQUIRED)

    target_link_libraries(${PROJECT_NAME} PRIVATE
        ${OPENGL_LIB} ${COCOA_LIB} ${CF_LIB}
        ${COREVIDEO_LIB} ${IOSURFACE_LIB}
        ${CORE_GRAPHICS_LIB} ${CORE_TEXT_LIB}
    )
    set_target_properties(${PROJECT_NAME} PROPERTIES
        LINK_FLAGS "-undefined dynamic_lookup"
    )
elseif(WIN32)
    find_package(OpenGL REQUIRED)
    target_link_libraries(${PROJECT_NAME} PRIVATE OpenGL::GL EGL)
elseif(UNIX)
    find_package(OpenGL REQUIRED)
    pkg_check_modules(EGL REQUIRED egl)
    target_link_libraries(${PROJECT_NAME} PRIVATE OpenGL::GL ${EGL_LIBRARIES})
    target_link_directories(${PROJECT_NAME} PRIVATE ${EGL_LIBRARY_DIRS})
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

# 测试子目录（可选，默认包含，适合开发）
option(BUILD_TESTS "Build test cases" ON)

if(BUILD_TESTS)
    add_subdirectory(test)
endif()
