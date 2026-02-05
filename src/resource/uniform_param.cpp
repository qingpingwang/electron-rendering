#include "uniform_param.h"
#include "../gl/shader.h"
#include <algorithm>

namespace vp {

UniformParam::UniformParam() :
    type_(UniformType::Float), bool_value_(false) {
    vec_value_.push_back(0.0f); // Float 默认值（vec1）
}

UniformParam::UniformParam(std::string name, UniformType type, std::vector<float> vec_value,
                           std::string uniform_target,
                           bool apply_to_all_passes, std::vector<int> render_pass_indices) :
    type_(type),
    name_(std::move(name)),
    uniform_target_(uniform_target.empty() ? name_ : std::move(uniform_target)),
    render_pass_index_list_(std::move(render_pass_indices)),
    apply_to_all_passes_(apply_to_all_passes),
    vec_value_(std::move(vec_value)),
    bool_value_(false) {
}

UniformParam::~UniformParam() {
}

bool UniformParam::load(const nlohmann::json &config) {
    // 加载基本信息（使用 value 提供默认值）
    name_ = config.value("name", "");
    uniform_target_ = config.value("uniformTarget", "");

    // 加载影响的 pass 列表
    if (config.contains("renderPassIndex")) {
        const auto &idx_array = config["renderPassIndex"];
        render_pass_index_list_.reserve(idx_array.size());
        for (const auto &idx : idx_array) {
            render_pass_index_list_.emplace_back(idx.get<int>());
        }
    }

    // 识别类型（默认 float）
    std::string type_str = config.value("type", "float");

    if (type_str == "float" || type_str == "vec2" || type_str == "vec3" || type_str == "vec4") {
        // 确定类型
        if (type_str == "float")
            type_ = UniformType::Float;
        else if (type_str == "vec2")
            type_ = UniformType::Vec2;
        else if (type_str == "vec3")
            type_ = UniformType::Vec3;
        else if (type_str == "vec4")
            type_ = UniformType::Vec4;

        // 根据类型提前 resize（避免动态扩容）
        int size = (type_ == UniformType::Float) ? 1 : (type_ == UniformType::Vec2) ? 2 :
                                                   (type_ == UniformType::Vec3)     ? 3 :
                                                                                      4;
        vec_value_.resize(size, 0.0f);

        // 加载值：优先 value（内部 uniform），其次 defaultValue（外部 uniform）
        std::string value_key = config.contains("value") ? "value" : "defaultValue";
        if (config.contains(value_key)) {
            if (config[value_key].is_number()) {
                vec_value_[0] = config[value_key].get<float>();
            } else if (config[value_key].is_array()) {
                for (size_t i = 0; i < config[value_key].size() && i < vec_value_.size(); i++) {
                    vec_value_[i] = config[value_key][i].get<float>();
                }
            }
        }
    } else if (type_str == "boolean") {
        type_ = UniformType::Boolean;
        // 优先 value，其次 defaultValue
        bool_value_ = config.contains("value") ? config.value("value", false) : config.value("defaultValue", false);
    } else if (type_str == "animation") {
        type_ = UniformType::Animation;
        animation_name_ = config.value("animationName", "");
        if (uniform_target_.empty()) {
            uniform_target_ = config.value("uniformTarget", "");
        }
    } else if (type_str == "texture") {
        type_ = UniformType::Texture;
    }

    return true;
}

UniformType UniformParam::getType() const {
    return type_;
}

const std::string &UniformParam::getName() const {
    return name_;
}

const std::string &UniformParam::getUniformTarget() const {
    return uniform_target_;
}

bool UniformParam::affectsPass(int pass_index) const {
    if (apply_to_all_passes_) {
        return true;
    }
    return std::find(render_pass_index_list_.begin(), render_pass_index_list_.end(),
                     pass_index)
           != render_pass_index_list_.end();
}

float UniformParam::getFloatValue() const {
    // Float 是 vec1 的特化，返回第一个元素
    return vec_value_.empty() ? 0.0f : vec_value_[0];
}

const std::vector<float> &UniformParam::getVecValue() const {
    return vec_value_;
}

int UniformParam::getVecSize() const {
    return static_cast<int>(vec_value_.size());
}

bool UniformParam::getBoolValue() const {
    return bool_value_;
}

std::string UniformParam::getAnimationName() const {
    return animation_name_;
}

void UniformParam::setValue(float v) {
    // Float 是 vec1
    vec_value_.clear();
    vec_value_.push_back(v);
}

void UniformParam::setValue(const std::vector<float> &v) {
    vec_value_ = v;
}

void UniformParam::setValue(bool v) {
    bool_value_ = v;
}

void UniformParam::applyToShader(gl::Shader *shader) const {
    if (!shader) return;

    shader->use();

    if (type_ == UniformType::Float) {
        shader->setFloat(uniform_target_, getFloatValue());
    } else if (type_ == UniformType::Vec2) {
        const auto &vec = vec_value_;
        shader->setVec2(uniform_target_, vec[0], vec[1]);
    } else if (type_ == UniformType::Vec3) {
        const auto &vec = vec_value_;
        shader->setVec3(uniform_target_, vec[0], vec[1], vec[2]);
    } else if (type_ == UniformType::Vec4) {
        const auto &vec = vec_value_;
        shader->setVec4(uniform_target_, vec[0], vec[1], vec[2], vec[3]);
    } else if (type_ == UniformType::Boolean) {
        shader->setInt(uniform_target_, bool_value_ ? 1 : 0);
    }

    shader->unuse();
}

} // namespace vp
