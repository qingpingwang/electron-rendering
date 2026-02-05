#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace vp {

// 前向声明
namespace gl {
class Shader;
}

enum class UniformType {
    Float,
    Vec2,
    Vec3,
    Vec4,
    Boolean,
    Animation,
    Texture
};

// Uniform 参数
class UniformParam {
public:
    UniformParam();

    // 直接构造（用于程序化创建，避免 JSON 序列化/反序列化开销）
    // 用于 Float/Vec2/Vec3/Vec4 类型
    // uniform_target 为空时使用 name
    UniformParam(std::string name, UniformType type, std::vector<float> vec_value,
                 std::string uniform_target = "",
                 bool apply_to_all_passes = false,
                 std::vector<int> render_pass_indices = {});

    ~UniformParam();

    bool load(const nlohmann::json &config);

    UniformType getType() const;
    const std::string &getName() const;          // 外部名称
    const std::string &getUniformTarget() const; // shader 变量名

    // 是否影响指定 pass
    bool affectsPass(int pass_index) const;

    // 获取受影响的 pass 索引列表
    const std::vector<int> &getRenderPassIndices() const;

    // 类型相关的值
    float getFloatValue() const;                   // Float (vec1 特化)
    const std::vector<float> &getVecValue() const; // Float/Vec2/Vec3/Vec4 统一接口
    int getVecSize() const;                        // 获取向量维度
    bool getBoolValue() const;
    std::string getAnimationName() const; // 关联的动画名

    void setValue(float v);                     // Float (vec1)
    void setValue(const std::vector<float> &v); // Vec2/Vec3/Vec4
    void setValue(bool v);

    // 将当前 uniform 值应用到 shader（内部处理 use/unuse）
    void applyToShader(class gl::Shader *shader) const;

private:
    UniformType type_;
    std::string name_;
    std::string uniform_target_;
    std::vector<int> render_pass_index_list_;

    bool apply_to_all_passes_ = false;

    // 值存储（统一用 vector，Float 是 vec1）
    std::vector<float> vec_value_; // Float(vec1)/Vec2/Vec3/Vec4
    bool bool_value_;
    std::string animation_name_; // type == Animation
};

} // namespace vp
