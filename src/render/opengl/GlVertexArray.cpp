#include "GlVertexArray.h"

#include <utility>

namespace dzc::opengl {
namespace {

Result<void> failure(
    GlResourceErrorCode code,
    const char* userMessage,
    const char* diagnosticMessage) {
    return Result<void>::failure(Error{
        ErrorDomain::OpenGL,
        static_cast<std::uint32_t>(code),
        userMessage,
        diagnosticMessage,
        "GlVertexArray"});
}

bool tokenCanAccess(
    const GlContextThreadToken& token,
    const std::thread::id& ownerThread) noexcept {
    return token.isCurrentThread()
        && ownerThread == std::this_thread::get_id();
}

} // namespace

GlVertexArray::~GlVertexArray() noexcept {
    releaseNoexcept();
}

GlVertexArray::GlVertexArray(GlVertexArray&& other) noexcept
    : mId(other.mId),
      mOperations(std::move(other.mOperations)),
      mOwnerThread(other.mOwnerThread),
      mLabel(std::move(other.mLabel)),
      mReleasePending(other.mReleasePending) {
    other.mId = 0;
    other.mOwnerThread = std::thread::id{};
    other.mLabel.clear();
    other.mReleasePending = false;
}

GlVertexArray& GlVertexArray::operator=(GlVertexArray&& other) noexcept {
    if (this != &other) {
        GlVertexArray previous(std::move(*this));
        mId = other.mId;
        mOperations = std::move(other.mOperations);
        mOwnerThread = other.mOwnerThread;
        mLabel = std::move(other.mLabel);
        mReleasePending = other.mReleasePending;

        other.mId = 0;
        other.mOwnerThread = std::thread::id{};
        other.mLabel.clear();
        other.mReleasePending = false;
    }
    return *this;
}

Result<void> GlVertexArray::create(
    const GlContextThreadToken& token,
    std::shared_ptr<const IGlResourceOperations> operations) {
    if (!token.isCurrentThread()) {
        return failure(
            GlResourceErrorCode::InvalidThreadToken,
            "The OpenGL resource token belongs to another thread",
            "GlVertexArray::create requires a token bound to the calling thread");
    }
    if (mId != 0) {
        return failure(
            GlResourceErrorCode::OperationFailed,
            "The OpenGL vertex array already owns a resource",
            "GlVertexArray::create must be called on an empty object");
    }
    if (!operations) {
        operations = makeDefaultGlResourceOperations();
    }

    std::uint32_t generatedId = 0;
    if (!operations
        || !operations->createVertexArray(generatedId)
        || generatedId == 0) {
        return failure(
            GlResourceErrorCode::CreationFailed,
            "OpenGL vertex array creation failed",
            "The resource operation table returned no vertex-array ID");
    }

    mId = generatedId;
    mOperations = std::move(operations);
    mOwnerThread = std::this_thread::get_id();
    mLabel.clear();
    mReleasePending = false;
    return Result<void>::success();
}

Result<void> GlVertexArray::setLabel(
    const GlContextThreadToken& token,
    std::string_view label) {
    if (mId == 0) {
        return failure(
            GlResourceErrorCode::OperationFailed,
            "The OpenGL vertex array is empty",
            "GlVertexArray::setLabel requires a created vertex array");
    }
    if (!tokenCanAccess(token, mOwnerThread)) {
        return failure(
            GlResourceErrorCode::InvalidThreadToken,
            "The OpenGL resource token cannot access this vertex array",
            "GlVertexArray::setLabel requires the creating Context thread");
    }
    if (!mOperations || !mOperations->labelVertexArray(mId, label)) {
        return failure(
            GlResourceErrorCode::OperationFailed,
            "OpenGL vertex-array labeling failed",
            "The resource operation table rejected the vertex-array label");
    }

    mLabel.assign(label.data(), label.size());
    return Result<void>::success();
}

Result<void> GlVertexArray::reset(const GlContextThreadToken& token) {
    if (mId == 0) {
        return Result<void>::success();
    }
    if (!tokenCanAccess(token, mOwnerThread)) {
        mReleasePending = true;
        return failure(
            GlResourceErrorCode::InvalidThreadToken,
            "The OpenGL resource token cannot destroy this vertex array",
            "GlVertexArray::reset requires the creating Context thread");
    }
    if (!mOperations || !mOperations->deleteVertexArray(mId)) {
        mReleasePending = true;
        return failure(
            GlResourceErrorCode::OperationFailed,
            "OpenGL vertex-array destruction failed",
            "The resource operation table did not confirm vertex-array deletion");
    }

    mId = 0;
    mOperations.reset();
    mOwnerThread = std::thread::id{};
    mLabel.clear();
    mReleasePending = false;
    return Result<void>::success();
}

void GlVertexArray::releaseNoexcept() noexcept {
    if (mId == 0) {
        return;
    }
    if (mOperations
        && mOwnerThread == std::this_thread::get_id()
        && mOperations->deleteVertexArray(mId)) {
        mId = 0;
        mOperations.reset();
        mOwnerThread = std::thread::id{};
        mLabel.clear();
        mReleasePending = false;
        return;
    }
    mReleasePending = true;
}

} // namespace dzc::opengl