#include "input_core.hpp"
#include "signatures.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace nioh1fix::runtime {
namespace {
inline constexpr std::size_t kCounterOffset = 0x80;
inline constexpr std::size_t kPlayerIndexOffset = 0x160;
struct ActionCounterCadence {
    void* module{};
    LONG64 previousTick{};
    double accumulator{};
};
std::array<ActionCounterCadence, 4> actionCounterCadence{};

bool AdvanceHeldCounters(void* module) {
    auto* bytes = static_cast<std::uint8_t*>(module);
    const int player = *reinterpret_cast<int*>(bytes + kPlayerIndexOffset);
    if (IsThirtyFpsProfile() || player < 0 || player >= 4 ||
        g.frequency.QuadPart <= 0) {
        if (player >= 0 && player < 4) actionCounterCadence[player] = {};
        return true;
    }

    auto& state = actionCounterCadence[player];
    LARGE_INTEGER now{};
    if (!QueryPerformanceCounter(&now)) return true;
    if (state.module != module || state.previousTick <= 0 ||
        now.QuadPart <= state.previousTick) {
        state = {module, now.QuadPart, 0.0};
        return true;
    }

    const LONG64 elapsed = now.QuadPart - state.previousTick;
    state.previousTick = now.QuadPart;
    if (double(elapsed) > double(g.frequency.QuadPart) * 0.1) {
        state.accumulator = 0.0;
        return true;
    }

    const double cadence = double(g.frequency.QuadPart) / 60.0;
    state.accumulator += double(elapsed);
    if (state.accumulator < cadence) return false;
    state.accumulator = std::fmod(state.accumulator, cadence);
    return true;
}

void NormalizedActionCounterUpdate(void* module, std::uint64_t current,
                                   std::uint64_t previous) {
    auto* counters = reinterpret_cast<std::uint32_t*>(
        static_cast<std::uint8_t*>(module) + kCounterOffset);
    UpdateGameplayActionCounters(std::span<std::uint32_t,
        kGameplayActionCounterCount>(counters, kGameplayActionCounterCount),
        current, previous, AdvanceHeldCounters(module));
}
} // namespace

PatchStatus InstallActionCounterHook(const PeImage&,
    const CompatibilityPlan& plan, PatchSet& patches) {
    auto& record = patches.actionCounters;
    record = {};
    record.address = plan.actionCounters;
    record.size = kActionCountersOverwriteSize;
    std::memcpy(record.original.data(), record.address, record.size);
    std::fill_n(record.applied.data(), record.size, 0x90);
    record.applied[0] = 0x48;
    record.applied[1] = 0xB8;
    const auto destination = reinterpret_cast<std::uintptr_t>(
        &NormalizedActionCounterUpdate);
    std::memcpy(record.applied.data() + 2, &destination, sizeof(destination));
    record.applied[10] = 0xFF;
    record.applied[11] = 0xE0;
    if (!PatchCode(record.address, {record.original.data(), record.size},
                   {record.applied.data(), record.size},
                   "the gameplay held-action cadence"))
        return PatchStatus::unavailable;
    Log("Normalized gameplay held-action counters to a maximum 60 Hz cadence.");
    return PatchStatus::installed;
}
} // namespace nioh1fix::runtime
