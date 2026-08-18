#include "dzc/EngineCommand.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <utility>

namespace dzc {
namespace {

constexpr bool isContinuationByte(std::uint8_t value) noexcept {
    return (value & 0xC0U) == 0x80U;
}

bool isValidUtf8(std::string_view text) noexcept {
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(text.data());
    std::size_t index = 0;

    while (index < text.size()) {
        const std::uint8_t first = bytes[index];
        if (first <= 0x7FU) {
            ++index;
            continue;
        }

        if (first >= 0xC2U && first <= 0xDFU) {
            if (index + 1U >= text.size() || !isContinuationByte(bytes[index + 1U])) {
                return false;
            }
            index += 2U;
            continue;
        }

        if (first == 0xE0U) {
            if (index + 2U >= text.size() || bytes[index + 1U] < 0xA0U ||
                bytes[index + 1U] > 0xBFU || !isContinuationByte(bytes[index + 2U])) {
                return false;
            }
            index += 3U;
            continue;
        }

        if ((first >= 0xE1U && first <= 0xECU) || (first >= 0xEEU && first <= 0xEFU)) {
            if (index + 2U >= text.size() || !isContinuationByte(bytes[index + 1U]) ||
                !isContinuationByte(bytes[index + 2U])) {
                return false;
            }
            index += 3U;
            continue;
        }

        if (first == 0xEDU) {
            if (index + 2U >= text.size() || bytes[index + 1U] < 0x80U ||
                bytes[index + 1U] > 0x9FU || !isContinuationByte(bytes[index + 2U])) {
                return false;
            }
            index += 3U;
            continue;
        }

        if (first == 0xF0U) {
            if (index + 3U >= text.size() || bytes[index + 1U] < 0x90U ||
                bytes[index + 1U] > 0xBFU || !isContinuationByte(bytes[index + 2U]) ||
                !isContinuationByte(bytes[index + 3U])) {
                return false;
            }
            index += 4U;
            continue;
        }

        if (first >= 0xF1U && first <= 0xF3U) {
            if (index + 3U >= text.size() || !isContinuationByte(bytes[index + 1U]) ||
                !isContinuationByte(bytes[index + 2U]) || !isContinuationByte(bytes[index + 3U])) {
                return false;
            }
            index += 4U;
            continue;
        }

        if (first == 0xF4U) {
            if (index + 3U >= text.size() || bytes[index + 1U] < 0x80U ||
                bytes[index + 1U] > 0x8FU || !isContinuationByte(bytes[index + 2U]) ||
                !isContinuationByte(bytes[index + 3U])) {
                return false;
            }
            index += 4U;
            continue;
        }

        return false;
    }

    return true;
}

bool isValidCommand(const LoadDatasetCommand& command) noexcept {
    return !command.path.empty() && isValidUtf8(command.path);
}

bool isValidCommand(const SetPointSizeCommand& command) noexcept {
    return std::isfinite(command.pixels) && command.pixels >= 1.0F && command.pixels <= 64.0F;
}

template <typename Command>
bool isValidCommand(const Command&) noexcept {
    return true;
}

} // namespace

bool isValidEngineCommand(const EngineCommand& command) noexcept {
    return std::visit([](const auto& value) noexcept { return isValidCommand(value); }, command);
}

} // namespace dzc