#include "GlChunkResource.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

using dzc::AttributeSchema;
using dzc::ChunkCpuData;
using dzc::ChunkMetadata;
using dzc::ErrorDomain;
using dzc::PointAttribute;
using dzc::opengl::GlChunkAttributeFormat;
using dzc::opengl::GlChunkBufferUsage;
using dzc::opengl::GlChunkErrorCode;
using dzc::opengl::GlChunkResource;
using dzc::opengl::GlContextThreadToken;
using dzc::opengl::IGlChunkUploadOperations;

struct Upload final {
    std::uint32_t id{};
    std::vector<std::byte> bytes;
    GlChunkBufferUsage usage{};
};

struct Attribute final {
    std::uint32_t index{};
    GlChunkAttributeFormat format{};
    std::uint32_t components{};
    bool normalized{};
    std::uint32_t stride{};
};

class FakeChunkOperations final : public IGlChunkUploadOperations {
public:
    mutable std::uint32_t nextId{1U};
    mutable bool failBufferCreation{false};
    mutable bool failVertexArrayCreation{false};
    mutable bool failDeleteBuffer{false};
    mutable bool failDeleteVertexArray{false};
    mutable bool failBind{false};
    mutable bool failUpload{false};
    mutable bool failConfigure{false};
    mutable bool failEnable{false};
    mutable bool failUnbind{false};
    mutable std::vector<std::uint32_t> createdBuffers;
    mutable std::vector<std::uint32_t> createdVertexArrays;
    mutable std::vector<std::uint32_t> deletedBuffers;
    mutable std::vector<std::uint32_t> deletedVertexArrays;
    mutable std::vector<Upload> uploads;
    mutable std::vector<Attribute> attributes;

    bool createBuffer(std::uint32_t& id) const noexcept override {
        if (failBufferCreation) return false;
        id = nextId++;
        createdBuffers.push_back(id);
        return true;
    }
    bool deleteBuffer(std::uint32_t id) const noexcept override {
        if (failDeleteBuffer) return false;
        deletedBuffers.push_back(id);
        return true;
    }
    bool labelBuffer(std::uint32_t, std::string_view) const noexcept override { return true; }
    bool createVertexArray(std::uint32_t& id) const noexcept override {
        if (failVertexArrayCreation) return false;
        id = nextId++;
        createdVertexArrays.push_back(id);
        return true;
    }
    bool deleteVertexArray(std::uint32_t id) const noexcept override {
        if (failDeleteVertexArray) return false;
        deletedVertexArrays.push_back(id);
        return true;
    }
    bool labelVertexArray(std::uint32_t, std::string_view) const noexcept override { return true; }

    bool bindVertexArray(std::uint32_t) const noexcept override { return !failBind; }
    bool bindArrayBuffer(std::uint32_t) const noexcept override { return !failBind; }
    bool uploadArrayBuffer(std::uint32_t id, const std::vector<std::byte>& bytes,
                           GlChunkBufferUsage usage) const noexcept override {
        if (failUpload) return false;
        uploads.push_back(Upload{id, bytes, usage});
        return true;
    }
    bool configureVertexAttribute(std::uint32_t index, GlChunkAttributeFormat format,
                                  std::uint32_t components, bool normalized,
                                  std::uint32_t stride) const noexcept override {
        if (failConfigure) return false;
        attributes.push_back(Attribute{index, format, components, normalized, stride});
        return true;
    }
    bool enableVertexAttribute(std::uint32_t) const noexcept override { return !failEnable; }
    bool unbindArrayBuffer() const noexcept override { return !failUnbind; }
    bool unbindVertexArray() const noexcept override { return !failUnbind; }
};

AttributeSchema schema(std::uint32_t mask) {
    return AttributeSchema{mask};
}

ChunkMetadata metadata(std::uint64_t count, std::uint32_t mask) {
    ChunkMetadata value{};
    value.pointCount = count;
    value.schema = schema(mask);
    return value;
}

