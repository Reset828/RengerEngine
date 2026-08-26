#include "GlChunkResource.h"

#include <glad/glad.h>

#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <string>
#include <utility>

namespace dzc::opengl {
namespace {

Result<void> failure(
    GlChunkErrorCode code,
    std::string userMessage,
    std::string diagnosticMessage,
    std::string context) {
    return Result<void>::failure(Error{
        ErrorDomain::OpenGL,
        static_cast<std::uint32_t>(code),
        std::move(userMessage),
        std::move(diagnosticMessage),
        std::move(context)});
}

bool tokenCanAccess(const GlContextThreadToken& token,
                    const std::thread::id& ownerThread) noexcept {
    return token.isCurrentThread() && ownerThread == std::this_thread::get_id();
}

bool checkedBytes(std::uint64_t count, std::uint64_t bytesPerItem, std::size_t& result) noexcept {
    const auto maxSize = std::numeric_limits<std::size_t>::max();
    if (count > static_cast<std::uint64_t>(maxSize)
        || bytesPerItem != 0U && count > static_cast<std::uint64_t>(maxSize) / bytesPerItem) {
        return false;
    }
    result = static_cast<std::size_t>(count * bytesPerItem);
    return true;
}

bool checkedAdd(std::size_t left, std::size_t right, std::size_t& result) noexcept {
    if (right > std::numeric_limits<std::size_t>::max() - left) return false;
    result = left + right;
    return true;
}

Result<void> invalidSize(const char* stage) {
    return failure(
        GlChunkErrorCode::SizeOverflow,
        "The chunk GPU upload size is too large",
        "A point-count to byte-count conversion overflowed std::size_t",
        std::string("Chunk upload: ") + stage);
}

class GladChunkUploadOperations final : public IGlChunkUploadOperations {
public:
    bool createBuffer(std::uint32_t& id) const noexcept override {
        if (glGenBuffers == nullptr) return false;
        GLuint value = 0;
        glGenBuffers(1, &value);
        id = static_cast<std::uint32_t>(value);
        return id != 0U;
    }

    bool deleteBuffer(std::uint32_t id) const noexcept override {
        if (id == 0U) return true;
        if (glDeleteBuffers == nullptr) return false;
        const GLuint value = static_cast<GLuint>(id);
        glDeleteBuffers(1, &value);
        return true;
    }

    bool labelBuffer(std::uint32_t id, std::string_view label) const noexcept override {
        if (id == 0U || glObjectLabel == nullptr
            || label.size() > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max())) return false;
        glObjectLabel(GL_BUFFER, static_cast<GLuint>(id), static_cast<GLsizei>(label.size()), label.data());
        return true;
    }

    bool createVertexArray(std::uint32_t& id) const noexcept override {
        if (glGenVertexArrays == nullptr) return false;
        GLuint value = 0;
        glGenVertexArrays(1, &value);
        id = static_cast<std::uint32_t>(value);
        return id != 0U;
    }

    bool deleteVertexArray(std::uint32_t id) const noexcept override {
        if (id == 0U) return true;
        if (glDeleteVertexArrays == nullptr) return false;
        const GLuint value = static_cast<GLuint>(id);
        glDeleteVertexArrays(1, &value);
        return true;
    }

    bool labelVertexArray(std::uint32_t id, std::string_view label) const noexcept override {
        if (id == 0U || glObjectLabel == nullptr
            || label.size() > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max())) return false;
        glObjectLabel(GL_VERTEX_ARRAY, static_cast<GLuint>(id), static_cast<GLsizei>(label.size()), label.data());
        return true;
    }

    bool bindVertexArray(std::uint32_t id) const noexcept override {
        if (glBindVertexArray == nullptr) return false;
        glBindVertexArray(static_cast<GLuint>(id));
        return true;
    }

