#include "StatusPresenter.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace dzc {
namespace {

QString backendName(RenderBackendType value) {
    switch (value) {
    case RenderBackendType::OpenGL: return QStringLiteral("OpenGL");
    case RenderBackendType::Vulkan: return QStringLiteral("Vulkan");
    }
    return QStringLiteral("Unknown");
}

QString datasetStateName(DatasetState value) {
    switch (value) {
    case DatasetState::None: return QStringLiteral("None");
    case DatasetState::Opening: return QStringLiteral("Opening");
    case DatasetState::Building: return QStringLiteral("Building");
    case DatasetState::Ready: return QStringLiteral("Ready");
    case DatasetState::Cancelling: return QStringLiteral("Cancelling");
    case DatasetState::Error: return QStringLiteral("Error");
    }
    return QStringLiteral("Unknown");
}

QString cudaModeName(OptionalFeatureMode value) {
    switch (value) {
    case OptionalFeatureMode::Off: return QStringLiteral("Off");
    case OptionalFeatureMode::On: return QStringLiteral("On");
    case OptionalFeatureMode::Auto: return QStringLiteral("Auto");
    }
    return QStringLiteral("Unknown");
}

QString yesNo(bool value) {
    return value ? QStringLiteral("Yes") : QStringLiteral("No");
}

QString decimal(std::uint64_t value) {
    return QString::number(static_cast<qulonglong>(value));
}

} // namespace

StatusPresentation StatusPresenter::format(const std::shared_ptr<const EngineSnapshot>& snapshot) {
    const EngineSnapshot empty;
    const EngineSnapshot& value = snapshot ? *snapshot : empty;
    StatusPresentation result;
    result.backend = snapshot ? backendName(value.backend) : QStringLiteral("Unknown");
    const double fps = value.performance.framesPerSecond;
    result.framesPerSecond = std::isfinite(fps) && fps >= 0.0
        ? QString::number(fps == 0.0 ? 0.0 : fps, 'f', 2) : QStringLiteral("Unknown");
    result.datasetState = datasetStateName(value.dataset.state);
    const double progress = std::isfinite(value.dataset.progress)
        ? std::clamp(value.dataset.progress, 0.0, 1.0) : 0.0;
    result.loadProgress = QString::number(static_cast<int>(std::lround(progress * 100.0))) + QStringLiteral("%");
    result.totalPoints = decimal(value.dataset.totalPointCount);
    result.visiblePoints = decimal(value.dataset.visiblePointCount);
    result.totalChunks = decimal(value.dataset.chunkCount);
    result.visibleChunks = decimal(value.dataset.visibleChunkCount);
    result.cpuResidency = decimal(value.memory.cpuResidentBytes);
    result.cpuBudget = decimal(value.memory.cpuBudgetBytes);
    result.gpuResidency = decimal(value.memory.gpuResidentBytes);
    result.gpuBudget = decimal(value.memory.gpuBudgetBytes);
    result.cudaAvailable = snapshot ? yesNo(value.cudaAvailable) : QStringLiteral("Unknown");
    result.cudaMode = snapshot ? cudaModeName(value.cudaMode) : QStringLiteral("Unknown");
    result.cudaEnabled = snapshot ? yesNo(value.cudaEnabled) : QStringLiteral("Unknown");
    result.currentError = QStringLiteral("None");
    if (value.mostRecentError) {
        const auto& message = value.mostRecentError->userMessage;
        result.currentError = message.empty() ? QStringLiteral("Unknown")
            : QString::fromUtf8(message.data(), static_cast<int>(message.size()));
    }
    return result;
}

} // namespace dzc
