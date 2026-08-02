#include "runtime.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <vector>

namespace nioh1fix::runtime {
static_assert((kAccessorCallerSlots & (kAccessorCallerSlots - 1)) == 0);

void InitializeMotionDiagnostics(const CompatibilityPlan& plan) {
    for (std::size_t i = 0; i < plan.motionCalls.size(); ++i)
        g.motionReturnAddresses[i] = plan.motionCalls[i] + 5;
}

void RecordMotionCaller(const void* returnAddress) {
    if (!g.animationDiagnosticsActive) return;
    for (std::size_t i = 0; i < g.motionReturnAddresses.size(); ++i)
        if (returnAddress == g.motionReturnAddresses[i]) {
            InterlockedIncrement(&g.motionPathCalls[i]);
            return;
        }
}

void RecordAccessorCaller(const void* returnAddress) {
    if (!g.animationDiagnosticsActive) return;
    const auto caller = reinterpret_cast<std::uintptr_t>(returnAddress);
    const auto base = reinterpret_cast<std::uintptr_t>(g.imageBase);
    if (!base || caller < base || caller - base >= g.imageSize) return;
    const LONG encodedRva = static_cast<LONG>(caller - base + 1);
    std::size_t slot = (std::size_t(encodedRva) * 2654435761U) &
                       (kAccessorCallerSlots - 1);
    for (std::size_t probe = 0; probe < kAccessorCallerSlots; ++probe) {
        auto& sample = g.accessorCallers[slot];
        const LONG found = InterlockedCompareExchange(&sample.rva, encodedRva, 0);
        if (found == 0 || found == encodedRva) {
            InterlockedIncrement(&sample.calls);
            return;
        }
        slot = (slot + 1) & (kAccessorCallerSlots - 1);
    }
    InterlockedIncrement(&g.accessorCallerOverflow);
}

float GetGameplayReferenceFps() {
    RecordAccessorCaller(NIOH1FIX_RETURN_ADDRESS());
    if (IsThirtyFpsProfile()) return 30.0F;
    const LONG64 ticks = InterlockedCompareExchange64(&g.lastPresentInterval, 0, 0);
    if (ticks <= 0 || g.frequency.QuadPart <= 0) return kInternalTargetFps * 2.0F;
    return float(std::clamp(double(g.frequency.QuadPart) / double(ticks) * 2.0,
                            30.0, 2000.0));
}

std::string CollectAnimationDiagnostics() {
    struct Activity { LONG rva{}, calls{}; };
    std::vector<Activity> active;
    LONG total{};
    for (auto& sample : g.accessorCallers) {
        const LONG encodedRva = InterlockedCompareExchange(&sample.rva, 0, 0);
        if (!encodedRva) continue;
        const LONG calls = InterlockedCompareExchange(&sample.calls, 0, 0);
        const LONG delta = calls - sample.previousCalls;
        sample.previousCalls = calls;
        if (delta > 0) { active.push_back({encodedRva - 1, delta}); total += delta; }
    }
    std::ranges::sort(active, [](const Activity& left, const Activity& right) {
        return left.calls > right.calls;
    });
    std::ostringstream out;
    out << ", cloth_primary_updates=" << Counter(9)
        << ", cloth_secondary_updates=" << Counter(10)
        << ", motion_path_calls=" << g.motionPathCalls[0] << '/'
        << g.motionPathCalls[1] << '/' << g.motionPathCalls[2]
        << ", fps_accessor_interval_calls=" << total
        << ", fps_accessor_top_rvas=";
    const std::size_t count = std::min<std::size_t>(active.size(), 8);
    if (!count) out << "none";
    for (std::size_t i = 0; i < count; ++i) {
        if (i) out << '/';
        out << "0x" << std::hex << std::uppercase << active[i].rva
            << ':' << std::dec << active[i].calls;
    }
    out << ", fps_accessor_overflow=" << g.accessorCallerOverflow;
    return out.str();
}
} // namespace nioh1fix::runtime
