# Electron Video Rendering Engine

基于 Electron + C++ Native Addon 的高性能视频渲染引擎，专为视频编辑和合成场景设计。

![截图](test/image.png)

## 核心特性

- **多图层合成**：支持多轨道视频合成，类似 After Effects 的时间轴架构
- **高性能解码**：CPU解码优化，避免GPU传输开销
- **OpenGL离屏渲染**：FBO + 纹理渲染管线，无需显示窗口
- **异步预加载**：后台线程预渲染下一帧，流畅播放体验
- **零拷贝设计**：渲染结果直接传递给 JavaScript，减少内存拷贝

## 架构设计

### 整体架构

```
┌─────────────────────────────────────┐
│        Electron 前端层               │
│  - 播放控制 (play/pause/seek)       │
│  - Canvas 显示                       │
│  - 时间轴 UI                         │
└──────────────┬──────────────────────┘
               │ N-API
┌──────────────▼──────────────────────┐
│        C++ Native 渲染引擎           │
│                                      │
│  合成器层：多图层管理 + 缓存         │
│      ↓                               │
│  图层层：视频/图片图层抽象           │
│      ↓                               │
│  解码层：FFmpeg 视频解码             │
│      ↓                               │
│  渲染层：OpenGL 纹理 + FBO          │
└──────────────────────────────────────┘
```

### 数据流

```
视频文件 → FFmpeg解码(CPU) → RGBA内存 → OpenGL纹理 
         → 多图层合成 → FBO渲染 → 读回CPU → JavaScript
```

## 技术方案

### 渲染管线

```
视频文件 → FFmpeg CPU解码 → RGBA内存 → OpenGL纹理 
→ 多图层合成 → FBO渲染 → 读回CPU → JavaScript
```

- **解码**：FFmpeg CPU解码，YUV转RGBA
- **上传**：RGBA数据上传到GPU纹理
- **合成**：OpenGL多图层叠加渲染到FBO
- **输出**：FBO像素数据读回CPU，传递给JavaScript

### 异步预渲染策略

```
用户请求帧 N
  ↓
命中缓存？→ 是 → 直接返回（<1ms）
  ↓ 否
实时渲染帧 N（20-40ms）
  ↓
后台异步准备帧 N+1
  ↓
用户请求帧 N+1 → 命中缓存 ✓
```

- **顺序播放**：缓存命中率 95%+，流畅无卡顿
- **跳转/拖动**：首帧延迟可接受，后续帧快速响应

## 使用方式

### 构建

```bash
# 安装依赖 (macOS)
brew install cmake ffmpeg pkg-config

# 编译
npm install
npm run build

# 运行
npm start
```

### API 示例

```javascript
const player = require('./build/Release/video_player');

// 初始化
player.init();
player.load(JSON.stringify(config));

// 播放控制
player.setCurrentTime(5000);  // 跳转到 5秒
const pixels = player.draw(); // 获取当前帧 RGBA 像素

// 查询信息
const info = player.getInfo();
console.log(info.width, info.height, info.durationMs);
```

## 性能特点

- **解码性能**：1080p视频单帧解码 10-30ms
- **合成性能**：OpenGL多图层合成 <5ms
- **总延迟**：首帧渲染 20-40ms，缓存命中 <1ms
- **内存占用**：单帧缓存约 8MB（1920×1080×4字节）

## 技术要点

| 技术点 | 实现方案 |
|--------|----------|
| OpenGL Y轴翻转 | 双FBO + 翻转渲染 |
| 内存拷贝优化 | 直接写入 ArrayBuffer |
| 播放流畅度 | 异步预渲染 + 单帧缓存 |
| 多图层合成 | FBO离屏渲染 + 纹理混合 |

## License

MIT
