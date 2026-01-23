# Electron + C++ Addon 着色器渲染 Demo

验证 Electron 渲染进程中 C++ Native Addon 的能力。

## 功能点

### 1. C++ OpenGL 着色器渲染
- C++ 创建独立的 OpenGL 3.2 Core 上下文（CGL）
- 使用 GLSL 着色器绘制纯色到 FBO
- `glReadPixels` 读取像素数据返回前端
- 前端通过 `putImageData` 显示到 Canvas

### 2. 本地文件系统访问
- C++ 使用 `std::filesystem` 读取系统下载目录
- 返回文件列表（文件名、大小、是否目录）
- 证明渲染进程中的 Native Addon 拥有完整系统权限

### 3. 架构特点
```
渲染进程 (nodeIntegration: true)
├── index.html
│   └── require('./build/Release/skia_render')
│       └── C++ Addon
│           ├── OpenGL 渲染
│           └── 文件系统访问
```

## 运行

```bash
npm install    # 安装依赖 + 编译 C++ Addon
npm start      # 启动应用
```

## 技术栈

- Electron 28
- Node.js N-API (node-addon-api)
- OpenGL 3.2 Core (macOS CGL)
- C++17 (std::filesystem)

## 文件结构

```
├── src/skia_render.cc   # C++ Addon 源码
├── binding.gyp          # node-gyp 编译配置
├── main.js              # Electron 主进程
├── index.html           # 前端页面
└── package.json
```

