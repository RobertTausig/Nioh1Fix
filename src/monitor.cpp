#include "signatures.hpp"
#include "matrix_diagnostics.hpp"

#include <cstring>
#include <sstream>

namespace nioh1fix::runtime {
struct AnimationDiagnosticsLifetime {
    ~AnimationDiagnosticsLifetime() {
        InterlockedExchange(&g.animationDiagnosticsActive, 0);
    }
};

static bool CheckCorePatch(const PatchRecord& patch, const char* changed) {
    if (IsApplied(patch)) return true;
    Log(changed); return false;
}
static bool MaintainCore(PatchSet& patches) {
    if (!CheckCorePatch(patches.gameplay,
            "The gameplay FPS accessor changed unexpectedly after patching.")) return false;
    if (!CheckCorePatch(patches.limiter,
            "The main frame limiter changed unexpectedly.")) return false;
    return CheckCorePatch(patches.present,
                          "Present dispatch changed unexpectedly after patching.");
}
static bool MaintainOptional(const PeImage& image, const CompatibilityPlan& plan,
                             PatchSet& patches) {
    if (patches.motionStatus == PatchStatus::pending)
        patches.motionStatus = InstallMotionHooks(image, plan, patches);
    if (patches.motionStatus == PatchStatus::installed)
        for (const auto& patch : patches.motion) if (!IsApplied(patch)) {
            Log("An animation timing call changed unexpectedly."); return false;
        }
    for (std::size_t i = 0; i < kHooks.size(); ++i) {
        auto& state = patches.hooks[i];
        if (state.status == PatchStatus::pending)
            state.status = InstallBlockHook(image, kHooks[i], plan.hookBlocks[i],
                                            state, patches.resources);
        else if (state.status == PatchStatus::installed && !IsApplied(state.patch)) {
            Log(std::string("The ") + kHooks[i].name +
                " hook block changed unexpectedly.");
            return false;
        }
    }
    if (patches.inputStatus == PatchStatus::pending)
        patches.inputStatus = InstallInputHook(image, plan, patches);
    if (patches.inputStatus == PatchStatus::installed && !IsApplied(patches.input)) {
        Log("The input cadence call changed unexpectedly."); return false;
    }
    if (patches.textScrollStatus == PatchStatus::pending)
        patches.textScrollStatus = InstallTextScrollHook(image, plan, patches);
    if (patches.textScrollStatus == PatchStatus::installed &&
        !IsApplied(patches.textScroll)) {
        Log("The overflow-text scroll update changed unexpectedly."); return false;
    }
    if (patches.matrixDiagnosticsStatus == PatchStatus::pending)
        patches.matrixDiagnosticsStatus = InstallMatrixDiagnostics(image, plan, patches);
    if (patches.matrixDiagnosticsStatus == PatchStatus::installed)
        for (const auto& patch : patches.matrixCopies) if (!IsApplied(patch)) {
            Log("A model-matrix diagnostic call changed unexpectedly."); return false;
        }
    return true;
}
static const char* Normalized(PatchStatus status) {
    return status == PatchStatus::installed ? "normalized" : "baseline";
}
static void LogSummary(const PatchSet& patches, unsigned reapplyCount) {
    const bool clouds = patches.hooks[5].status == PatchStatus::installed &&
                        patches.hooks[6].status == PatchStatus::installed &&
                        patches.hooks[7].status == PatchStatus::installed;
    const bool clothDiagnostics = patches.hooks[8].status == PatchStatus::installed &&
                                  patches.hooks[9].status == PatchStatus::installed;
    std::ostringstream out;
    out << "Runtime monitor completed after " << kMonitorDurationMs
        << " ms; profile state is patched, reapply_count=" << reapplyCount
        << ", present_dispatch=" << (patches.present.address ? "non_blocking" : "not_found")
        << ", engine_synchronization=original, main_frame_limiter="
        << (patches.limiter.address ? "disabled" : "not_found")
        << ", gameplay_timing=" << (patches.gameplay.address ? "dynamic_compensation" : "not_found")
        << ", entity_animation=" << Normalized(patches.motionStatus)
        << ", vegetation_animation=" << Normalized(patches.hooks[2].status)
        << ", interface_animation=" << Normalized(patches.hooks[3].status)
        << ", overflow_text_scrolling=" << Normalized(patches.textScrollStatus)
        << ", water_animation=" << Normalized(patches.hooks[4].status)
        << ", cloud_animation=" << (clouds ? "normalized" : "baseline")
        << ", camera_input=" << Normalized(patches.hooks[0].status)
        << ", aiming_camera_input=" << Normalized(patches.hooks[1].status)
        << ", cloth_diagnostics=" << (clothDiagnostics ? "active" : "unavailable")
        << ", matrix_diagnostics="
        << (patches.matrixDiagnosticsStatus == PatchStatus::installed ?
            "active" : "unavailable")
        << ", menu_input="
        << (patches.inputStatus == PatchStatus::installed ? "60_hz_gated" : "baseline")
        << '.';
    Log(out.str());
}

bool Monitor(const PeImage& image) {
    AnimationDiagnosticsLifetime diagnosticsLifetime;
    PatchSet patches{}; CompatibilityPlan plan{};
    unsigned reapplyCount{}; bool coreInstalled{};
    for (DWORD elapsed = kMonitorIntervalMs; elapsed <= kMonitorDurationMs;
         elapsed += kMonitorIntervalMs) {
        Sleep(kMonitorIntervalMs);
        if (!coreInstalled) {
            const auto status = ResolveCompatibility(image, plan);
            if (status == ResolveStatus::pending) continue;
            if (status == ResolveStatus::incompatible) return false;
            g.imageBase = image.base;
            g.imageSize = image.headers->OptionalHeader.SizeOfImage;
            InterlockedExchange(&g.animationDiagnosticsActive, 1);
            g.activeProfile = plan.activeProfile;
            Log(plan.knownBuild ? "Recognized the validated Steam executable."
                                : "Executable is untested but signature-compatible.");
            if (!InstallCore(image, plan, patches, elapsed)) {
                g.activeProfile = nullptr; Log("Core patch transaction failed.");
                return false;
            }
            coreInstalled = true;
        } else if (!MaintainCore(patches)) return false;
        if (!MaintainOptional(image, plan, patches)) return false;
        if (elapsed % kDiagnosticsIntervalMs == 0) LogDiagnostics(plan.table, elapsed);
        const auto tableBytes = std::span<const std::uint8_t>(
            plan.table, kFrameProfileSignature.size());
        const auto state = InspectGameplayProfiles(tableBytes, float(kInternalTargetFps));
        if (state == ProfileState::patched) continue;
        if (state == ProfileState::invalid) {
            std::ostringstream out; out << "Frame profile data changed unexpectedly after "
                << elapsed << " ms; monitoring stopped."; Log(out.str());
            return false;
        }
        std::ostringstream out; out << "Frame profile reset to 60 FPS after "
            << elapsed << " ms; reapplying the patch."; Log(out.str());
        if (!SetFrameProfiles(plan.table, true)) {
            Log("Failed to reapply the framerate patch."); return false;
        }
        ++reapplyCount;
    }
    if (!coreInstalled) {
        Log("No complete compatible patch plan was found; no changes were made.");
        return false;
    }
    LogSummary(patches, reapplyCount);
    return patches.present.address && patches.limiter.address && patches.gameplay.address;
}
} // namespace nioh1fix::runtime
