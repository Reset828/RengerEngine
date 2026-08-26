#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <thread>

namespace dzc::opengl {

enum class GlResourceErrorCode : std::uint32_t {
    CreationFailed = 1,
    InvalidThreadToken = 2,
    OperationFailed = 3
};

class GlContextThreadToken final {
public:
    static GlContextThreadToken current() noexcept;

    bool isCurrentThread() const noexcept;

private:
    explicit GlContextThreadToken(std::thread::id threadId) noexcept
        : mThreadId(threadId) {}

    std::thread::id mThreadId;

    friend class GlBuffer;
    friend class GlVertexArray;
};

class IGlResourceOperations {
public:
    virtual ~IGlResourceOperations() = default;

    virtual bool createBuffer(std::uint32_t& id) const noexcept = 0;
    virtual bool deleteBuffer(std::uint32_t id) const noexcept = 0;
    virtual bool labelBuffer(std::uint32_t id, std::string_view label) const noexcept = 0;

    virtual bool createVertexArray(std::uint32_t& id) const noexcept = 0;
    virtual bool deleteVertexArray(std::uint32_t id) const noexcept = 0;
    virtual bool labelVertexArray(std::uint32_t id, std::string_view label) const noexcept = 0;
};

std::shared_ptr<const IGlResourceOperations> makeDefaultGlResourceOperations();

} // namespace dzc::opengl