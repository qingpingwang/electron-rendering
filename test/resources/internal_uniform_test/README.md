# 内部 Uniform 测试

## 概述

测试 renderPass 级别的内部 uniform（不对外暴露），展示内外部 uniform 的统一复用设计。

## 统一设计：复用 UniformParam

### 核心理念

内部 uniform 和外部 uniform **完全复用同一套代码**：

```cpp
// UniformParam 类 - 统一管理
class UniformParam {
    // 支持 float/vec2/vec3/vec4/boolean/animation/texture
};

// 外部 uniform - RenderResource 级别
std::vector<std::unique_ptr<UniformParam>> uniforms_;

// 内部 uniform - RenderPass 级别
std::vector<std::unique_ptr<UniformParam>> internal_uniforms_;
```

### 对比

| 维度 | 外部 Uniform | 内部 Uniform |
|------|-------------|-------------|
| **定义位置** | `RenderResource` 级别 | `RenderPass` 级别 |
| **配置位置** | 根级 `uniform[]` | `renderPass[i].uniform[]` |
| **管理类** | `UniformParam` | `UniformParam`（复用） |
| **对外暴露** | ✅ 用户可调整 | ❌ 不对外 |
| **修改接口** | `setFloatParam()` | 无（固定值） |
| **用途** | 用户参数化 | Pass 内部固定配置 |

## 配置示例

```json
{
    "renderPass": [
        {
            "shader": {...},
            "uniform": [                    // ← 内部 uniform
                {
                    "uniformTarget": "saturation",
                    "type": "float",
                    "value": 1.5            // 固定值
                },
                {
                    "uniformTarget": "tint",
                    "type": "vec3",
                    "value": [1.0, 0.9, 0.8]
                }
            ]
        }
    ],
    
    "uniform": [                            // ← 外部 uniform
        {
            "name": "亮度",
            "uniformTarget": "brightness",
            "type": "float",
            "defaultValue": 1.0             // 用户可调
        }
    ]
}
```

## 使用场景

### 外部 Uniform（用户可调）

```cpp
// 用户可以修改
resource->setFloatParam("亮度", 1.5f);
```

**适用于**：
- 需要用户交互的参数
- 需要动态调整的值
- 需要动画驱动的参数

### 内部 Uniform（固定配置）

**不提供修改接口**，在配置文件中写死。

**适用于**：
- Pass 内部算法的固定系数
- 不希望暴露给用户的内部参数
- 每个 pass 独立的配置值

**示例：**
- 饱和度增强系数（固定 1.5）
- 色调调整（固定暖色）
- 暗角强度（固定 0.3）
- 算法参数（如高斯模糊的 sigma）

## 实现原理

### 1. 统一加载

```cpp
// RenderPass::loadInternalUniforms
bool RenderPass::loadInternalUniforms(const nlohmann::json &config) {
    if (!config.contains("uniform")) return true;
    
    // 复用 UniformParam 类加载
    for (const auto &uniform_config : config["uniform"]) {
        auto uniform = std::make_unique<UniformParam>();
        if (uniform->load(uniform_config)) {
            internal_uniforms_.push_back(std::move(uniform));
        }
    }
    return true;
}
```

### 2. 统一提取

```cpp
// RenderPass::getInternalUniforms
void RenderPass::getInternalUniforms(
    std::map<std::string, float> &float_uniforms,
    std::map<std::string, std::vector<float>> &vec_uniforms,
    std::map<std::string, bool> &bool_uniforms) const {
    
    // 复用 UniformParam 的逻辑提取值
    for (const auto &uniform : internal_uniforms_) {
        std::string target = uniform->getUniformTarget();
        auto type = uniform->getType();
        
        if (type == UniformType::Float) {
            float_uniforms[target] = uniform->getFloatValue();
        } else if (type == UniformType::Vec2 || ...) {
            vec_uniforms[target] = uniform->getVecValue();
        }
    }
}
```

### 3. 统一传入 Shader

