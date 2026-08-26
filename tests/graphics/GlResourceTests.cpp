#include "GlBuffer.h"
#include "GlVertexArray.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

using dzc::ErrorDomain;
using dzc::opengl::GlBuffer;
using dzc::opengl::GlContextThreadToken;
using dzc::opengl::GlResourceErrorCode;
using dzc::opengl::GlVertexArray;
using dzc::opengl::IGlResourceOperations;

class FakeResourceOperations final : public IGlResourceOperations {
public:
    bool failBufferCreation{false};
    bool failVertexArrayCreation{false};
    bool failBufferLabel{false};
    bool failVertexArrayLabel{false};
    bool failBufferDeletion{false};
    bool failVertexArrayDeletion{false};
    mutable std::uint32_t nextId{1};
    mutable std::vector<std::uint32_t> createdBuffers;
    mutable std::vector<std::uint32_t> createdVertexArrays;
    mutable std::vector<std::uint32_t> deletedBuffers;
    mutable std::vector<std::uint32_t> deletedVertexArrays;
    mutable std::vector<std::pair<std::uint32_t, std::string>> bufferLabels;
    mutable std::vector<std::pair<std::uint32_t, std::string>> vertexArrayLabels;

    bool createBuffer(std::uint32_t& id) const noexcept override {
        if (failBufferCreation) return false;
        id = nextId++;
        createdBuffers.push_back(id);
        return true;
    }
    bool deleteBuffer(std::uint32_t id) const noexcept override {
        if (failBufferDeletion) return false;
        deletedBuffers.push_back(id);
        return true;
    }
    bool labelBuffer(std::uint32_t id, std::string_view label) const noexcept override {
        if (failBufferLabel) return false;
        bufferLabels.emplace_back(id, std::string(label));
        return true;
    }
    bool createVertexArray(std::uint32_t& id) const noexcept override {
        if (failVertexArrayCreation) return false;
        id = nextId++;
        createdVertexArrays.push_back(id);
        return true;
    }
    bool deleteVertexArray(std::uint32_t id) const noexcept override {
        if (failVertexArrayDeletion) return false;
        deletedVertexArrays.push_back(id);
        return true;
    }
    bool labelVertexArray(std::uint32_t id, std::string_view label) const noexcept override {
        if (failVertexArrayLabel) return false;
        vertexArrayLabels.emplace_back(id, std::string(label));
        return true;
    }
};

std::shared_ptr<FakeResourceOperations> fakeOperations() {
    return std::make_shared<FakeResourceOperations>();
}

void assertOpenGlError(const dzc::Result<void>& result, GlResourceErrorCode code) {
    assert(!result.hasValue());
    assert(result.error().domain == ErrorDomain::OpenGL);
    assert(result.error().code == static_cast<std::uint32_t>(code));
    assert(!result.error().userMessage.empty());
    assert(!result.error().diagnosticMessage.empty());
    assert(!result.error().context.empty());
}

void testBufferCreationAndReset() {
    auto operations = fakeOperations();
    const auto token = GlContextThreadToken::current();
    GlBuffer buffer;
    assert(buffer.create(token, operations).hasValue());
    assert(buffer.isValid());
    assert(buffer.id() == operations->createdBuffers.front());
    assert(buffer.reset(token).hasValue());
    assert(!buffer.isValid());
    assert(operations->deletedBuffers.size() == 1);
    assert(buffer.reset(token).hasValue());
    assert(operations->deletedBuffers.size() == 1);
}

void testVertexArrayCreationAndMoveAssignment() {
    auto operations = fakeOperations();
    const auto token = GlContextThreadToken::current();
    GlVertexArray source;
    GlVertexArray target;
    assert(source.create(token, operations).hasValue());
    const auto sourceId = source.id();
    assert(target.create(token, operations).hasValue());
    const auto targetId = target.id();
    target = std::move(source);
    assert(!source.isValid());
    assert(target.id() == sourceId);
    assert(operations->deletedVertexArrays.size() == 1);
    assert(operations->deletedVertexArrays.front() == targetId);
    assert(target.reset(token).hasValue());
    assert(operations->deletedVertexArrays.size() == 2);
}

