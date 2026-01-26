cmake_minimum_required(VERSION 3.15)
project(video_player)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15")
    add_definitions(-DGL_SILENCE_DEPRECATION)
endif()

# ========== 依赖 ==========

# nlohmann/json (header-only)
include(FetchContent)
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)
FetchContent_MakeAvailable(json)

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

# ========== 源文件 ==========

set(SOURCES
    src/addon.cpp
    src/core/root_node.cpp
    src/decoder/video_decoder.cpp
    src/render/gl_renderer.cpp
    src/layer/video_layer.cpp
)

# ========== 构建 ==========

add_library(${PROJECT_NAME} SHARED ${SOURCES})

set_target_properties(${PROJECT_NAME} PROPERTIES
    PREFIX ""
    SUFFIX ".node"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/Release"
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${NODE_ADDON_API_DIR}
    ${NODE_API_HEADERS_DIR}
    ${FFMPEG_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    NAPI_VERSION=8
    NAPI_DISABLE_CPP_EXCEPTIONS
)

if(APPLE)
    find_library(OPENGL_LIB OpenGL REQUIRED)
    find_library(COCOA_LIB Cocoa REQUIRED)
    find_library(CF_LIB CoreFoundation REQUIRED)
    
    target_link_libraries(${PROJECT_NAME} PRIVATE
        ${OPENGL_LIB}
        ${COCOA_LIB}
        ${CF_LIB}
        ${FFMPEG_LIBRARIES}
        nlohmann_json::nlohmann_json
    )
    target_link_directories(${PROJECT_NAME} PRIVATE ${FFMPEG_LIBRARY_DIRS})
    
    set_target_properties(${PROJECT_NAME} PROPERTIES
        LINK_FLAGS "-undefined dynamic_lookup"
    )
elseif(UNIX)
    find_package(OpenGL REQUIRED)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        OpenGL::GL
        ${FFMPEG_LIBRARIES}
        nlohmann_json::nlohmann_json
    )
    target_link_directories(${PROJECT_NAME} PRIVATE ${FFMPEG_LIBRARY_DIRS})
endif()

target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wno-unused-parameter)
