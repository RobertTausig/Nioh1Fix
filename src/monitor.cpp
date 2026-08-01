#include "signatures.hpp"

#include <cstring>
#include <sstream>

namespace nioh1fix::runtime {
static bool CheckCorePatch(const PatchRecord& patch, const char* changed) {
    if (!patch.address || IsApplied(patch)) return true;
    Log(changed); return false;
}
static bool MaintainCore(const PeImage& image, PatchSet& patches, DWORD elapsed) {
    if (!patches.gameplay.address && !InstallGameplayHook(image, patches.gameplay))
        return false;
    if (!CheckCorePatch(patches.gameplay,
            "The gameplay FPS accessor changed unexpectedly after patching.")) return false;
    if (!patches.limiter.address && !InstallLimiter(image, patches.limiter)) return false;
    if (!CheckCorePatch(patches.limiter,
            "The main frame limiter changed unexpectedly.")) return false;
    if (!patches.present.address && !InstallPresentHook(image, patches.present, elapsed))
        return false;
    return CheckCorePatch(patches.present,
                          "Present dispatch changed unexpectedly after patching.");
}
static bool MaintainOptional(const PeImage& image, PatchSet& patches) {
    if (patches.motionStatus == PatchStatus::pending)
        patches.motionStatus = InstallMotionHooks(image, patches);
    if (patches.motionStatus == PatchStatus::installed)
        for (const auto& patch : patches.motion) if (!IsApplied(patch)) {
            Log("An animation timing call changed unexpectedly."); return false;
        }
    for (std::size_t i = 0; i < kHooks.size(); ++i) {
        auto& state = patches.hooks[i];
        if (state.status == PatchStatus::pending)
            state.status = InstallBlockHook(image, kHooks[i], state, patches.resources);
        else if (state.status == PatchStatus::installed && !IsApplied(state.patch)) {
            Log(std::string("The ") + kHooks[i].name +
                " timing block changed unexpectedly.");
            return false;
        }
    }
    if (patches.inputStatus == PatchStatus::pending)
        patches.inputStatus = InstallInputHook(image, patches);
    if (patches.inputStatus == PatchStatus::installed && !IsApplied(patches.input)) {
        Log("The input cadence call changed unexpectedly."); return false;
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
        << ", water_animation=" << Normalized(patches.hooks[4].status)
        << ", cloud_animation=" << (clouds ? "normalized" : "baseline")
        << ", camera_input=" << Normalized(patches.hooks[0].status)
        << ", aiming_camera_input=" << Normalized(patches.hooks[1].status)
        << ", menu_input="
        << (patches.inputStatus == PatchStatus::installed ? "60_hz_gated" : "baseline")
        << '.';
    Log(out.str());
}

bool Monitor(const PeImage& image, std::uint8_t* table) {
    const auto tableBytes = std::span<const std::uint8_t>(
        table, kFrameProfileSignature.size());
    PatchSet patches{}; unsigned reapplyCount{};
    for (DWORD elapsed = kMonitorIntervalMs; elapsed <= kMonitorDurationMs;
         elapsed += kMonitorIntervalMs) {
        Sleep(kMonitorIntervalMs);
        if (!MaintainCore(image, patches, elapsed) ||
            !MaintainOptional(image, patches)) return false;
        if (elapsed % kDiagnosticsIntervalMs == 0) LogDiagnostics(table, elapsed);
        const auto state = InspectGameplayProfiles(tableBytes, float(kInternalTargetFps));
        if (state == ProfileState::patched) continue;
        if (state == ProfileState::invalid) {
            std::ostringstream out; out << "Frame profile data changed unexpectedly after "
                << elapsed << " ms; monitoring stopped."; Log(out.str());
            return false;
        }
        std::ostringstream out; out << "Frame profile reset to 60 FPS after "
            << elapsed << " ms; reapplying the patch."; Log(out.str());
        if (!ApplyFrameProfiles(table)) {
            Log("Failed to reapply the framerate patch."); return false;
        }
        ++reapplyCount;
    }
    LogSummary(patches, reapplyCount);
    return patches.present.address && patches.limiter.address && patches.gameplay.address;
}
} // namespace nioh1fix::runtime
