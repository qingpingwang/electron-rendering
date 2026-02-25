# Electron Video Rendering Engine

基于 Electron + C++ Native Addon 的高性能视频渲染引擎，专为视频编辑和合成场景设计。

![截图](test/image.png)

## 核心特性

- **多图层合成**：支持多轨道视频合成，类似 After Effects 的时间轴架构
- **富文本渲染**：基于 Skia skparagraph 的文字排版，支持字间距、行高、多重描边、逐 run 阴影
- **高性能解码**：FFmpeg CPU 解码，YUV 转 RGBA
- **OpenGL 离屏渲染**：FBO + 纹理渲染管线，无需显示窗口
- **Skia GPU 文字**：共享 OpenGL 上下文，Skia 直接绘制到 FBO
- **OOP N-API 绑定**：ObjectWrap 将 C++ 对象映射为 JS 对象，支持多实例
- **异步预加载**：后台线程预渲染下一帧，流畅播放体验
- **零拷贝设计**：渲染结果直接传递给 JavaScript，减少内存拷贝

## 架构设计

### 整体架构

```
┌──────────────────────────────────────────┐
│           Electron 前端层                 │
│  - createRoot() 创建渲染器实例           │
│  - root.getLayers() 获取图层对象         │
│  - 播放控制 / Canvas 显示 / 时间轴 UI   │
└──────────────┬───────────────────────────┘
               │ N-API ObjectWrap
┌──────────────▼───────────────────────────┐
│        C++ Native 渲染引擎                │
│                                           │
│  绑定层：RootWrap / LayerWrap (OOP)      │
│      ↓                                    │
│  合成器层：RootNode 多图层管理 + 缓存    │
│      ↓                                    │
│  图层层：VideoLayer / TextLayer          │
│      ↓              ↓                     │
│  解码层：FFmpeg    文字层：Skia Paragraph │
│      ↓              ↓                     │
│  渲染层：OpenGL 纹理 + FBO              │
└───────────────────────────────────────────┘
```

### 数据流

```
视频文件 → FFmpeg 解码(CPU) → RGBA → OpenGL 纹理 ─┐
                                                     ├→ FBO 多图层合成 → 读回 CPU → JS
富文本 JSON → Skia ParagraphBuilder → GPU 绘制 ────┘
```

## 技术方案

### 渲染管线

- **视频解码**：FFmpeg CPU 解码，YUV 转 RGBA，上传到 GPU 纹理
- **文字渲染**：Skia skparagraph 模块，支持富文本样式（字间距、行高、描边、阴影）
- **图层合成**：OpenGL 多图层叠加渲染到 FBO
- **输出**：FBO 像素数据读回 CPU，传递给 JavaScript

### 文字渲染

基于 Skia `skparagraph` 模块实现富文本排版：

- **字间距 / 行高**：`TextStyle::setLetterSpacing` / `setHeight`
- **多重描边**：为每层描边构建独立 `Paragraph`，从外到内叠加绘制，描边宽度 = 比例 × 字号
- **逐 run 阴影**：每个富文本 run 独立应用 `SkImageFilters::DropShadowOnly`，支持角度、距离、扩散
- **字体管理**：`FontManager` 单例持有 `FontCollection` + `SkUnicode`，统一解析和缓存

### N-API OOP 绑定

通过 `Napi::ObjectWrap` 将 C++ 对象直接映射为 JS 对象：

```
createRoot()  →  RootWrap (owns RootNode)
                    │
                    ├── init() / load() / draw() / cleanup()
                    ├── width / height / durationMs (getters)
                    │
                    └── getLayers()  →  LayerWrap[] (holds Layer*)
                                          ├── name / type / startTime / endTime
                                          ├── text / alignment (TextLayer)
                                          └── videoFrameRate / videoLoaded (VideoLayer)
```

**生命周期安全**：LayerWrap 通过 generation counter 校验——`unload()` / `cleanup()` 后旧的 layer 引用自动失效，抛 JS 异常而非段错误。

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
const { createRoot, getVideoInfo } = require('./build/Release/video_player');

// 创建渲染器实例
const root = createRoot();
root.init();
root.load(JSON.stringify(config));

// 查询属性
console.log(root.width, root.height, root.durationMs);

// 获取图层
const layers = root.getLayers();
layers.forEach(layer => {
    console.log(layer.name, layer.type, layer.startTime, layer.endTime);
    if (layer.type === 'text') console.log(layer.text, layer.alignment);
});

// 播放控制
root.setCurrentTime(5000);
const pixels = root.draw();

// 清理
root.cleanup();
```

## 性能特点

- **解码性能**：1080p 视频单帧解码 10-30ms
- **合成性能**：OpenGL 多图层合成 <5ms
- **总延迟**：首帧渲染 20-40ms，缓存命中 <1ms
- **内存占用**：单帧缓存约 8MB（1920×1080×4 字节）

## 技术要点

| 技术点 | 实现方案 |
|--------|----------|
| 文字渲染 | Skia skparagraph + GPU 直绘 FBO |
| 富文本描边 | 多层 Paragraph 叠加，圆角连接 |
| 富文本阴影 | SkImageFilters::DropShadowOnly 逐 run 投影 |
| JS 绑定 | Napi::ObjectWrap OOP + generation counter |
| OpenGL Y轴翻转 | 四边形 UV 翻转 + stbi_flip |
| 内存拷贝优化 | 直接写入 ArrayBuffer |
| 播放流畅度 | 异步预渲染 + 单帧缓存 |
| 多图层合成 | FBO 离屏渲染 + 纹理混合 |

## License

MIT