    bool bindArrayBuffer(std::uint32_t id) const noexcept override {
        if (glBindBuffer == nullptr) return false;
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(id));
        return true;
    }

    bool uploadArrayBuffer(std::uint32_t id, const std::vector<std::byte>& bytes,
                           GlChunkBufferUsage usage) const noexcept override {
        if (id == 0U || glBindBuffer == nullptr || glBufferData == nullptr
            || bytes.size() > static_cast<std::size_t>(std::numeric_limits<GLsizeiptr>::max())) return false;
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(id));
        const GLenum glUsage = usage == GlChunkBufferUsage::StaticDraw ? GL_STATIC_DRAW : GL_STATIC_DRAW;
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(bytes.size()), bytes.data(), glUsage);
        return true;
    }

    bool configureVertexAttribute(std::uint32_t index, GlChunkAttributeFormat format,
                                  std::uint32_t componentCount, bool normalized,
                                  std::uint32_t stride) const noexcept override {
        if (glVertexAttribPointer == nullptr
            || componentCount == 0U || componentCount > static_cast<std::uint32_t>(std::numeric_limits<GLint>::max())
            || stride > static_cast<std::uint32_t>(std::numeric_limits<GLsizei>::max())) return false;
        GLenum type = GL_FLOAT;
        if (format == GlChunkAttributeFormat::UInt8) type = GL_UNSIGNED_BYTE;
        if (format == GlChunkAttributeFormat::UInt16) type = GL_UNSIGNED_SHORT;
        glVertexAttribPointer(static_cast<GLuint>(index), static_cast<GLint>(componentCount), type,
                              normalized ? GL_TRUE : GL_FALSE, static_cast<GLsizei>(stride), nullptr);
        return true;
    }

    bool enableVertexAttribute(std::uint32_t index) const noexcept override {
        if (glEnableVertexAttribArray == nullptr) return false;
        glEnableVertexAttribArray(static_cast<GLuint>(index));
        return true;
    }

    bool unbindArrayBuffer() const noexcept override {
        if (glBindBuffer == nullptr) return false;
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        return true;
    }

    bool unbindVertexArray() const noexcept override {
        if (glBindVertexArray == nullptr) return false;
        glBindVertexArray(0);
        return true;
    }
};

void appendCleanupFailure(std::string& diagnostic, const char* resource) {
    if (!diagnostic.empty()) diagnostic += "; ";
    diagnostic += "temporary resource cleanup failed: ";
    diagnostic += resource;
}

} // namespace

std::shared_ptr<const IGlChunkUploadOperations> makeDefaultGlChunkUploadOperations() {
    static const std::shared_ptr<const IGlChunkUploadOperations> operations =
        std::make_shared<const GladChunkUploadOperations>();
    return operations;
}

GlChunkResource::~GlChunkResource() noexcept {
    if (isValid() && (mOwnerThread != std::this_thread::get_id() || !mOperations)) {
        mReleasePending = true;
    }
}

GlChunkResource::GlChunkResource(GlChunkResource&& other) noexcept
    : mVertexArray(std::move(other.mVertexArray)),
      mPositionBuffer(std::move(other.mPositionBuffer)),
      mColorBuffer(std::move(other.mColorBuffer)),
      mIntensityBuffer(std::move(other.mIntensityBuffer)),
      mOperations(std::move(other.mOperations)),
      mOwnerThread(other.mOwnerThread),
      mStats(other.mStats),
      mReleasePending(other.mReleasePending) {
    other.markMovedFrom();
}

GlChunkResource& GlChunkResource::operator=(GlChunkResource&& other) noexcept {
    if (this != &other) {
        GlChunkResource previous(std::move(*this));
        mVertexArray = std::move(other.mVertexArray);
        mPositionBuffer = std::move(other.mPositionBuffer);
        mColorBuffer = std::move(other.mColorBuffer);
        mIntensityBuffer = std::move(other.mIntensityBuffer);
        mOperations = std::move(other.mOperations);
        mOwnerThread = other.mOwnerThread;
        mStats = other.mStats;
        mReleasePending = other.mReleasePending;
        other.markMovedFrom();
    }
    return *this;
}

void GlChunkResource::markMovedFrom() noexcept {
    mOperations.reset();
    mOwnerThread = std::thread::id{};
    mStats = {};
    mReleasePending = false;
}

bool GlChunkResource::isValid() const noexcept {
    return mVertexArray.isValid() && mPositionBuffer.isValid()
        && mColorBuffer.isValid() && mIntensityBuffer.isValid() && mStats.valid;
}

bool GlChunkResource::releasePending() const noexcept {
    return mReleasePending || mVertexArray.releasePending() || mPositionBuffer.releasePending()
        || mColorBuffer.releasePending() || mIntensityBuffer.releasePending();
}