void testBufferMoveOnlyDeletesOnce() {
    auto operations = fakeOperations();
    const auto token = GlContextThreadToken::current();
    GlBuffer source;
    assert(source.create(token, operations).hasValue());
    const auto id = source.id();
    GlBuffer moved(std::move(source));
    assert(!source.isValid());
    assert(moved.id() == id);
    assert(moved.reset(token).hasValue());
    assert(operations->deletedBuffers.size() == 1);
}

void testCreationAndOperationErrors() {
    const auto token = GlContextThreadToken::current();
    auto operations = fakeOperations();
    operations->failBufferCreation = true;
    GlBuffer buffer;
    assertOpenGlError(buffer.create(token, operations), GlResourceErrorCode::CreationFailed);

    operations = fakeOperations();
    assert(buffer.create(token, operations).hasValue());
    assert(buffer.setLabel(token, "before").hasValue());
    operations->failBufferLabel = true;
    assertOpenGlError(buffer.setLabel(token, "after"), GlResourceErrorCode::OperationFailed);
    assert(buffer.label() == "before");

    operations->failBufferDeletion = true;
    assertOpenGlError(buffer.reset(token), GlResourceErrorCode::OperationFailed);
    assert(buffer.isValid());
    assert(buffer.releasePending());
    operations->failBufferDeletion = false;
    assert(buffer.reset(token).hasValue());
}

void testLabels() {
    auto operations = fakeOperations();
    const auto token = GlContextThreadToken::current();
    GlBuffer buffer;
    GlVertexArray array;
    assert(buffer.create(token, operations).hasValue());
    assert(array.create(token, operations).hasValue());
    assert(buffer.setLabel(token, "points").hasValue());
    assert(array.setLabel(token, "vao").hasValue());
    assert(buffer.label() == "points");
    assert(array.label() == "vao");
    assert(operations->bufferLabels.back().second == "points");
    assert(operations->vertexArrayLabels.back().second == "vao");
    assert(buffer.reset(token).hasValue());
    assert(array.reset(token).hasValue());
}

void testDestructorSkipsCrossThreadDeletion() {
    auto operations = fakeOperations();
    const auto token = GlContextThreadToken::current();
    auto resource = std::make_unique<GlBuffer>();
    assert(resource->create(token, operations).hasValue());
    const auto id = resource->id();

    std::thread worker([resource = std::move(resource)]() mutable {
        resource.reset();
    });
    worker.join();

    assert(operations->deletedBuffers.empty());
    assert(id != 0);
}

void testWrongThreadAndNoexceptDestruction() {
    auto operations = fakeOperations();
    const auto token = GlContextThreadToken::current();
    GlBuffer buffer;
    assert(buffer.create(token, operations).hasValue());
    dzc::Result<void> wrongThreadResult = dzc::Result<void>::success();
    std::thread worker([&] { wrongThreadResult = buffer.reset(GlContextThreadToken::current()); });
    worker.join();
    assertOpenGlError(wrongThreadResult, GlResourceErrorCode::InvalidThreadToken);
    assert(buffer.isValid());
    assert(buffer.releasePending());
    assert(operations->deletedBuffers.empty());
    assert(buffer.reset(token).hasValue());

    auto failingOperations = fakeOperations();
    failingOperations->failBufferDeletion = true;
    {
        GlBuffer noThrow;
        assert(noThrow.create(token, failingOperations).hasValue());
        assertOpenGlError(noThrow.reset(token), GlResourceErrorCode::OperationFailed);
        assert(noThrow.releasePending());
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--real-context") {
        std::cout << "SKIPPED: real OpenGL Context infrastructure is provided by GL-007.\n";
        return 77;
    }

    testBufferCreationAndReset();
    testVertexArrayCreationAndMoveAssignment();
    testBufferMoveOnlyDeletesOnce();
    testCreationAndOperationErrors();
    testLabels();
    testWrongThreadAndNoexceptDestruction();
    testDestructorSkipsCrossThreadDeletion();
    return 0;
}