#include "signatures.hpp"

namespace nioh1fix::runtime {
bool MaintainProjectileTiming(const PeImage& image,
    const CompatibilityPlan& plan, PatchSet& patches) {
    auto& state = patches.postureTiming;
    if (state.status == PatchStatus::pending)
        state.status = InstallPostureTiming(
            image, plan, state, patches.resources);
    else if (state.status == PatchStatus::installed && !IsApplied(state.patch)) {
        Log("The Shot posture-animation call changed unexpectedly.");
        return false;
    }
    return true;
}
} // namespace nioh1fix::runtime
