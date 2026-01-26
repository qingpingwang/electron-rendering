# Electron C++ Video Player

Electron + C++ Native Addon 视频播放器，使用 FFmpeg 解码 + OpenGL 渲染。

## 特性

- **FFmpeg 解码**：支持 MP4 等常见视频格式
- **OpenGL 渲染**：GPU 加速纹理渲染，FBO 离屏渲染
- **异步预渲染**：后台线程预解码下一帧，缓存命中时直接拷贝
- **帧率同步**：基于视频帧率的精确播放控制
- **零拷贝优化**：`glReadPixels` 直接写入 JS ArrayBuffer

## 架构

```
┌─────────────────────────────────────────────────────────────┐
│                    前端 (index.html)                         │
│  ┌───────────────────────────────────────────────────────┐  │
│  │  播放器状态管理 (JS)                                    │  │
│  │  - requestAnimationFrame + 帧率同步                    │  │
│  │  - play() / pause() / stop() / seek()                 │  │
│  └─────────────────────┬─────────────────────────────────┘  │
│                        │                                     │
│                        ▼ setCurrentTime(ms) + draw()         │
│  ┌───────────────────────────────────────────────────────┐  │
│  │              video_player.node (C++ Addon)             │  │
│  └───────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│                      C++ 后端                                │
│                                                              │
│  src/core/          RootNode - 合成器根节点                   │
│      │              └─ 异步预渲染 + 缓存管理                   │
│      │                                                       │
│  src/layer/         Layer 图层                               │
│      │              └─ VideoLayer - 视频图层                  │
│      │                                                       │
│  src/decoder/       VideoDecoder - FFmpeg 解码               │
│      │              └─ decodeFrameAt(ms) → RGBA              │
│      │                                                       │
│  src/render/        GLRenderer - OpenGL 渲染                 │
│                     └─ FBO + 纹理 + Y轴翻转                   │
└─────────────────────────────────────────────────────────────┘
```

## 异步预渲染

```
帧1 draw() → 渲染帧1 → 启动线程准备帧2
                            ↓ (异步)
帧2 draw() → 缓存命中 → 拷贝缓存 → 启动线程准备帧3
                                        ↓ (异步)
帧3 draw() → 缓存命中 → 拷贝缓存 → ...
```

- **缓存命中**：当前时间在缓存帧 ±半帧间隔内（25fps → ±20ms）
- **取消机制**：缓存未命中时取消正在进行的异步渲染

## API

```javascript
const addon = require('./build/Release/video_player');

addon.init();                        // 初始化 OpenGL
addon.load(jsonString);              // 加载配置 (JSON)
addon.setCurrentTime(5000);          // 设置时间 (ms)
const pixels = addon.draw();         // 渲染并获取 RGBA 像素
addon.getInfo();                     // { width, height, durationMs, frameRate, gpu }
addon.unload();                      // 卸载
addon.cleanup();                     // 清理
```

## JSON 配置格式

```json
{
  "canvas_config": { "width": 1920, "height": 1080 },
  "tracks": [
    {
      "type": "video",
      "segments": [
        { "id": "seg1", "material_id": "mat1" }
      ]
    }
  ],
  "materials": {
    "videos": [
      { "id": "mat1", "path": "test/test.mp4" }
    ]
  }
}
```

## 目录结构

```
src/
├── addon.cpp              # N-API 绑定
├── core/
│   ├── root_node.h
│   └── root_node.cpp      # RootNode + 异步预渲染
├── layer/
│   ├── layer.h            # Layer 基类
│   ├── video_layer.h
│   └── video_layer.cpp    # VideoLayer
├── decoder/
│   ├── video_decoder.h
│   └── video_decoder.cpp  # FFmpeg 解码
└── render/
    ├── gl_renderer.h
    └── gl_renderer.cpp    # OpenGL + 双FBO翻转
```

## 构建

```bash
# 安装依赖 (macOS)
brew install cmake ffmpeg pkg-config

# 构建
npm install
npm run build

# 运行
npm start
```

## 技术要点

| 问题 | 解决方案 |
|------|----------|
| OpenGL Y轴翻转 | 双 FBO + 普通着色器二次渲染 |
| 播放速度控制 | requestAnimationFrame + 帧间隔检测 |
| 内存拷贝优化 | glReadPixels 直接写入 Napi::ArrayBuffer |
| 异步渲染取消 | cancel_flag_ + 每 layer 检查 |
