#pragma once

#include <cstdint>
#include <string>

namespace dzc {

enum class ErrorDomain : std::uint8_t {
    General,
    Configuration,
    FileIo,
    DataFormat,
    Task,
    OpenGL,
    Vulkan,
    Cuda,
    Interop,
    Cache,
    Resource,
    Internal
};

struct Error final {
    ErrorDomain domain{ErrorDomain::General};
    std::uint32_t code{0};
    std::string userMessage;
    std::string diagnosticMessage;
    std::string context;
};

} // namespace dzc