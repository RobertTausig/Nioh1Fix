#include "projectile_core.hpp"
#include "runtime.hpp"

#include <array>
#include <cstring>

namespace nioh1fix::runtime {
struct ControlCadenceRecord {
    void* controlScript{};
    double accumulator{};
    ULONGLONG lastTick{};
};

static SRWLOCK cadenceLock = SRWLOCK_INIT;
static std::array<ControlCadenceRecord, 64> cadenceRecords{};

static ControlCadenceRecord& FindCadenceRecord(
    void* controlScript, ULONGLONG now) {
    ControlCadenceRecord* unused = nullptr;
    ControlCadenceRecord* oldest = cadenceRecords.data();
    for (auto& record : cadenceRecords) {
        if (record.controlScript == controlScript) return record;
        if (!record.controlScript && !unused) unused = &record;
        if (record.lastTick < oldest->lastTick) oldest = &record;
    }
    auto& record = unused ? *unused : *oldest;
    record = {controlScript, 0.0, now};
    return record;
}

bool NormalizedShotControlScriptTick(void* controlScript) {
    InterlockedIncrement(&g.controlScriptTicks);
    auto original = g.originalShotControlScriptTick;
    if (!original) return true;
    int steps = 1;
    if (!IsThirtyFpsProfile()) {
        const ULONGLONG now = GetTickCount64();
        AcquireSRWLockExclusive(&cadenceLock);
        auto& record = FindCadenceRecord(controlScript, now);
        if (now - record.lastTick > 500) record.accumulator = 0.0;
        record.lastTick = now;
        const double progress = 0.5 * static_cast<double>(ReadTimingScale());
        steps = AdvanceProjectileCadence(record.accumulator, progress, false);
        ReleaseSRWLockExclusive(&cadenceLock);
    }
    if (!steps) {
        InterlockedIncrement(&g.controlScriptSkipped);
        return true;
    }
    InterlockedAdd(&g.controlScriptSteps, steps);
    bool alive = true;
    for (int step = 0; step < steps && alive; ++step)
        alive = original(controlScript);
    return alive;
}

PatchStatus InstallShotControlScriptTiming(const PeImage& image,
    const CompatibilityPlan& plan, HookState& state, HookResources& resources) {
    if (!EnsureHookResources(image, resources)) return PatchStatus::unavailable;
    auto* relay = resources.code + 928;
    std::array<std::uint8_t, 12> jump{0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};
    const auto wrapper = reinterpret_cast<std::uintptr_t>(
        &NormalizedShotControlScriptTick);
    std::memcpy(jump.data() + 2, &wrapper, sizeof(wrapper));
    if (!WriteExecutable(relay, jump) ||
        !IsReachable(plan.controlScriptCall + 5, relay)) {
        Log("Could not create the Shot control-script timing relay.");
        return PatchStatus::unavailable;
    }
    state.patch = {};
    state.patch.address = plan.controlScriptCall;
    state.patch.size = 5;
    std::memcpy(state.patch.original.data(), plan.controlScriptCall, 5);
    state.patch.applied[0] = 0xE8;
    const auto displacement = static_cast<std::int32_t>(
        reinterpret_cast<std::intptr_t>(relay) -
        reinterpret_cast<std::intptr_t>(plan.controlScriptCall + 5));
    std::memcpy(state.patch.applied.data() + 1, &displacement,
                sizeof(displacement));
    g.originalShotControlScriptTick = plan.controlScriptTarget;
    if (!PatchCode(plan.controlScriptCall, {state.patch.original.data(), 5},
                   {state.patch.applied.data(), 5},
                   "Shot control-script timing")) {
        g.originalShotControlScriptTick = nullptr;
        return PatchStatus::unavailable;
    }
    Log("Set the Shot lifetime-script tick to the 30 Hz projectile cadence.");
    return PatchStatus::installed;
}
} // namespace nioh1fix::runtime
