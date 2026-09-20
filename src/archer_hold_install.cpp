#include "archer_diagnostic.hpp"
#include "diagnostic_hooks.hpp"
#include "signatures.hpp"

namespace nioh1fix::runtime {
bool MaintainArcherHoldDiagnostic(const PeImage& image,
    const CompatibilityPlan& plan, PatchSet& patches) {
    auto& state = patches.archerHoldDiagnostic;
    if (state.status == PatchStatus::pending) {
        CallbackDiagnosticOptions options{};
        options.callback = reinterpret_cast<const void*>(&CaptureArrowRelease);
        options.stackAlignedAfterBlock = true;
        options.failure = "Could not install the archer-hold release timer.";
        options.success = kChildShotReleaseDiagnostic.success;
        state.status = InstallCallbackDiagnostic(image,
            kChildShotReleaseDiagnostic, plan.childShotTransformBlock,
            state, patches.resources, options);
    } else if (state.status == PatchStatus::installed && !IsApplied(state.patch)) {
        Log("The archer-hold release timer changed unexpectedly.");
        return false;
    }
    return true;
}

bool MaintainActionEventDiagnostic(const PeImage& image,
    const CompatibilityPlan& plan, PatchSet& patches) {
    auto& state = patches.actionEventDiagnostic;
    auto& constructor = patches.shotConstructorDiagnostic;
    auto& motion = patches.archerMotionTiming;
    if (constructor.status == PatchStatus::pending) {
        CallbackDiagnosticOptions options{};
        options.callback = reinterpret_cast<const void*>(
            &CaptureShotConstruction);
        options.failure = "Could not install the Shot/action correlation trace.";
        options.success = kShotConstructorDiagnostic.success;
        constructor.status = InstallCallbackDiagnostic(image,
            kShotConstructorDiagnostic, plan.shotConstructorBlock,
            constructor, patches.resources, options);
    }
    if (state.status == PatchStatus::pending) {
        state.status = InstallArcherActionTiming(image, plan, patches);
    } else if (state.status == PatchStatus::installed &&
        !IsApplied(state.patch)) {
        Log("The action-event update trace changed unexpectedly.");
        return false;
    }
    if (state.status == PatchStatus::installed &&
        constructor.status == PatchStatus::installed &&
        motion.status == PatchStatus::pending)
        motion.status = InstallArcherMotionTiming(image, plan, patches);
    else if (motion.status == PatchStatus::installed &&
        !IsApplied(motion.patch)) {
        Log("The archer held-motion cadence hook changed unexpectedly.");
        return false;
    }
    if (constructor.status == PatchStatus::installed &&
        !IsApplied(constructor.patch)) {
        Log("The Shot/action correlation trace changed unexpectedly.");
        return false;
    }
    return true;
}
} // namespace nioh1fix::runtime
