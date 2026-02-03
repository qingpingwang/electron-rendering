#include "gl_types.h"

namespace vp {
namespace gl {

bool Texture::isValid() const {
    return id != 0;
}

bool FBO::isValid() const {
    return fbo != 0;
}

bool QuadMesh::isValid() const {
    return vao != 0;
}

}
} // namespace vp::gl
