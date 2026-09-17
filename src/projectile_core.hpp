#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace nioh1fix {
inline constexpr double kProjectilePostureCadenceHz = 30.0;
inline constexpr double kShotDeltaUnitsPerSecond = 30.0;
inline constexpr double kMinimumProjectileStepProgress = 0.03;
inline constexpr double kMaximumProjectileStepProgress = 6.0;
inline bool UsesDynamicProjectileCadence(bool thirtyFpsProfile, double delta) {
    return !thirtyFpsProfile && std::isfinite(delta) && delta > 0.0;
}

inline int AdvanceProjectileCadence(double& accumulator, double delta,
                                    bool thirtyFpsProfile) {
    if (!UsesDynamicProjectileCadence(thirtyFpsProfile, delta)) return 1;
    const double stepScale =
        kProjectilePostureCadenceHz / kShotDeltaUnitsPerSecond;
    accumulator += std::clamp(delta * stepScale,
                              kMinimumProjectileStepProgress,
                              kMaximumProjectileStepProgress);
    const int steps = static_cast<int>(accumulator);
    accumulator -= steps;
    return steps;
}

} // namespace nioh1fix
