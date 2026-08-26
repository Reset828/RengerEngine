#include "GlResource.h"

#include <glad/glad.h>

#include <limits>
#include <memory>

namespace dzc::opengl {
namespace {

class GladResourceOperations final : public IGlResourceOperations {
public:
    bool createBuffer(std::uint32_t& id) const noexcept override {
        if (glGenBuffers == nullptr) return false;
        GLuint generated = 0;
        glGenBuffers(1, &generated);
        id = static_cast<std::uint32_t>(generated);
        return id != 0;
    }
    bool deleteBuffer(std::uint32_t id) const noexcept override {
        if (id == 0) return true;
        if (glDeleteBuffers == nullptr) return false;
        const GLuint resource = static_cast<GLuint>(id);
        glDeleteBuffers(1, &resource);
        return true;
    }
    bool labelBuffer(std::uint32_t id, std::string_view label) const noexcept override {
        if (id == 0 || glObjectLabel == nullptr || label.size() > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max())) return false;
        glObjectLabel(GL_BUFFER, static_cast<GLuint>(id), static_cast<GLsizei>(label.size()), label.data());
        return true;
    }
    bool createVertexArray(std::uint32_t& id) const noexcept override {
        if (glGenVertexArrays == nullptr) return false;
        GLuint generated = 0;
        glGenVertexArrays(1, &generated);
        id = static_cast<std::uint32_t>(generated);
        return id != 0;
    }
    bool deleteVertexArray(std::uint32_t id) const noexcept override {
        if (id == 0) return true;
        if (glDeleteVertexArrays == nullptr) return false;
        const GLuint resource = static_cast<GLuint>(id);
        glDeleteVertexArrays(1, &resource);
        return true;
    }
    bool labelVertexArray(std::uint32_t id, std::string_view label) const noexcept override {
        if (id == 0 || glObjectLabel == nullptr || label.size() > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max())) return false;
        glObjectLabel(GL_VERTEX_ARRAY, static_cast<GLuint>(id), static_cast<GLsizei>(label.size()), label.data());
        return true;
    }
};

} // namespace

GlContextThreadToken GlContextThreadToken::current() noexcept {
    return GlContextThreadToken(std::this_thread::get_id());
}

bool GlContextThreadToken::isCurrentThread() const noexcept {
    return mThreadId == std::this_thread::get_id();
}

std::shared_ptr<const IGlResourceOperations> makeDefaultGlResourceOperations() {
    static const std::shared_ptr<const IGlResourceOperations> operations = std::make_shared<const GladResourceOperations>();
    return operations;
}

} // namespace dzc::opengl