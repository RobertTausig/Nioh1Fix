#pragma once

#include "runtime.hpp"

namespace nioh1fix::runtime {
std::string PostureTimingDiagnostics();
bool NormalizedShotControlScriptTick(void* controlScript);
PatchStatus InstallPostureTiming(const PeImage& image,
    const CompatibilityPlan& plan, HookState& state, HookResources& resources);
PatchStatus InstallShotControlScriptTiming(const PeImage& image,
    const CompatibilityPlan& plan, HookState& state, HookResources& resources);
bool MaintainProjectileTiming(const PeImage& image,
    const CompatibilityPlan& plan, PatchSet& patches);
} // namespace nioh1fix::runtime