```cpp
// RenderPass::execute
// 先设置外部参数
for (const auto &[name, value] : float_uniforms) {
    shader_->setFloat(name, value);
}

// 再设置内部参数（复用同样的逻辑）
std::map<std::string, float> internal_floats;
std::map<std::string, std::vector<float>> internal_vecs;
std::map<std::string, bool> internal_bools;
getInternalUniforms(internal_floats, internal_vecs, internal_bools);

for (const auto &[name, value] : internal_floats) {
    shader_->setFloat(name, value);
}
```

## 代码复用对比

### 修改前（重复代码）

```cpp
// 外部 uniform - 使用 UniformParam
std::vector<std::unique_ptr<UniformParam>> uniforms_;

// 内部 uniform - 手动解析 JSON
if (type_str == "float") {
    internal_float_uniforms_[target] = config["value"].get<float>();
} else if (type_str == "vec2") {
    for (auto &v : config["value"]) {
        internal_vec_uniforms_[target].push_back(v.get<float>());
    }
}
// ... 大量重复代码
```

### 修改后（完全复用）

```cpp
// 外部 uniform
std::vector<std::unique_ptr<UniformParam>> uniforms_;

// 内部 uniform（复用同一个类）
std::vector<std::unique_ptr<UniformParam>> internal_uniforms_;

// 统一加载
auto uniform = std::make_unique<UniformParam>();
uniform->load(config);
```

## 优势

### 1. 零重复代码
- 加载逻辑复用
- 提取逻辑复用
- 类型处理复用

### 2. 统一维护
- 新增类型（如 mat3/mat4）只需改 `UniformParam`
- 所有地方自动支持

### 3. 一致性保证
- 配置格式完全一致
- 行为完全一致

### 4. 灵活性
- 外部 uniform：提供修改接口
- 内部 uniform：不提供修改接口
- 同一套基础设施，不同的暴露策略

## 测试用例

本示例包含：

**Pass 0：**
- 外部参数：`brightness`（用户可调）
- 内部参数：`saturation`（固定 1.5）、`tint`（固定暖色）

**Pass 1：**
- 外部参数：`brightness`（用户可调）
- 内部参数：`vignetteStrength`（固定 0.3）、`vignetteColor`（固定黑色）

## 使用

```cpp
auto resource = std::make_unique<RenderResource>(root);
resource->loadFromFolder("./test/resources/internal_uniform_test");

// 只能调整外部参数
resource->setFloatParam("亮度", 1.5f);

// 内部参数（saturation, tint, vignetteStrength, vignetteColor）
// 在配置文件中固定，不对外暴露

gl::FBO output = resource->render(input_fbo, time_ms);
```

## 架构图

```
RenderResource
  ├── uniform[]                      (外部 uniform)
  │   └── UniformParam               (用户可调)
  │
  └── renderPass[]
      ├── Pass 0
      │   ├── shader
      │   └── uniform[]              (内部 uniform)
      │       └── UniformParam       (固定值，复用同一个类)
      │
      └── Pass 1
          ├── shader
          └── uniform[]              (内部 uniform)
              └── UniformParam       (固定值，复用同一个类)
```

## 配置字段对比

| 字段 | 外部 Uniform | 内部 Uniform | 说明 |
|------|-------------|-------------|------|
| name | ✅ 必需 | ❌ 无 | 外部显示名称 |
| description | ✅ 可选 | ❌ 无 | 外部说明 |
| uniformTarget | ✅ 必需 | ✅ 必需 | Shader 变量名 |
| type | ✅ 必需 | ✅ 必需 | 类型 |
| defaultValue | ✅ 必需 | ❌ 无 | 外部默认值 |
| value | ✅ 可选 | ✅ 必需 | 内部固定值 |
| range | ✅ 可选 | ❌ 无 | 外部取值范围 |
| renderPassIndex | ✅ 必需 | ❌ 无 | 外部影响的 pass |
| comment | ❌ 无 | ✅ 可选 | 内部注释 |

## 总结

通过复用 `UniformParam` 类，实现了：
- ✅ 零重复代码
- ✅ 统一维护
- ✅ 灵活暴露策略
- ✅ 完全一致的配置格式

**内部 uniform 和外部 uniform 在底层完全共享同一套加载、管理、传递逻辑。**
