#pragma once

#include "dzc/EngineSnapshot.h"

#include <QString>
#include <memory>

namespace dzc {

struct StatusPresentation final {
    QString backend;
    QString framesPerSecond;
    QString datasetState;
    QString loadProgress;
    QString totalPoints;
    QString visiblePoints;
    QString totalChunks;
    QString visibleChunks;
    QString cpuResidency;
    QString cpuBudget;
    QString gpuResidency;
    QString gpuBudget;
    QString cudaAvailable;
    QString cudaMode;
    QString cudaEnabled;
    QString currentError;
};

class StatusPresenter final {
public:
    // Formats snapshot values without retaining the snapshot or touching widgets.
    static StatusPresentation format(const std::shared_ptr<const EngineSnapshot>& snapshot);
};

} // namespace dzc