ChunkCpuData cpuData(std::size_t count, bool color, bool intensity) {
    ChunkCpuData value;
    for (std::size_t i = 0; i < count; ++i) {
        value.positions.emplace_back(static_cast<float>(i), static_cast<float>(i + 1U), static_cast<float>(i + 2U));
        if (color) value.colorsRgba8.push_back(0x10203040U + static_cast<std::uint32_t>(i));
        if (intensity) value.intensities.push_back(static_cast<std::uint16_t>(100U + i));
    }
    return value;
}

std::uint32_t mask(bool color, bool intensity) {
    return static_cast<std::uint32_t>(PointAttribute::Position)
        | (color ? static_cast<std::uint32_t>(PointAttribute::Color) : 0U)
        | (intensity ? static_cast<std::uint32_t>(PointAttribute::Intensity) : 0U);
}

void assertOpenGlError(const dzc::Result<void>& result, GlChunkErrorCode code) {
    assert(!result.hasValue());
    assert(result.error().domain == ErrorDomain::OpenGL);
    assert(result.error().code == static_cast<std::uint32_t>(code));
    assert(!result.error().userMessage.empty());
    assert(!result.error().diagnosticMessage.empty());
    assert(!result.error().context.empty());
}

void testSuccessfulStreamsAndAttributes() {
    auto operations = std::make_shared<FakeChunkOperations>();
    GlChunkResource resource;
    const auto token = GlContextThreadToken::current();
    auto data = cpuData(2U, true, true);
    assert(resource.upload(token, metadata(2U, mask(true, true)), data, operations).hasValue());
    assert(resource.isValid());
    assert(resource.pointCount() == 2U);
    assert(resource.stats().positionBytes == 24U);
    assert(resource.stats().colorBytes == 8U);
    assert(resource.stats().intensityBytes == 4U);
    assert(resource.stats().totalBytes == 36U);
    assert(resource.stats().hasPosition && resource.stats().hasColor && resource.stats().hasIntensity);
    assert(operations->uploads.size() == 3U);
    assert(operations->attributes.size() == 3U);
    assert(operations->uploads[0].usage == GlChunkBufferUsage::StaticDraw);
    const auto& color = operations->uploads[1].bytes;
    const std::uint8_t expectedColor[] = {0x10U, 0x20U, 0x30U, 0x40U, 0x10U, 0x20U, 0x30U, 0x41U};
    assert(std::memcmp(color.data(), expectedColor, sizeof(expectedColor)) == 0);
    const auto& intensity = operations->uploads[2].bytes;
    const std::uint16_t expectedIntensity[] = {100U, 101U};
    assert(std::memcmp(intensity.data(), expectedIntensity, sizeof(expectedIntensity)) == 0);
    assert(operations->attributes[0].index == 0U && operations->attributes[0].format == GlChunkAttributeFormat::Float32);
    assert(operations->attributes[0].components == 3U && !operations->attributes[0].normalized && operations->attributes[0].stride == 12U);
    assert(operations->attributes[1].index == 1U && operations->attributes[1].format == GlChunkAttributeFormat::UInt8);
    assert(operations->attributes[1].components == 4U && operations->attributes[1].normalized && operations->attributes[1].stride == 4U);
    assert(operations->attributes[2].index == 2U && operations->attributes[2].format == GlChunkAttributeFormat::UInt16);
    assert(operations->attributes[2].components == 1U && operations->attributes[2].normalized && operations->attributes[2].stride == 2U);
    assert(resource.reset(token).hasValue());
    assert(operations->deletedVertexArrays.size() == 1U);
    assert(operations->deletedBuffers.size() == 3U);
}

