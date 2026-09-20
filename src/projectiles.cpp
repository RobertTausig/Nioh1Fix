#include "projectile_hooks.hpp"
#include "signatures.hpp"

namespace nioh1fix::runtime {
bool MaintainProjectileTiming(const PeImage& image,
    const CompatibilityPlan& plan, PatchSet& patches) {
    auto& posture = patches.postureTiming;
    auto& control = patches.controlScriptTiming;
    if (control.status == PatchStatus::pending)
        control.status = InstallShotControlScriptTiming(
            image, plan, control, patches.resources);
    if (control.status != PatchStatus::installed) {
        posture.status = PatchStatus::unavailable;
        return true;
    }
    if (posture.status == PatchStatus::pending)
        posture.status = InstallPostureTiming(
            image, plan, posture, patches.resources);
    if (posture.status != PatchStatus::installed) {
        if (!PatchCode(control.patch.address,
                {control.patch.applied.data(), control.patch.size},
                {control.patch.original.data(), control.patch.size},
                "Shot control-script rollback")) return false;
        control.status = PatchStatus::unavailable;
        return true;
    }
    if (!IsApplied(control.patch)) {
        Log("The Shot control-script timing call changed unexpectedly.");
        return false;
    }
    if (!IsApplied(posture.patch)) {
        Log("The Shot posture-animation call changed unexpectedly.");
        return false;
    }
    return true;
}
} // namespace nioh1fix::runtime
