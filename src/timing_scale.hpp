#pragma once

#include <algorithm>
#include <cmath>

namespace nioh1fix
{
inline constexpr double kReferenceFps = 60.0;
inline constexpr double kMinimumMeasuredFps = 10.0;
inline constexpr double kMaximumMeasuredFps = 2000.0;
inline constexpr double kMinimumTimingScale =
    kReferenceFps / kMaximumMeasuredFps;
inline constexpr double kMaximumTimingScale =
    kReferenceFps / kMinimumMeasuredFps;
inline constexpr double kMaximumPresentationIntervalSeconds =
    1.0 / kMinimumMeasuredFps;
inline constexpr double kTimingSmoothing = 0.25;

struct TimingScaleState
{
    double scale{1.0};
    bool initialized{};
};

inline double BoundedFallbackScale(double fallbackFps)
{
    if (!std::isfinite(fallbackFps) || fallbackFps <= 0.0) {
        return 1.0;
    }
    return std::clamp(
        kReferenceFps / fallbackFps,
        kMinimumTimingScale,
        kMaximumTimingScale);
}

inline double UpdateTimingScale(TimingScaleState& state,
                                double intervalSeconds,
                                double fallbackFps)
{
    if (!std::isfinite(intervalSeconds) || intervalSeconds <= 0.0 ||
        intervalSeconds > kMaximumPresentationIntervalSeconds) {
        if (!state.initialized) {
            state.scale = BoundedFallbackScale(fallbackFps);
        }
        return state.scale;
    }

    const double measuredScale = std::clamp(
        intervalSeconds * kReferenceFps,
        kMinimumTimingScale,
        kMaximumTimingScale);
    if (!state.initialized) {
        state.scale = measuredScale;
        state.initialized = true;
        return state.scale;
    }

    state.scale += (measuredScale - state.scale) * kTimingSmoothing;
    state.scale = std::clamp(
        state.scale, kMinimumTimingScale, kMaximumTimingScale);
    return state.scale;
}
} // namespace nioh1fix
