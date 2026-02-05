# 渲染资源测试用例

## 资源列表

### 1. simple_effect - 简单亮度调节
- **类型**：单 pass 效果
- **用途**：测试基础渲染流程
- **参数**：亮度（0.0 - 2.0）

**文件：**
```
simple_effect/
  ├── config.json
  └── shaders/
      ├── simple.vert
      └── simple.frag
```

**测试要点：**
- ✓ 单 pass 渲染
- ✓ uniform 参数传递
- ✓ FBO 创建和复用

---

### 2. blur_effect - 高斯模糊
- **类型**：双 pass 效果
- **用途**：测试多 pass 依赖关系
- **参数**：模糊半径（1.0 - 20.0）

**文件：**
```
blur_effect/
  ├── config.json
  └── shaders/
      ├── blur.vert
      ├── blur_h.frag  (水平模糊)
      └── blur_v.frag  (垂直模糊)
```

**测试要点：**
- ✓ 多 pass 渲染
- ✓ pass 依赖（pass[1] 依赖 pass[0]）
- ✓ 智能 FBO 回收
- ✓ fboSize 降采样（pass[0] 使用 0.5）
- ✓ 动画支持

**渲染流程：**
```
input (1920x1080)
    ↓
Pass 0: 水平模糊 → fbo[0] (960x540, 降采样)
    ↓
Pass 1: 垂直模糊（读取 fbo[0]）→ fbo[1] (1920x1080)
    ↓
释放 fbo[0]（后续无依赖）
    ↓
返回 fbo[1]
```

---

## 使用方法

### 加载资源

```cpp
#include "resource/render_resource.h"

// 创建资源
auto resource = std::make_unique<RenderResource>(root);

// 加载（传入文件夹路径）
if (!resource->loadFromFolder("./test/resources/blur_effect")) {
    // 加载失败
    return;
}
```

### 执行渲染

```cpp
// 输入 FBO
gl::FBO input = /* 输入帧 */;

// 渲染
gl::FBO output = resource->render(input, current_time_ms);

// 使用输出
if (output.isValid()) {
    // 绘制到最终目标
    gl::drawTextureQuad(render_fbo, 
                        gl::Texture{output.texture, output.width, output.height},
                        shader, 0, "uTex", quad);
}
```

### 调整参数

```cpp
// 调整模糊半径
resource->setFloatParam("模糊半径", 10.0f);

// 重新渲染
gl::FBO output = resource->render(input, current_time_ms);
```

---

## 配置说明

### renderPass

| 字段 | 类型 | 说明 |
|------|------|------|
| fboSize | float | FBO 大小比例（1.0=原尺寸，0.5=半尺寸） |
| shader.vert | string | 顶点着色器路径（相对） |
| shader.frag | string | 片段着色器路径（相对） |
| asInputTexIndex | array | 作为其他 pass 输入的定义 |

### asInputTexIndex

| 字段 | 类型 | 说明 |
|------|------|------|
| name | string | shader 中的 uniform 变量名 |
| pipe | int | 纹理单元（0,1,2...） |
| renderPassIndex | int | 来自哪个 pass 的输出 |

### uniform

| 字段 | 类型 | 说明 |
|------|------|------|
| name | string | 外部显示名称 |
| type | string | float/boolean/animation |
| uniformTarget | string | shader 中的变量名 |
| defaultValue | number | 默认值 |
| renderPassIndex | array | 影响的 pass 列表 |

### animation

| 字段 | 类型 | 说明 |
|------|------|------|
| name | string | 动画名称 |
| channelNum | int | 通道数（1,2,3,4） |
| strength | float | 动画强度 |
| speed | float | 动画速度 |
| repeatMode | int | 0=停止, 2=循环 |
| interpolationType | string | linear/cubic |
| animationInfo | array | 关键帧数据 |

---

## 状态

✅ 基础框架完成
✅ 测试资源准备
□ 集成测试
□ Layer 集成