void testMissingStreamsAndReplacement() {
    auto operations = std::make_shared<FakeChunkOperations>();
    GlChunkResource resource;
    const auto token = GlContextThreadToken::current();
    assert(resource.upload(token, metadata(3U, mask(false, false)), cpuData(3U, false, false), operations).hasValue());
    assert(resource.stats().colorBytes == 12U && resource.stats().intensityBytes == 6U);
    assert(resource.stats().hasPosition && resource.stats().hasColor && resource.stats().hasIntensity);
    const std::uint8_t white[] = {255U, 255U, 255U, 255U};
    for (std::size_t offset = 0; offset < operations->uploads[1].bytes.size(); offset += 4U) {
        assert(std::memcmp(operations->uploads[1].bytes.data() + offset, white, sizeof(white)) == 0);
    }
    const std::uint16_t fullIntensity = 65535U;
    for (std::size_t offset = 0; offset < operations->uploads[2].bytes.size(); offset += sizeof(fullIntensity)) {
        assert(std::memcmp(operations->uploads[2].bytes.data() + offset, &fullIntensity, sizeof(fullIntensity)) == 0);
    }
    const auto oldPointCount = resource.pointCount();
    const auto oldCreated = operations->createdBuffers.size();
    operations->failUpload = true;
    auto failed = resource.upload(token, metadata(1U, mask(false, false)), cpuData(1U, false, false), operations);
    assertOpenGlError(failed, GlChunkErrorCode::UploadFailed);
    assert(resource.isValid() && resource.pointCount() == oldPointCount);
    assert(operations->createdBuffers.size() > oldCreated);
    operations->failUpload = false;
    assert(resource.upload(token, metadata(1U, mask(false, false)), cpuData(1U, false, false), operations).hasValue());
    assert(resource.pointCount() == 1U);
    assert(resource.reset(token).hasValue());
}

void testValidationAndOverflow() {
    auto operations = std::make_shared<FakeChunkOperations>();
    const auto token = GlContextThreadToken::current();
    GlChunkResource resource;
    const auto before = operations->createdBuffers.size() + operations->createdVertexArrays.size();
    auto noPosition = resource.upload(token, metadata(1U, 0U), cpuData(1U, false, false), operations);
    assertOpenGlError(noPosition, GlChunkErrorCode::InvalidInput);
    assert(before == operations->createdBuffers.size() + operations->createdVertexArrays.size());
    auto wrongColor = cpuData(1U, true, false);
    auto mismatch = resource.upload(token, metadata(2U, mask(true, false)), wrongColor, operations);
    assertOpenGlError(mismatch, GlChunkErrorCode::InvalidInput);
    auto stray = cpuData(1U, false, false);
    stray.colorsRgba8.push_back(0xFFFFFFFFU);
    auto strayResult = resource.upload(token, metadata(1U, mask(false, false)), stray, operations);
    assertOpenGlError(strayResult, GlChunkErrorCode::InvalidInput);
    auto overflow = resource.upload(token, metadata(UINT64_MAX, mask(false, false)), {}, operations);
    assertOpenGlError(overflow, GlChunkErrorCode::SizeOverflow);
}

void testSchemaCombinationsAndFailures() {
    const auto token = GlContextThreadToken::current();
    for (const auto combination : {std::pair<bool, bool>{false, false}, {true, false}, {false, true}, {true, true}}) {
        auto operations = std::make_shared<FakeChunkOperations>();
        GlChunkResource resource;
        assert(resource.upload(token, metadata(2U, mask(combination.first, combination.second)),
                               cpuData(2U, combination.first, combination.second), operations).hasValue());
        assert(resource.schema().hasColor() == combination.first);
        assert(resource.schema().hasIntensity() == combination.second);
        assert(resource.reset(token).hasValue());
    }

    for (int failureKind = 0; failureKind < 7; ++failureKind) {
        auto operations = std::make_shared<FakeChunkOperations>();
        if (failureKind == 0) operations->failVertexArrayCreation = true;
        if (failureKind == 1) operations->failBufferCreation = true;
        if (failureKind == 2) operations->failBind = true;
        if (failureKind == 3) operations->failUpload = true;
        if (failureKind == 4) operations->failConfigure = true;
        if (failureKind == 5) operations->failEnable = true;
        if (failureKind == 6) operations->failUnbind = true;
        GlChunkResource resource;
        auto result = resource.upload(token, metadata(1U, mask(true, true)), cpuData(1U, true, true), operations);
        assertOpenGlError(result, failureKind <= 1 ? GlChunkErrorCode::CreationFailed : GlChunkErrorCode::UploadFailed);
        assert(!resource.isValid());
        assert(operations->deletedVertexArrays.size() == operations->createdVertexArrays.size());
        assert(operations->deletedBuffers.size() == operations->createdBuffers.size());
    }

    auto deleteOperations = std::make_shared<FakeChunkOperations>();
    GlChunkResource resource;
    assert(resource.upload(token, metadata(1U, mask(true, true)), cpuData(1U, true, true), deleteOperations).hasValue());
    deleteOperations->failDeleteBuffer = true;
    auto deleteResult = resource.reset(token);
    assertOpenGlError(deleteResult, GlChunkErrorCode::OperationFailed);
    assert(resource.releasePending());
    deleteOperations->failDeleteBuffer = false;
    assert(resource.reset(token).hasValue());
}

