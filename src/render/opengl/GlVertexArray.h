#pragma once

#include "GlResource.h"

#include <dzc/Result.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

namespace dzc::opengl {

class GlVertexArray final {
public:
    GlVertexArray() noexcept = default;
    ~GlVertexArray() noexcept;

    GlVertexArray(const GlVertexArray&) = delete;
    GlVertexArray& operator=(const GlVertexArray&) = delete;
    GlVertexArray(GlVertexArray&& other) noexcept;
    GlVertexArray& operator=(GlVertexArray&& other) noexcept;

    dzc::Result<void> create(
        const GlContextThreadToken& token,
        std::shared_ptr<const IGlResourceOperations> operations = {});
    dzc::Result<void> setLabel(
        const GlContextThreadToken& token,
        std::string_view label);
    dzc::Result<void> reset(const GlContextThreadToken& token);

    std::uint32_t id() const noexcept { return mId; }
    bool isValid() const noexcept { return mId != 0; }
    const std::string& label() const noexcept { return mLabel; }
    bool releasePending() const noexcept { return mReleasePending; }

private:
    void releaseNoexcept() noexcept;

    std::uint32_t mId{0};
    std::shared_ptr<const IGlResourceOperations> mOperations;
    std::thread::id mOwnerThread;
    std::string mLabel;
    bool mReleasePending{false};
};

} // namespace dzc::opengl