dzc::Result<void> GlChunkResource::upload(
    const GlContextThreadToken& token,
    const dzc::ChunkMetadata& metadata,
    const dzc::ChunkCpuData& cpuData,
    std::shared_ptr<const IGlChunkUploadOperations> operations) {
    if (!token.isCurrentThread()) {
        return failure(GlChunkErrorCode::InvalidThreadToken,
                       "The OpenGL upload token belongs to another thread",
                       "GlChunkResource::upload requires a token bound to the calling thread",
                       "Chunk upload: thread token");
    }
    if ((mVertexArray.isValid() || mPositionBuffer.isValid() || mColorBuffer.isValid() || mIntensityBuffer.isValid())
        && !tokenCanAccess(token, mOwnerThread)) {
        return failure(GlChunkErrorCode::InvalidThreadToken,
                       "The OpenGL upload token cannot replace this chunk resource",
                       "Replacing an existing chunk requires the thread that uploaded it",
                       "Chunk upload: existing resource thread token");
    }
    if (metadata.pointCount == 0U) {
        return failure(GlChunkErrorCode::InvalidInput, "The chunk contains no points",
                       "metadata.pointCount must be greater than zero", "Chunk upload: input validation");
    }
    if (!metadata.schema.hasPosition()) {
        return failure(GlChunkErrorCode::InvalidInput, "The chunk schema has no Position attribute",
                       "Position is required for the OpenGL point vertex stream", "Chunk upload: schema validation");
    }

    std::size_t pointCount = 0U;
    if (metadata.pointCount > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return invalidSize("point count");
    }
    pointCount = static_cast<std::size_t>(metadata.pointCount);

    std::size_t positionBytes = 0U, colorBytes = 0U, intensityBytes = 0U, totalBytes = 0U;
    if (!checkedBytes(metadata.pointCount, 3U * sizeof(float), positionBytes)
        || !checkedBytes(metadata.pointCount, 4U * sizeof(std::uint8_t), colorBytes)
        || !checkedBytes(metadata.pointCount, sizeof(std::uint16_t), intensityBytes)
        || !checkedAdd(positionBytes, colorBytes, totalBytes)
        || !checkedAdd(totalBytes, intensityBytes, totalBytes)) {
        return invalidSize("attribute streams");
    }

    if (cpuData.positions.size() != pointCount) {
        return failure(GlChunkErrorCode::InvalidInput, "Position data length does not match point count",
                       "ChunkCpuData.positions must contain exactly metadata.pointCount entries", "Chunk upload: Position validation");
    }
    if ((metadata.schema.hasColor() && cpuData.colorsRgba8.size() != pointCount)
        || (!metadata.schema.hasColor() && !cpuData.colorsRgba8.empty())) {
        return failure(GlChunkErrorCode::InvalidInput, "Color data does not match the chunk schema",
                       "Color data must be present exactly when the schema declares Color", "Chunk upload: Color validation");
    }
    if ((metadata.schema.hasIntensity() && cpuData.intensities.size() != pointCount)
        || (!metadata.schema.hasIntensity() && !cpuData.intensities.empty())) {
        return failure(GlChunkErrorCode::InvalidInput, "Intensity data does not match the chunk schema",
                       "Intensity data must be present exactly when the schema declares Intensity", "Chunk upload: Intensity validation");
    }
    if (!operations) operations = makeDefaultGlChunkUploadOperations();
    if (!operations) {
        return failure(GlChunkErrorCode::CreationFailed, "OpenGL chunk operations are unavailable",
                       "No chunk upload operation table was provided", "Chunk upload: operations");
    }

    try {
        std::vector<std::byte> positionData(positionBytes);
        std::vector<std::byte> colorData(colorBytes);
        std::vector<std::byte> intensityData(intensityBytes);
        for (std::size_t i = 0; i < pointCount; ++i) {
            const float values[3] = {cpuData.positions[i].x, cpuData.positions[i].y, cpuData.positions[i].z};
            std::memcpy(positionData.data() + i * 3U * sizeof(float), values, sizeof(values));
            const std::uint32_t color = metadata.schema.hasColor() ? cpuData.colorsRgba8[i] : 0xFFFFFFFFU;
            const std::uint8_t rgba[4] = {
                static_cast<std::uint8_t>(color >> 24U), static_cast<std::uint8_t>(color >> 16U),
                static_cast<std::uint8_t>(color >> 8U), static_cast<std::uint8_t>(color)};
            std::memcpy(colorData.data() + i * 4U, rgba, sizeof(rgba));
            const std::uint16_t intensity = metadata.schema.hasIntensity() ? cpuData.intensities[i] : 0xFFFFU;
            std::memcpy(intensityData.data() + i * sizeof(std::uint16_t), &intensity, sizeof(intensity));
        }

        GlVertexArray temporaryVao;
        GlBuffer temporaryPosition;
        GlBuffer temporaryColor;
        GlBuffer temporaryIntensity;
        auto cleanupTemporary = [&](std::string& diagnostic) {
            try { if (!temporaryVao.reset(token).hasValue()) appendCleanupFailure(diagnostic, "VAO"); } catch (...) { appendCleanupFailure(diagnostic, "VAO"); }
            try { if (!temporaryIntensity.reset(token).hasValue()) appendCleanupFailure(diagnostic, "Intensity VBO"); } catch (...) { appendCleanupFailure(diagnostic, "Intensity VBO"); }
            try { if (!temporaryColor.reset(token).hasValue()) appendCleanupFailure(diagnostic, "Color VBO"); } catch (...) { appendCleanupFailure(diagnostic, "Color VBO"); }
            try { if (!temporaryPosition.reset(token).hasValue()) appendCleanupFailure(diagnostic, "Position VBO"); } catch (...) { appendCleanupFailure(diagnostic, "Position VBO"); }
            try { if (!operations->unbindArrayBuffer()) appendCleanupFailure(diagnostic, "array-buffer unbind"); } catch (...) { appendCleanupFailure(diagnostic, "array-buffer unbind"); }
            try { if (!operations->unbindVertexArray()) appendCleanupFailure(diagnostic, "vertex-array unbind"); } catch (...) { appendCleanupFailure(diagnostic, "vertex-array unbind"); }
        };
        std::string creationDiagnostic;
        auto createTemporary = [&](auto& resource, const char* name) {
            if (resource.create(token, operations).hasValue()) return true;
            creationDiagnostic = std::string("The operation table could not create ") + name;
            cleanupTemporary(creationDiagnostic);
            return false;
        };
        if (!createTemporary(temporaryVao, "the temporary VAO")
            || !createTemporary(temporaryPosition, "the temporary Position VBO")
            || !createTemporary(temporaryColor, "the temporary Color VBO")
            || !createTemporary(temporaryIntensity, "the temporary Intensity VBO")) {
            return failure(GlChunkErrorCode::CreationFailed, "OpenGL chunk resource creation failed",
                           creationDiagnostic, "Chunk upload: resource creation");
        }
        auto uploadStream = [&](GlBuffer& buffer, const std::vector<std::byte>& bytes, const char* name) -> Result<void> {
            if (!operations->bindArrayBuffer(buffer.id()))
                return failure(GlChunkErrorCode::UploadFailed, "OpenGL buffer binding failed", "The operation table rejected binding of the " + std::string(name), "Chunk upload: bind " + std::string(name));
            if (!operations->uploadArrayBuffer(buffer.id(), bytes, GlChunkBufferUsage::StaticDraw))
                return failure(GlChunkErrorCode::UploadFailed, "OpenGL buffer upload failed", "The operation table rejected upload of the " + std::string(name), "Chunk upload: upload " + std::string(name));
            return Result<void>::success();
        };
        for (auto stream : {std::pair<GlBuffer*, const std::vector<std::byte>*>(&temporaryPosition, &positionData),
                            std::pair<GlBuffer*, const std::vector<std::byte>*>(&temporaryColor, &colorData),
                            std::pair<GlBuffer*, const std::vector<std::byte>*>(&temporaryIntensity, &intensityData)}) {
            const char* name = stream.first == &temporaryPosition ? "Position VBO" : stream.first == &temporaryColor ? "Color VBO" : "Intensity VBO";
            auto result = uploadStream(*stream.first, *stream.second, name);
            if (!result.hasValue()) { std::string diagnostic = result.error().diagnosticMessage; cleanupTemporary(diagnostic); return failure(GlChunkErrorCode::UploadFailed, result.error().userMessage, diagnostic, result.error().context); }
        }

        if (!operations->bindVertexArray(temporaryVao.id())) {
            std::string diagnostic = "The operation table rejected VAO binding"; cleanupTemporary(diagnostic);
            return failure(GlChunkErrorCode::UploadFailed, "OpenGL vertex-array binding failed", diagnostic, "Chunk upload: bind VAO");
        }
        struct Attribute { std::uint32_t index; GlChunkAttributeFormat format; std::uint32_t components; bool normalized; std::uint32_t stride; GlBuffer* buffer; } attributes[] = {
            {0U, GlChunkAttributeFormat::Float32, 3U, false, static_cast<std::uint32_t>(3U * sizeof(float)), &temporaryPosition},
            {1U, GlChunkAttributeFormat::UInt8, 4U, true, 4U, &temporaryColor},
            {2U, GlChunkAttributeFormat::UInt16, 1U, true, static_cast<std::uint32_t>(sizeof(std::uint16_t)), &temporaryIntensity}
        };
        for (const auto& attribute : attributes) {
            if (!operations->bindArrayBuffer(attribute.buffer->id())
                || !operations->configureVertexAttribute(attribute.index, attribute.format, attribute.components, attribute.normalized, attribute.stride)
                || !operations->enableVertexAttribute(attribute.index)) {
                std::string diagnostic = "The operation table rejected VAO attribute configuration"; cleanupTemporary(diagnostic);
                return failure(GlChunkErrorCode::UploadFailed, "OpenGL vertex attribute configuration failed", diagnostic, "Chunk upload: configure attribute " + std::to_string(attribute.index));
            }
        }
        if (!operations->unbindArrayBuffer() || !operations->unbindVertexArray()) {
            std::string diagnostic = "The operation table rejected OpenGL state cleanup"; cleanupTemporary(diagnostic);
            return failure(GlChunkErrorCode::UploadFailed, "OpenGL upload state cleanup failed", diagnostic, "Chunk upload: unbind state");
        }

        GlChunkResourceStats newStats;
        newStats.pointCount = metadata.pointCount;
        newStats.schema = metadata.schema;
        newStats.hasPosition = metadata.schema.hasPosition();
        newStats.hasColor = true;
        newStats.hasIntensity = true;
        newStats.positionBytes = positionBytes;
        newStats.colorBytes = colorBytes;
        newStats.intensityBytes = intensityBytes;
        newStats.totalBytes = totalBytes;
        newStats.valid = true;

        if (mVertexArray.isValid() || mPositionBuffer.isValid() || mColorBuffer.isValid() || mIntensityBuffer.isValid()) {
            auto oldVao = mVertexArray.reset(token);
            auto oldIntensity = mIntensityBuffer.reset(token);
            auto oldColor = mColorBuffer.reset(token);
            auto oldPosition = mPositionBuffer.reset(token);
            if (!oldVao.hasValue() || !oldIntensity.hasValue() || !oldColor.hasValue() || !oldPosition.hasValue()) {
                mReleasePending = true;
                mStats.valid = isValid();
                std::string diagnostic = "The existing chunk resource could not be released before replacement";
                cleanupTemporary(diagnostic);
                return failure(GlChunkErrorCode::OperationFailed, "The existing OpenGL chunk resource could not be replaced", diagnostic, "Chunk upload: release old resource");
            }
        }

        mVertexArray = std::move(temporaryVao);
        mPositionBuffer = std::move(temporaryPosition);
        mColorBuffer = std::move(temporaryColor);
        mIntensityBuffer = std::move(temporaryIntensity);
        mOperations = std::move(operations);
        mOwnerThread = std::this_thread::get_id();
        mStats = newStats;
        mReleasePending = false;
        return Result<void>::success();
    } catch (const std::exception& exception) {
        return failure(GlChunkErrorCode::OperationFailed, "OpenGL chunk upload allocation failed",
                       exception.what(), "Chunk upload: CPU packing");
    } catch (...) {
        return failure(GlChunkErrorCode::OperationFailed, "OpenGL chunk upload failed",
                       "An unknown exception occurred during CPU packing", "Chunk upload: CPU packing");
    }
}

