#pragma once

#include "../gl/shader.h"
#include "../gl/types.h"
#include "uniform_param.h"
#include <nlohmann/json.hpp>
#include <memory>
#include <string>
#include <vector>

namespace vp {

// 前向声明
class RootNode;

// 纹理输入
struct TextureInput {
    std::string name;
    int pipe;
    GLuint texture_id;
};

// 单个渲染通道
class RenderPass {
public:
    RenderPass(RootNode *root, int pass_index);
    ~RenderPass();

    // 从 JSON 加载配置
    bool load(const nlohmann::json &config, const std::string &base_path);

    // 执行渲染
    // inputs: 输入 FBO 数组（支持多输入，用于转场效果）
    // textures: 所有纹理绑定（包括 pass 间依赖纹理和外部纹理）
    // uniforms: uniform 参数
    gl::FBO execute(const std::vector<gl::FBO> &inputs,
                    const std::vector<std::unique_ptr<UniformParam>> &uniforms,
                    const std::vector<TextureInput> &tex_inputs);

    // 获取 shader（用于外部直接更新 uniform）
    gl::Shader *getShader() const;

    // asInputTexIndex 定义（放在这里，因为后面的函数需要用到）
    struct InputTexDef {
        std::string name;      // uniform 变量名
        int pipe;              // 纹理单元
        int render_pass_index; // 来自哪个 pass（当前 pass 的输出将作为该索引 pass 的输入）
    };

    // 获取作为指定 pass 输入的纹理定义（找不到返回 nullptr）
    const InputTexDef *getAsInputTexDefFor(int pass_index) const;

    float getFBOSizeRatio() const;

    // 获取 asInputTexIndex 列表（用于 RenderResource 构建依赖关系）
    const std::vector<InputTexDef> &getAsInputTexList() const;

private:
    RootNode *root_;
    std::unique_ptr<gl::Shader> shader_;
    float fbo_size_ratio_;                       // FBO 大小比例（相对于原始输入）
    int pass_index_;                             // 当前 pass 索引
    std::vector<InputTexDef> as_input_tex_list_; // 当前 pass 的输出作为其他 pass 输入的定义

    // 加载 shader（同时处理内部 uniform）
    bool loadShader(const nlohmann::json &config, const std::string &base_path);
};

} // namespace vp
