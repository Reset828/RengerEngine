#pragma once

#include <cstdint>

namespace dzc {

struct DatasetId final {
    std::uint64_t value{0};

    friend constexpr bool operator==(DatasetId lhs, DatasetId rhs) noexcept {
        return lhs.value == rhs.value;
    }

    friend constexpr bool operator!=(DatasetId lhs, DatasetId rhs) noexcept {
        return !(lhs == rhs);
    }
};

struct ChunkId final {
    std::uint64_t value{0};

    friend constexpr bool operator==(ChunkId lhs, ChunkId rhs) noexcept {
        return lhs.value == rhs.value;
    }

    friend constexpr bool operator!=(ChunkId lhs, ChunkId rhs) noexcept {
        return !(lhs == rhs);
    }
};

struct FrameId final {
    std::uint64_t value{0};

    friend constexpr bool operator==(FrameId lhs, FrameId rhs) noexcept {
        return lhs.value == rhs.value;
    }

    friend constexpr bool operator!=(FrameId lhs, FrameId rhs) noexcept {
        return !(lhs == rhs);
    }
};

struct TaskId final {
    std::uint64_t value{0};

    friend constexpr bool operator==(TaskId lhs, TaskId rhs) noexcept {
        return lhs.value == rhs.value;
    }

    friend constexpr bool operator!=(TaskId lhs, TaskId rhs) noexcept {
        return !(lhs == rhs);
    }
};

struct RenderSize final {
    std::uint32_t width{0};
    std::uint32_t height{0};
    float devicePixelRatio{1.0F};

    friend constexpr bool operator==(const RenderSize& lhs, const RenderSize& rhs) noexcept {
        return lhs.width == rhs.width && lhs.height == rhs.height &&
               lhs.devicePixelRatio == rhs.devicePixelRatio;
    }

    friend constexpr bool operator!=(const RenderSize& lhs, const RenderSize& rhs) noexcept {
        return !(lhs == rhs);
    }
};

struct ColorRgba final {
    float red{0.0F};
    float green{0.0F};
    float blue{0.0F};
    float alpha{1.0F};

    friend constexpr bool operator==(const ColorRgba& lhs, const ColorRgba& rhs) noexcept {
        return lhs.red == rhs.red && lhs.green == rhs.green && lhs.blue == rhs.blue &&
               lhs.alpha == rhs.alpha;
    }

    friend constexpr bool operator!=(const ColorRgba& lhs, const ColorRgba& rhs) noexcept {
        return !(lhs == rhs);
    }
};

} // namespace dzc