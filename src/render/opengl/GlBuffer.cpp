#include "GlBuffer.h"

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
        "GlBuffer"});
}

bool tokenCanAccess(
    const GlContextThreadToken& token,
    const std::thread::id& ownerThread) noexcept {
    return token.isCurrentThread()
        && ownerThread == std::this_thread::get_id();
}

} // namespace

GlBuffer::~GlBuffer() noexcept {
    releaseNoexcept();
}

GlBuffer::GlBuffer(GlBuffer&& other) noexcept
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

GlBuffer& GlBuffer::operator=(GlBuffer&& other) noexcept {
    if (this != &other) {
        GlBuffer previous(std::move(*this));
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

Result<void> GlBuffer::create(
    const GlContextThreadToken& token,
    std::shared_ptr<const IGlResourceOperations> operations) {
    if (!token.isCurrentThread()) {
        return failure(
            GlResourceErrorCode::InvalidThreadToken,
            "The OpenGL resource token belongs to another thread",
            "GlBuffer::create requires a token bound to the calling thread");
    }
    if (mId != 0) {
        return failure(
            GlResourceErrorCode::OperationFailed,
            "The OpenGL buffer already owns a resource",
            "GlBuffer::create must be called on an empty object");
    }
    if (!operations) {
        operations = makeDefaultGlResourceOperations();
    }

    std::uint32_t generatedId = 0;
    if (!operations
        || !operations->createBuffer(generatedId)
        || generatedId == 0) {
        return failure(
            GlResourceErrorCode::CreationFailed,
            "OpenGL buffer creation failed",
            "The resource operation table returned no buffer ID");
    }

    mId = generatedId;
    mOperations = std::move(operations);
    mOwnerThread = std::this_thread::get_id();
    mLabel.clear();
    mReleasePending = false;
    return Result<void>::success();
}

Result<void> GlBuffer::setLabel(
    const GlContextThreadToken& token,
    std::string_view label) {
    if (mId == 0) {
        return failure(
            GlResourceErrorCode::OperationFailed,
            "The OpenGL buffer is empty",
            "GlBuffer::setLabel requires a created buffer");
    }
    if (!tokenCanAccess(token, mOwnerThread)) {
        return failure(
            GlResourceErrorCode::InvalidThreadToken,
            "The OpenGL resource token cannot access this buffer",
            "GlBuffer::setLabel requires the creating Context thread");
    }
    if (!mOperations || !mOperations->labelBuffer(mId, label)) {
        return failure(
            GlResourceErrorCode::OperationFailed,
            "OpenGL buffer labeling failed",
            "The resource operation table rejected the buffer label");
    }

    mLabel.assign(label.data(), label.size());
    return Result<void>::success();
}

Result<void> GlBuffer::reset(const GlContextThreadToken& token) {
    if (mId == 0) {
        return Result<void>::success();
    }
    if (!tokenCanAccess(token, mOwnerThread)) {
        mReleasePending = true;
        return failure(
            GlResourceErrorCode::InvalidThreadToken,
            "The OpenGL resource token cannot destroy this buffer",
            "GlBuffer::reset requires the creating Context thread");
    }
    if (!mOperations || !mOperations->deleteBuffer(mId)) {
        mReleasePending = true;
        return failure(
            GlResourceErrorCode::OperationFailed,
            "OpenGL buffer destruction failed",
            "The resource operation table did not confirm buffer deletion");
    }

    mId = 0;
    mOperations.reset();
    mOwnerThread = std::thread::id{};
    mLabel.clear();
    mReleasePending = false;
    return Result<void>::success();
}

void GlBuffer::releaseNoexcept() noexcept {
    if (mId == 0) {
        return;
    }
    if (mOperations
        && mOwnerThread == std::this_thread::get_id()
        && mOperations->deleteBuffer(mId)) {
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