#include "GlDrawOperations.h"

#include <glad/glad.h>

#include <limits>
#include <string>

namespace dzc::opengl {
namespace {
GLenum target(GlDrawBufferTarget value) noexcept {
  return value == GlDrawBufferTarget::UniformBuffer ? GL_UNIFORM_BUFFER
                                                    : GL_SHADER_STORAGE_BUFFER;
}
class GladDrawOperations final : public IGlDrawOperations {
public:
  bool createBuffer(std::uint32_t &id) const noexcept override {
    if (!glGenBuffers)
      return false;
    GLuint value = 0;
    glGenBuffers(1, &value);
    id = static_cast<std::uint32_t>(value);
    return id != 0;
  }
  bool deleteBuffer(std::uint32_t id) const noexcept override {
    if (!id)
      return true;
    if (!glDeleteBuffers)
      return false;
    const GLuint value = static_cast<GLuint>(id);
    glDeleteBuffers(1, &value);
    return true;
  }
  bool labelBuffer(std::uint32_t id,
                   std::string_view label) const noexcept override {
    if (!id || !glObjectLabel ||
        label.size() >
            static_cast<std::size_t>(std::numeric_limits<GLsizei>::max()))
      return false;
    glObjectLabel(GL_BUFFER, static_cast<GLuint>(id),
                  static_cast<GLsizei>(label.size()), label.data());
    return true;
  }
  bool createVertexArray(std::uint32_t &id) const noexcept override {
    if (!glGenVertexArrays)
      return false;
    GLuint value = 0;
    glGenVertexArrays(1, &value);
    id = static_cast<std::uint32_t>(value);
    return id != 0;
  }
  bool deleteVertexArray(std::uint32_t id) const noexcept override {
    if (!id)
      return true;
    if (!glDeleteVertexArrays)
      return false;
    const GLuint value = static_cast<GLuint>(id);
    glDeleteVertexArrays(1, &value);
    return true;
  }
  bool labelVertexArray(std::uint32_t id,
                        std::string_view label) const noexcept override {
    if (!id || !glObjectLabel ||
        label.size() >
            static_cast<std::size_t>(std::numeric_limits<GLsizei>::max()))
      return false;
    glObjectLabel(GL_VERTEX_ARRAY, static_cast<GLuint>(id),
                  static_cast<GLsizei>(label.size()), label.data());
    return true;
  }
  bool bindVertexArray(std::uint32_t id) const noexcept override {
    if (!glBindVertexArray)
      return false;
    glBindVertexArray(static_cast<GLuint>(id));
    return true;
  }
  bool bindArrayBuffer(std::uint32_t id) const noexcept override {
    if (!glBindBuffer)
      return false;
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(id));
    return true;
  }
  bool uploadArrayBuffer(std::uint32_t id, const std::vector<std::byte> &bytes,
                         GlChunkBufferUsage usage) const noexcept override {
    if (!glBindBuffer || !glBufferData ||
        bytes.size() >
            static_cast<std::size_t>(std::numeric_limits<GLsizeiptr>::max()))
      return false;
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(id));
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(bytes.size()),
                 bytes.data(),
                 usage == GlChunkBufferUsage::StaticDraw ? GL_STATIC_DRAW
                                                         : GL_DYNAMIC_DRAW);
    return true;
  }
  bool configureVertexAttribute(std::uint32_t index,
                                GlChunkAttributeFormat format,
                                std::uint32_t components, bool normalized,
                                std::uint32_t stride) const noexcept override {
    if (!glVertexAttribPointer || components == 0 || components > 4)
      return false;
    GLenum type = GL_FLOAT;
    if (format == GlChunkAttributeFormat::UInt8)
      type = GL_UNSIGNED_BYTE;
    else if (format == GlChunkAttributeFormat::UInt16)
      type = GL_UNSIGNED_SHORT;
    glVertexAttribPointer(
        static_cast<GLuint>(index), static_cast<GLint>(components), type,
        normalized ? GL_TRUE : GL_FALSE, static_cast<GLsizei>(stride), nullptr);
    return true;
  }
  bool enableVertexAttribute(std::uint32_t index) const noexcept override {
    if (!glEnableVertexAttribArray)
      return false;
    glEnableVertexAttribArray(static_cast<GLuint>(index));
    return true;
  }
  bool unbindArrayBuffer() const noexcept override {
    return bindArrayBuffer(0);
  }
  bool unbindVertexArray() const noexcept override {
    return bindVertexArray(0);
  }
  bool bindDrawBuffer(GlDrawBufferTarget value,
                      std::uint32_t id) const noexcept override {
    if (!glBindBuffer)
      return false;
    glBindBuffer(target(value), static_cast<GLuint>(id));
    return true;
  }
  bool uploadDrawBuffer(GlDrawBufferTarget value, std::uint32_t id,
                        const void *data, std::size_t size,
                        GlDrawBufferUsage) const noexcept override {
    if (!glBindBuffer || !glBufferData ||
        size > static_cast<std::size_t>(std::numeric_limits<GLsizeiptr>::max()))
      return false;
    glBindBuffer(target(value), static_cast<GLuint>(id));
    glBufferData(target(value), static_cast<GLsizeiptr>(size), data,
                 GL_DYNAMIC_DRAW);
    return true;
  }
  bool bindDrawBufferBase(GlDrawBufferTarget value, std::uint32_t binding,
                          std::uint32_t id) const noexcept override {
    if (!glBindBufferBase)
      return false;
    glBindBufferBase(target(value), static_cast<GLuint>(binding),
                     static_cast<GLuint>(id));
    return true;
  }
  bool useProgram(std::uint32_t id) const noexcept override {
    if (!glUseProgram || !id)
      return false;
    glUseProgram(static_cast<GLuint>(id));
    return true;
  }
  bool setProgramUniformUInt(std::uint32_t programId, std::string_view name,
                             std::uint32_t value) const noexcept override {
    if (!programId || !glGetUniformLocation || !glUniform1ui ||
        name.size() > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max()))
      return false;
    const std::string uniformName(name);
    const GLint location = glGetUniformLocation(static_cast<GLuint>(programId),
                                                uniformName.c_str());
    if (location < 0)
      return false;
    glUniform1ui(location, static_cast<GLuint>(value));
    return true;
  }
  bool setViewport(std::uint32_t x, std::uint32_t y, std::uint32_t width,
                   std::uint32_t height) const noexcept override {
    if (!glViewport ||
        x > static_cast<std::uint32_t>(std::numeric_limits<GLint>::max()) ||
        y > static_cast<std::uint32_t>(std::numeric_limits<GLint>::max()) ||
        width > static_cast<std::uint32_t>(std::numeric_limits<GLsizei>::max()) ||
        height > static_cast<std::uint32_t>(std::numeric_limits<GLsizei>::max()))
      return false;
    glViewport(static_cast<GLint>(x), static_cast<GLint>(y),
               static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    return true;
  }
  bool clearColor(const dzc::ColorRgba &color) const noexcept override {
    if (!glClearColor || !glClear)
      return false;
    glClearColor(color.red, color.green, color.blue, color.alpha);
    glClear(GL_COLOR_BUFFER_BIT);
    return true;
  }
  bool drawPoints(std::uint32_t count) const noexcept override {
    if (!glDrawArrays ||
        count > static_cast<std::uint32_t>(std::numeric_limits<GLsizei>::max()))
      return false;
    glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(count));
    return true;
  }
};
} // namespace
std::shared_ptr<const IGlDrawOperations> makeDefaultGlDrawOperations() {
  static const auto operations = std::make_shared<const GladDrawOperations>();
  return operations;
}
} // namespace dzc::opengl
