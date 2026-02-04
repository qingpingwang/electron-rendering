#!/bin/bash

cd "$(dirname "$0")/.."

echo "=== Building FBO Pool Test ==="

# 编译测试程序
g++ -std=c++17 \
    -I./src \
    -I./third_party \
    -framework OpenGL \
    -framework Cocoa \
    -framework CoreFoundation \
    -framework CoreVideo \
    -framework IOSurface \
    -DGL_SILENCE_DEPRECATION \
    test/fbo_pool_test.cpp \
    src/gl/types.cpp \
    src/gl/functions.cpp \
    src/gl/fbo_pool.cpp \
    third_party/stb_image/stb_image_impl.cpp \
    -o test/fbo_pool_test

if [ $? -eq 0 ]; then
    echo "✓ Build successful"
    echo ""
    echo "=== Running Tests ==="
    ./test/fbo_pool_test
    exit_code=$?
    
    echo ""
    if [ $exit_code -eq 0 ]; then
        echo "✓ All tests passed!"
    else
        echo "✗ Some tests failed (exit code: $exit_code)"
    fi
    exit $exit_code
else
    echo "✗ Build failed"
    exit 1
fi
