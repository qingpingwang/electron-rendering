# Electron Video Rendering Engine

仓库地址：[electron-rendering](https://github.com/qingpingwang/electron-rendering.git)

**AI 原生的视频编排工作台**：在 Electron 中融合 **LangGraph 智能体**、**工程级持久化** 与 **C++/OpenGL/Skia 实时渲染管线**，让自然语言驱动时间轴、图层与素材——所见即所得，所聊即所改。

![产品界面](resources/res/image.png)

---

## 智能编排 · AI Copilot

- **对话即操作**：内嵌基于 **LangGraph** 的编排代理，理解工程上下文，将意图拆解为对图层、文案与素材的可执行变更。
- **流式认知输出**：支持带推理链路的模型；流式响应按 **思考 → 工具调用 → 总结** 分段呈现，避免信息混叠，可读性与可调试性兼备。
- **结构化工具调用 UI**：每一次 `function calling` 以独立卡片展示（工具名、参数、执行状态与结果），人机协作过程**全程可观测**，而非黑盒文本。
- **动态工程上下文**：会话与工程状态联动，代理可读取当前合成树、素材库等结构化信息，减少「幻觉式」编辑建议。

> 技术栈要点：**LangChain / LangGraph**、可插拔 LLM（含思考/推理模式）、**N-API** 桥接原生渲染与前端 UI。

## 渲染资源 Agent · Shader 特效对话创作

**用自然语言创作 GLSL Shader 特效与转场**——无需手写一行 OpenGL 代码，在对话中描述意图，AI 实时生成并预览。

![渲染资源 Agent](resources/res/image1.png)

### 工作流程

1. **新建对话**：点击首页「新建对话」，进入资源聊天室（零文件足迹，无需手动命名工程）。
2. **描述意图**：用中文描述想要的特效，例如 *"创建一个基于 sin 函数的波浪扭曲画面特效"*。
3. **AI 自动创作**：Agent 调用 `list_dir` → `write_file` 工具，生成完整的资源工程结构：
   ```
   warp_effect/
   ├── config.json          # 资源描述：名称、格式、参数声明
   └── shaders/
       └── pass0.frag       # GLSL 片段着色器
   ```
4. **自动挂载预览**：`config.json` 写入后触发自动挂载，左栏出现资源卡片，预览区实时呈现效果。
5. **参数调节**：右侧参数面板根据 `config.json` 中 `uniform[]` 声明动态生成滑块 / 开关，通过 N-API 实时调用 C++ 侧 `setMaterialFloatParam` 更新渲染。

### 三栏布局

| 区域 | 说明 |
|------|------|
| **左栏** 资源列表 | 扫描沙箱目录，展示特效 / 转场资源；点击卡片切换挂载状态 |
| **中栏** 实时预览 | 基于 C++ OpenGL 渲染管线，支持时间轴播放与拖动 |
| **右栏** 聊天 + 参数 | LangGraph 对话历史 + 已挂载资源的 uniform 参数面板 |
| **底栏** 日志 | 挂载状态、渲染耗时、工具调用实时输出 |

### Shader 内置 Uniform

Agent 生成的着色器可直接使用以下内置变量，**无需在 `config.json` 中声明**：

| 变量 | 类型 | 说明 |
|------|------|------|
| `uTime` | `float` | 当前播放时间（秒）|
| `uProgress` | `float` | 段内进度 `[0, 1]` |
| `inputTexture0` | `sampler2D` | 主输入纹理（特效 / 转场均有）|
| `inputTexture1` | `sampler2D` | 转场第二输入纹理 |

### 会话持久化

- 聊天历史通过 **LangGraph SqliteSaver** 按 `resource:<uuid>` 线程隔离存储，无直接 SQL 操作。
- 首页「最近对话」从 Checkpointer 实时读取，以第一条用户消息作为标题。
- 退出未对话的聊天室，系统自动删除空线程，不留历史记录。

---


- **SQLite Checkpointer**：对话与代理状态落盘至应用数据库（**SqliteSaver**），按 **工程 / 线程 ID（thread_id）** 隔离。
- **跨次打开可续聊**：切换项目即加载对应会话历史；新建工程自动初始化线程，删除工程可同步清理会话图谱。
- **历史与 UI 同源**：Checkpoint 中的 `HumanMessage` / `AIMessage` / `ToolMessage` 经统一序列化层映射为聊天时间线，**持久化记录与界面展示顺序一致**（含思考内容与工具轨迹）。

> 设计目标：**Never break userspace**——状态可恢复、可审计，适合长周期协作与复盘。

---

## 渲染性能 · 毫秒级合成

| 维度 | 说明 |
|------|------|
| **合成一帧** | 典型负载下，多图层离屏合成 **约 0.4 ms/帧**，为实时预览与批处理留出余量。 |
| **GPU 硬件解码** | **H.264 / HEVC** 在 **macOS** 上优先走 **VideoToolbox** 硬解，帧数据以 **CVPixelBuffer** 路径上传纹理，减少 CPU 色彩转换与 memcpy 压力。 |
| **GPU 文字** | **Skia** 与 **ANGLE** 共享 EGL 上下文，富文本直接绘制至 **FBO**，与视频纹理同一合成管线。 |
| **异步预取** | 后台预渲染下一帧，顺序播放缓存命中率高，交互拖拽时首帧可接受、后续快速跟上。 |

---

## 核心能力一览

- **多轨道时间轴**：类 After Effects 的非线性编排，视频 / 文本 / 音频分层管理。
- **富文本引擎**：基于 Skia **skparagraph**，字间距、行高、多重描边、逐 run 阴影。
- **离屏 ANGLE EGL/GLES3**：基于 ANGLE EGL Pbuffer 的离屏上下文，FBO + 纹理混合，无需可见窗口即可完成成片帧输出；macOS 使用 Metal 后端，Linux 使用 Surfaceless Mesa。
- **OOP N-API**：`ObjectWrap` 映射 `Root` / `Layer`，多实例、generation 校验，杜绝野指针泄漏到 JS。
- **零拷贝倾向**：渲染结果写入 `ArrayBuffer` 直出 JS，降低冗余拷贝。

---

## 架构鸟瞰

`third_party/nle-sdk` 是唯一直接子模块，统一提供渲染、图层、素材与编解码。
`src/` 仅保留 N-API 适配：对象生命周期、参数转换，以及双缓冲和下一帧异步预取的交接状态。
本仓库不保留 `vendor/`、`playback/`、`test/` 或 `work/` 目录；核心单测在 nle-sdk 维护。
macOS 构建并加载 `media_codec_apple`，其他平台使用 `media_codec_ffmpeg`。
编解码插件产物位于 `deploy/plugins`，需要与 `deploy/video_player.node`、`deploy/lib` 一起保留。


```
┌─────────────────────────────────────────────────────────────┐
│  Electron 前端 · 时间轴 / 预览 / 聊天                        │
│  AI：流式消息 · 工具卡片 · 会话历史                            │
└───────────────────────────┬─────────────────────────────────┘
                            │  IPC / N-API
┌───────────────────────────▼─────────────────────────────────┐
│  Agent 层（LangGraph + SqliteSaver）                          │
│  thread 隔离 · checkpoint 持久化 · 工具调用闭环                │
└───────────────────────────┬─────────────────────────────────┘
                            │
┌───────────────────────────▼─────────────────────────────────┐
│  nle-sdk 渲染引擎（ANGLE EGL/GLES3）                              │
│  RootNode 合成 → VideoLayer(Texture+HW 解码) + TextLayer(Skia) │
│  → FBO → glReadPixels → 像素回传 JS                           │
└─────────────────────────────────────────────────────────────┘
```

---

## 直接运行（不需要私有 SDK 源码）

当前 `deploy` 预编译产物适用于 **macOS Apple Silicon（arm64）**。其他平台需要维护者提供对应产物，不能混用。

```bash
brew install git-lfs ffmpeg
git clone https://github.com/qingpingwang/electron-rendering.git
cd electron-rendering
git lfs install --local
git lfs pull
npm install
npm start
```

不要使用 `--recurse-submodules`；运行无需初始化 `third_party/nle-sdk`。`npm install` 仅检查预编译产物、构建聊天 CSS 并适配 Electron 的 SQLite 依赖，不编译 NLE。视频预处理直接使用系统 FFmpeg。

`deploy/video_player.node` 为 N-API 8 入口，Skia、Lua、SoundTouch、NLE 核心静态链接到其中；`deploy/lib` 包含 ANGLE，`deploy/plugins` 包含解码插件。运行时通过相对 `.node` 的路径查找依赖，整个目录可随主仓库移动。`runtime.json` 记录平台/架构。缺少文件、尚未拉取 LFS 实体或平台不匹配时，启动前直接提示。

`deploy/` 和 `resources/` 整个目录由 Git LFS 管理，包括 JSON、着色器和脚本；不按扩展名区分，不重写历史。发布前需先推送 LFS 对象，普通 `git push` 的 LFS hook 会自动处理。

## 从源码重建（SDK 维护者）

```bash
brew install cmake ffmpeg pkg-config
bash scripts/init_submodule.sh
npm install
npm run build
# 同一构建入口也可通过脚本调用
./build.sh Release
# Debug 构建：./build.sh Debug
```

`npm run build`（默认 Release，Debug 可用 `npm run build -- Debug`）与 `./build.sh` 均调用主仓库的 `scripts/build_native.js`，统一构建 Skia/ANGLE、SDK、解码插件、N-API 适配层，再将运行产物复制到 `deploy`，中间文件与静态库仍保留在 `build`。发布前使用 Release 构建并验证，提交相应 LFS 文件。仅直接运行不需要这些步骤。

配置 **LLM**（如 `OPENAI_API_KEY`、方舟/兼容端点等）于项目根目录 `.env` 后，即可使用内置智能助手；数据库与 checkpoint 由应用自动维护。

### 渲染 API 示例

```javascript
const { createRoot, getVideoInfo } = require('./deploy/video_player.node');

const root = createRoot();
root.init();
root.load(JSON.stringify(config));

const groups = root.getGroups();
root.setCurrentTime(5000);
const { pixels } = root.draw();
root.cleanup();
```

---

## 技术矩阵

| 领域 | 实现 |
|------|------|
| AI 编排 | LangGraph、流式 messages、工具绑定与结构化回调 |
| 持久化 | SqliteSaver、thread 级 checkpoint、历史序列化 |
| **Shader 创作** | **GLSL 特效 / 转场 Agent**、`config.json` 声明式资源协议、uniform 参数面板 |
| 视频解码 | nle-sdk 插件：macOS media_codec_apple，其他平台 media_codec_ffmpeg |
| 合成 | ANGLE EGL/GLES3、FBO、纹理混合、硬件帧直传纹理 |
| 文字 | Skia skparagraph、GPU 直绘 |
| JS 绑定 | N-API ObjectWrap、generation 安全 |

---

## 交流

欢迎加入 **QQ 交流群**，群号：**523219063**。在 QQ 中搜索该群号即可加入，交流渲染管线、AI 编排与工程实践。

---

## License

MIT

## 编辑器画布操作

- 左侧导入视频；双击素材或拖入时间轴添加片段。切换「文本」后点击添加按钮创建文本。
- 点选画布中的图层，或选择时间轴片段：拖动选框移动，拖动角点等比缩放，拖动圆点旋转。
- 移动时按 Shift 约束水平/垂直方向；旋转时按 Shift 吸附到 15°；Escape 取消当前画布手势。
- ⌘Z / ⇧⌘Z 撤销、重做画布变换（Windows/Linux 使用 Ctrl）。属性面板修改或重载项目会清空这段历史。
- 在时间轴内横向拖动片段改变起始时间，吸附播放头；按 Alt 临时关闭吸附。
- 按 ⌘S 保存 SDK 导出的项目协议。支持同类型片段跨轨道拖拽，移空后自动删除轨道；暂未提供裁切手柄和全项目撤销。

## 本地资源目录

`resources/system/resources.json` 是编辑器素材库清单，路径相对于 `resources/system/`。分类为 `media`、`audio`、`texts`、`stickers`、`effects`、`transitions`、`captions`、`filters`、`adjustments`；没有资源的分类保持空数组。新增资源时填写唯一 `id`、`name`、`type` 和 `path`，特效与转场的路径指向包含 SDK `config.json` 的资源目录，转场 `duration` 单位为微秒。

当前收录 1 个视频、1 个音频、6 个特效、1 个转场。产品截图和特效内部依赖（例如蒙版视频）不作为独立素材收录；亮度资源遵循其 SDK 特效类型归入特效。特效与转场卡片使用配置中的 `preview_video`，预览区固定 88×88，下方显示名称（视频文件保持 200×200），鼠标悬浮静音循环播放，离开后暂停。

左侧支持分类、搜索、名称排序、本地/项目/素材库来源筛选和自适应卡片排列。视频、音频可添加到时间轴；特效应用到选中视频片段，转场应用到同轨道相邻片段之间。空资源分类仍显示入口与空状态。

编辑器上方三栏可拖动两条竖分割条，下方时间轴通过横分割条分配高度。素材区、播放器和属性区的最小宽度分别为 280、220、240 px；上半区和时间轴最小高度分别为 210、160 px。顶部不显示品牌与操作工具栏，保存使用 ⌘S，画布撤销/重做使用 ⌘Z / ⇧⌘Z。

日志在窗口顶部独立「日志」页查看，接收编辑器实时输出，保留最近 1000 条并支持清空。三条分割条的可见宽度均为 3 px。图层与轨道片段双向联动选择，选中当前时间之外的片段会定位到其开始时间。旋转按钮位于选框下方，按顺时针拖动即顺时针旋转。


### 视频预处理

工程打开时，前端从 `materials.videos` 收集视频绝对路径、去重，通过 Electron IPC 调用主进程的 `app/preprocess/cache.js`。系统需安装 `ffmpeg` 和 `ffprobe`；也可以设置 `NLE_FFMPEG_PATH`、`NLE_FFPROBE_PATH`。

```js
const { preprocess } = require('./app/preprocess/cache');
const results = await preprocess(videoPaths, cacheDir, onProgress);
```

`cacheDir` 为工程根目录下的 `.cache`。输出保存到 `.cache/{原视频绝对路径的 MD5}/`：`1fps_200.mp4`（长边最多 200、1fps、保留音频）、`720p.mp4`（横屏最多 1280×720，竖屏最多 720×1280，无音频）、`audio.m4a`（AAC、96kbps、44.1kHz、双声道）。无音轨时 `audio` 返回 `null`。存在的文件直接跳过，不检查损坏或源文件更新；需要重建时删除对应缓存文件。新文件写入临时文件，成功后改名。

返回数组与输入顺序一致，每项包含 `{ source, outputDir, thumbnail, video, audio, skipped }`。重复路径只处理一次，返回数组仍与输入一一对应。`skipped` 表示此次没有生成任何文件。

进度回调字段为 `{ type, status, total, completed, source?, index?, outputDir?, stage?, stages?, message?, result?, error? }`。`total` 是去重后的文件数，缓存命中计入 `completed`。事件依次包含 `batch-start`、`file-start`、`file-progress`、`file-complete`、`batch-complete`；文件失败发出 `file-error` 并拒绝 Promise。`stage` 为 `transcode`，`stages` 数组列出本次需要生成的 `thumbnail`、`video`、`audio`。IPC 请求使用 `media-cache:prepare`，进度使用 `media-cache:progress`，以 `requestId` 隔离请求。

SDK 视频层的 `setProxyPath(path)` 硬切换解码来源，`setProxyPath('')` 恢复原素材；`proxyPath` 可读取当前代理。项目协议和原始素材尺寸不变。时间轴及媒体卡片使用小视频，保留现有缩略图帧缓存、胶片缓存；前端音频使用分离文件，同文件共享一次解码。

预处理采用单输入、多输出的 FFmpeg 命令，通过 `split/asplit` 共用解码帧；只连接缺失输出分支。素材任务并发数设为系统可用逻辑核数；每个任务的解码、滤镜、小视频及主视频编码线程数也均设为该核数。这些是各阶段线程配置，不是整个进程的线程总数。

播放合成时，`RootNode::predecode(time_us)` 先遍历可见轨道，为当前时间的活动视频图层启动独立异步解码任务（包括转场两侧的实际取帧时间），然后按图层顺序绘制。实际绘制等待对应任务完成，再在渲染线程上传纹理；换素材、换代理、变为 inactive 和销毁图层前会等待任务收尾。原生 Apple / FFmpeg 插件共用该调度，Web 保持原异步解码路径。

缓存预处理仅在打开工程、导入视频时触发；普通编辑（移动片段、跨轨道移动、应用资源）直接使用当前工程的代理映射，不检查或构建缓存。导入时只处理未就绪的素材；缓存进度遮罩仅在 FFmpeg 实际转码时显示。

### 资源目录

- `resources/system/`：系统素材，按 `effect`、`transition`、`filter`、`sticker`、`font` 分类；素材清单位于 `resources/system/resources.json`。
- `resources/project/<工程ID>/protocol.json`：工程文件，素材路径相对于工程文件所在目录；新建工程也保存到此目录。
- `resources/res/`：README 引用图片等文档附件。

示例工程：`resources/project/test/protocol.json`、`resources/project/render_test/protocol.json`。自动生成的 `.cache` 仍忽略，不纳入 LFS。

工程的原视频、音频放在 `resources/project/<工程ID>/media/`，不登记到系统素材清单。每个工程包含三个文件：

- `protocol.json`：SDK 渲染协议。
- `draft_meta_info.json`：使用剪映字段 `draft_id`、`draft_name`、`tm_draft_create`、`tm_draft_modified`、`tm_duration`、`draft_materials`。`draft_materials` 的 `type: 0` 分组保存导入素材：`id`、`extra_info`（名称）、`metetype`、`file_Path`、`duration`、`width`、`height`、`import_time`。
- `draft_virtual_store.json`：沿用剪映的 `draft_virtual_store` 分组，`type: 0` 保存文件夹的 `id`、`display_name`，`type: 1` 保存 `child_id`、`parent_id`。空 ID 表示根目录，素材 ID 对应元数据登记。

两个附属文件由 `app/project_files.js` 在新建/打开工程时统一校正：补建缺失文件，协议中存在而元数据缺失的路径资源补入元数据并放到虚拟根目录，已有文件夹归属保留。导入素材登记到元数据并默认加入虚拟根目录，已有文件夹归属保留；保存时更新时长与修改时间，删除轨道片段不会删除已导入素材。只保留本地所需字段，不复制云端信息。`tm_*` 与素材 `duration` 使用微秒，`import_time` 使用秒；编辑器与 SDK 内部时间也统一使用微秒。`file_Path` 保存相对工程目录的路径，也可引用外部绝对路径，SDK 不读这两个文件。

`stickers` 清单仅包含 ID、名称和资源目录路径。Electron 的 `app/sticker.js` 读取该目录的 `config.json`，根据序列图尺寸或图片类型生成第一帧卡片；该解析器只负责展示，不转换 SDK 素材。

### 时间单位

工程协议、素材时长、时间范围、特效关键帧、`suggestionDuration`、`uTime`、SDK 与 N-API 时间均为整数微秒（1 秒 = 1,000,000）。接口使用 `TimeUs`、`durationUs`、`getDurationUs()`、`timeUs`；不提供旧毫秒协议兼容或自动猜测。仓库内示例已迁移，外部旧协议需显式转换后再加载。

WebAudio/HTMLVideoElement 仍按浏览器标准使用秒，WebCodecs 直接使用微秒，SkCodec 动图延迟从毫秒转为微秒；性能耗时、定时器等待保持各平台原生单位。解码插件 ABI 更新为 3，SDK 和插件需一起重新构建。