void testReplacementAndMoveAssignmentDeleteOnce() {
    auto operations = std::make_shared<FakeChunkOperations>();
    const auto token = GlContextThreadToken::current();
    GlChunkResource resource;
    assert(resource.upload(token, metadata(1U, mask(true, true)), cpuData(1U, true, true), operations).hasValue());
    assert(resource.upload(token, metadata(2U, mask(false, false)), cpuData(2U, false, false), operations).hasValue());
    assert(operations->deletedVertexArrays.size() == 1U);
    assert(operations->deletedBuffers.size() == 3U);

    GlChunkResource target;
    assert(target.upload(token, metadata(1U, mask(true, false)), cpuData(1U, true, false), operations).hasValue());
    const auto deletedVaosBeforeMove = operations->deletedVertexArrays.size();
    const auto deletedBuffersBeforeMove = operations->deletedBuffers.size();
    target = std::move(resource);
    assert(!resource.isValid() && target.isValid());
    assert(operations->deletedVertexArrays.size() == deletedVaosBeforeMove + 1U);
    assert(operations->deletedBuffers.size() == deletedBuffersBeforeMove + 3U);
    assert(target.reset(token).hasValue());
}
void testCrossThreadDestructorSkipsDeletion() {
    auto operations = std::make_shared<FakeChunkOperations>();
    std::unique_ptr<GlChunkResource> resource;
    std::thread worker([&] {
        auto local = std::make_unique<GlChunkResource>();
        const auto token = GlContextThreadToken::current();
        assert(local->upload(token, metadata(1U, mask(true, true)), cpuData(1U, true, true), operations).hasValue());
        resource = std::move(local);
    });
    worker.join();
    resource.reset();
    assert(operations->deletedVertexArrays.empty());
    assert(operations->deletedBuffers.empty());
}
void testMovesAndThreadToken() {
    auto operations = std::make_shared<FakeChunkOperations>();
    const auto token = GlContextThreadToken::current();
    GlChunkResource source;
    assert(source.upload(token, metadata(1U, mask(true, true)), cpuData(1U, true, true), operations).hasValue());
    const auto createdBuffers = operations->createdBuffers;
    GlChunkResource moved(std::move(source));
    assert(!source.isValid() && moved.isValid());
    GlChunkResource target;
    target = std::move(moved);
    assert(!moved.isValid() && target.isValid());
    std::optional<GlContextThreadToken> wrongToken;
    std::thread worker([&wrongToken] { wrongToken = GlContextThreadToken::current(); });
    worker.join();
    GlChunkResource empty;
    auto uploadResult = empty.upload(*wrongToken, metadata(1U, mask(false, false)), cpuData(1U, false, false), operations);
    assertOpenGlError(uploadResult, GlChunkErrorCode::InvalidThreadToken);
    auto result = target.reset(*wrongToken);
    assertOpenGlError(result, GlChunkErrorCode::InvalidThreadToken);
    assert(target.releasePending());
    assert(target.reset(token).hasValue());
    assert(operations->deletedBuffers.size() == createdBuffers.size());
}

} // namespace

int main() {
    static_assert(std::is_nothrow_destructible_v<GlChunkResource>);
    testSuccessfulStreamsAndAttributes();
    testMissingStreamsAndReplacement();
    testValidationAndOverflow();
    testSchemaCombinationsAndFailures();
    testMovesAndThreadToken();
    testReplacementAndMoveAssignmentDeleteOnce();
    testCrossThreadDestructorSkipsDeletion();
    return 0;
}
