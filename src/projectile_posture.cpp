#include "projectile_core.hpp"
#include "runtime.hpp"

#include <array>
#include <cstring>
#include <sstream>

namespace nioh1fix::runtime {
struct PostureCadenceRecord {
    void* posture{};
    double accumulator{};
    ULONGLONG lastTick{};
};

static SRWLOCK cadenceLock = SRWLOCK_INIT;
static std::array<PostureCadenceRecord, 64> cadenceRecords{};

static PostureCadenceRecord& FindCadenceRecord(void* posture, ULONGLONG now) {
    PostureCadenceRecord* unused = nullptr;
    PostureCadenceRecord* oldest = cadenceRecords.data();
    for (auto& record : cadenceRecords) {
        if (record.posture == posture) return record;
        if (!record.posture && !unused) unused = &record;
        if (record.lastTick < oldest->lastTick) oldest = &record;
    }
    auto& record = unused ? *unused : *oldest;
    record = {posture, 0.0, now};
    return record;
}

void NormalizedPostureAnimeUpdate(void* posture, float delta) {
    InterlockedIncrement(&g.postureCalls);
    auto original = g.originalPostureAnimeUpdate;
    if (!original) return;
    int steps = 1;
    if (UsesDynamicProjectileCadence(IsThirtyFpsProfile(), delta)) {
        const ULONGLONG now = GetTickCount64();
        AcquireSRWLockExclusive(&cadenceLock);
        auto& record = FindCadenceRecord(posture, now);
        if (now - record.lastTick > 500) record.accumulator = 0.0;
        record.lastTick = now;
        steps = AdvanceProjectileCadence(record.accumulator, delta, false);
        ReleaseSRWLockExclusive(&cadenceLock);
    }
    if (!steps) {
        InterlockedIncrement(&g.postureSkipped);
        return;
    }
    InterlockedAdd(&g.postureSteps, steps);
    for (int step = 0; step < steps; ++step) original(posture, delta);
}

std::string PostureTimingDiagnostics() {
    std::ostringstream out;
    out << ", posture_target_hz=" << kProjectilePostureCadenceHz
        << ", posture_calls=" << g.postureCalls
        << ", posture_steps=" << g.postureSteps
        << ", posture_skipped=" << g.postureSkipped
        << ", control_script_ticks=" << g.controlScriptTicks
        << ", control_script_steps=" << g.controlScriptSteps
        << ", control_script_skipped=" << g.controlScriptSkipped;
    return out.str();
}

PatchStatus InstallPostureTiming(const PeImage& image,
    const CompatibilityPlan& plan, HookState& state, HookResources& resources) {
    if (!EnsureHookResources(image, resources)) return PatchStatus::unavailable;
    auto* relay = resources.code + 896;
    std::array<std::uint8_t, 12> jump{0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};
    const auto wrapper = reinterpret_cast<std::uintptr_t>(
        &NormalizedPostureAnimeUpdate);
    std::memcpy(jump.data() + 2, &wrapper, sizeof(wrapper));
    if (!WriteExecutable(relay, jump) || !IsReachable(plan.postureCall + 5, relay)) {
        Log("Could not create the Shot posture timing relay.");
        return PatchStatus::unavailable;
    }
    state.patch = {};
    state.patch.address = plan.postureCall;
    state.patch.size = 5;
    std::memcpy(state.patch.original.data(), plan.postureCall, 5);
    state.patch.applied[0] = 0xE8;
    const auto displacement = static_cast<std::int32_t>(
        reinterpret_cast<std::intptr_t>(relay) -
        reinterpret_cast<std::intptr_t>(plan.postureCall + 5));
    std::memcpy(state.patch.applied.data() + 1, &displacement, sizeof(displacement));
    g.originalPostureAnimeUpdate = plan.postureTarget;
    if (!PatchCode(plan.postureCall, {state.patch.original.data(), 5},
                   {state.patch.applied.data(), 5}, "Shot posture timing")) {
        g.originalPostureAnimeUpdate = nullptr;
        return PatchStatus::unavailable;
    }
    Log("Set the Shot posture-animation cadence to the stock 30 Hz cadence.");
    return PatchStatus::installed;
}
} // namespace nioh1fix::runtime
