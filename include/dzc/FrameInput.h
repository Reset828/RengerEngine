#pragma once

#include "dzc/EngineTypes.h"

namespace dzc {

// Backend-independent per-frame input supplied by the application host.
struct FrameInput final {
    double deltaSeconds{0.0};
    RenderSize renderSize;
};

} // namespace dzc
