#include "render_pass.h"
#include "../engine/root_node.h"
#include "../gl/functions.h"
#include <algorithm>
#include <fstream>
#include <sstream>

namespace vp {

RenderPass::RenderPass(RootNode *root, int pass_index) :
    root_(root), fbo_size_ratio_(1.0f), pass_index_(pass_index) {
}

RenderPass::~RenderPass() {
}

bool RenderPass::load(const nlohmann::json &config, const std::string &base_path) {
    // 1. 加载 FBO 大小比例（默认 1.0）
    fbo_size_ratio_ = config.value("fboSize", 1.0f);

    // 2. 加载 shader（必须，同时处理内部 uniform）
    if (!config.contains("shader")) {
        return false;
    }
    if (!loadShader(config, base_path)) {
        return false;
    }

    // 3. 加载 asInputTexIndex（作为其他 pass 输入的定义）
    if (config.contains("asInputTexIndex")) {
        const auto &input_array = config["asInputTexIndex"];
        as_input_tex_list_.reserve(input_array.size());

        for (const auto &input_def : input_array) {
            InputTexDef def;
            def.name = input_def.value("name", "");
            def.pipe = input_def.value("pipe", 0);
            def.render_pass_index = input_def.value("renderPassIndex", 0);
            as_input_tex_list_.emplace_back(std::move(def));
        }
    }

    return true;
}

bool RenderPass::loadShader(const nlohmann::json &config, const std::string &base_path) {
    if (!config.contains("shader")) {
        return false;
    }

    const auto &shader_config = config["shader"];
    if (!shader_config.contains("vert") || !shader_config.contains("frag")) {
        return false;
    }

    std::string vert_path = base_path + "/" + shader_config["vert"].get<std::string>();
    std::string frag_path = base_path + "/" + shader_config["frag"].get<std::string>();

    // 读取 shader 文件
    auto read_file = [](const std::string &path) -> std::string {
        std::ifstream file(path);
        if (!file.is_open())
            return "";
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    };

    std::string vert_source = read_file(vert_path);
    std::string frag_source = read_file(frag_path);

    if (vert_source.empty() || frag_source.empty()) {
        return false;
    }

    // 创建 shader
    shader_ = std::make_unique<gl::Shader>(vert_source.c_str(), frag_source.c_str());

    // 检查编译错误
    if (!shader_->getError().empty()) {
        // 有错误但不打印，外部可通过 getError() 获取
        return false;
    }

    if (!shader_->isValid()) {
        return false;
    }

    // 设置内部 uniform（固定值，设置一次即可）
    if (config.contains("uniform")) {
        for (const auto &uniform_config : config["uniform"]) {
            UniformParam uniform;
            if (uniform.load(uniform_config)) {
                uniform.applyToShader(shader_.get());
            }
        }
    }

    return true;
}

gl::FBO RenderPass::execute(const std::vector<gl::FBO> &inputs,
                            const std::vector<std::unique_ptr<UniformParam>> &uniforms,
                            const std::vector<TextureInput> &tex_inputs) {
    if (!root_ || !shader_ || inputs.empty())
        return gl::FBO{};

    // 计算 FBO 大小（基于第一个输入）
    int fbo_width = static_cast<int>(inputs[0].width * fbo_size_ratio_);
    int fbo_height = static_cast<int>(inputs[0].height * fbo_size_ratio_);

    gl::FBO output_fbo = root_->getFBOPool()->acquire(fbo_width, fbo_height);
    if (!output_fbo.isValid())
        return gl::FBO{};

    // 设置uniform变量
    shader_->use();
    shader_->setVec2("resolution", static_cast<float>(fbo_width), static_cast<float>(fbo_height));
    shader_->setFloat("fboRatio", fbo_size_ratio_);
    for (const auto &uniform : uniforms) {
        if (uniform->affectsPass(pass_index_)) {
            uniform->applyToShader(shader_.get());
        }
    }

    shader_->use();
    // 绑定所有输入纹理（inputTexture0, inputTexture1, ...）
    for (size_t i = 0; i < inputs.size(); ++i) {
        const auto &input_fbo = inputs[i];
        if (!input_fbo.isValid()) {
            continue;
        }

        gl::bindTexture(gl::Texture{input_fbo.texture, input_fbo.width, input_fbo.height}, i);
        // 绑定到 inputTextureN
        std::string uniform_name = "inputTexture" + std::to_string(i);
        shader_->setInt(uniform_name.c_str(), i);
    }

    // 其它纹理输入
    for (const auto &tex_input : tex_inputs) {
        gl::bindTexture(gl::Texture{tex_input.texture_id, 0, 0}, tex_input.pipe);
        shader_->setInt(tex_input.name.c_str(), tex_input.pipe);
    }

    // 绑定输出 FBO
    gl::bindFBO(output_fbo);
    gl::cleanColor(0, 0, 0, 0);
    // 绘制全屏 quad
    gl::drawQuad(*root_->getQuad());
    // 解绑
    shader_->unuse();
    gl::unbindFBO();
    return output_fbo;
}

gl::Shader *RenderPass::getShader() const {
    return shader_.get();
}

float RenderPass::getFBOSizeRatio() const {
    return fbo_size_ratio_;
}

const RenderPass::InputTexDef *RenderPass::getAsInputTexDefFor(int pass_index) const {
    auto it = std::find_if(as_input_tex_list_.begin(), as_input_tex_list_.end(),
                           [pass_index](const InputTexDef &def) {
                               return def.render_pass_index == pass_index;
                           });
    return it != as_input_tex_list_.end() ? &(*it) : nullptr;
}

const std::vector<RenderPass::InputTexDef> &RenderPass::getAsInputTexList() const {
    return as_input_tex_list_;
}

} // namespace vp
