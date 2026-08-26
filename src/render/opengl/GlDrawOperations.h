#pragma once

#include "GlChunkResource.h"
#include <dzc/EngineTypes.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace dzc::opengl {

enum class GlDrawBufferTarget : std::uint8_t {
  UniformBuffer,
  ShaderStorageBuffer
};
enum class GlDrawBufferUsage : std::uint8_t { DynamicDraw };

class IGlDrawOperations : public IGlChunkUploadOperations {
public:
  ~IGlDrawOperations() override = default;
  virtual bool bindDrawBuffer(GlDrawBufferTarget target,
                              std::uint32_t bufferId) const noexcept = 0;
  virtual bool uploadDrawBuffer(GlDrawBufferTarget target,
                                std::uint32_t bufferId, const void *data,
                                std::size_t size,
                                GlDrawBufferUsage usage) const noexcept = 0;
  virtual bool bindDrawBufferBase(GlDrawBufferTarget target,
                                  std::uint32_t binding,
                                  std::uint32_t bufferId) const noexcept = 0;
  virtual bool useProgram(std::uint32_t programId) const noexcept = 0;
  virtual bool clearColor(const dzc::ColorRgba &color) const noexcept = 0;
  virtual bool drawPoints(std::uint32_t pointCount) const noexcept = 0;
};

std::shared_ptr<const IGlDrawOperations> makeDefaultGlDrawOperations();

} // namespace dzc::opengl