dzc::Result<void> GlChunkResource::reset(const GlContextThreadToken& token) {
    if (!isValid() && !mVertexArray.isValid() && !mPositionBuffer.isValid()
        && !mColorBuffer.isValid() && !mIntensityBuffer.isValid()) {
        mStats = {};
        mOperations.reset();
        mOwnerThread = std::thread::id{};
        mReleasePending = false;
        return Result<void>::success();
    }
    if (!tokenCanAccess(token, mOwnerThread)) {
        mReleasePending = true;
        return failure(GlChunkErrorCode::InvalidThreadToken,
                       "The OpenGL chunk resource token cannot destroy this resource",
                       "reset requires the thread that uploaded the chunk", "Chunk upload: reset thread token");
    }

    bool failed = false;
    std::string diagnostic;
    auto release = [&](auto& resource, const char* name) {
        if (resource.isValid()) {
            auto result = resource.reset(token);
            if (!result.hasValue()) { failed = true; if (!diagnostic.empty()) diagnostic += "; "; diagnostic += name; }
        }
    };
    release(mVertexArray, "VAO deletion failed");
    release(mIntensityBuffer, "Intensity VBO deletion failed");
    release(mColorBuffer, "Color VBO deletion failed");
    release(mPositionBuffer, "Position VBO deletion failed");
    if (failed) {
        mReleasePending = true;
        mStats.valid = isValid();
        return failure(GlChunkErrorCode::OperationFailed, "OpenGL chunk resource destruction failed",
                       diagnostic, "Chunk upload: reset");
    }
    mOperations.reset();
    mOwnerThread = std::thread::id{};
    mStats = {};
    mReleasePending = false;
    return Result<void>::success();
}

} // namespace dzc::opengl
