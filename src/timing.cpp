#include "runtime.hpp"
#include "matrix_diagnostics.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <sstream>

namespace nioh1fix::runtime {
State g{};
static LONG FloatBits(float value) { return std::bit_cast<LONG>(value); }

float ReadTimingScale() {
    return std::bit_cast<float>(InterlockedCompareExchange(&g.timingScaleBits, 0, 0));
}
LONG Counter(std::size_t index) {
    return g.timingData ? InterlockedCompareExchange(g.timingData + index, 0, 0) : 0;
}
bool IsThirtyFpsProfile() {
    if (!g.activeProfile) return false;
    const LONG profile = *g.activeProfile;
    return profile == 1 || profile == 2;
}
static void PublishTimingScale(double scale) {
    const LONG bits = FloatBits(IsThirtyFpsProfile() ? 1.0F : float(scale));
    InterlockedExchange(&g.timingScaleBits, bits);
    if (g.timingData) InterlockedExchange(g.timingData, bits);
}
float GetNormalizedMotionDelta() {
    InterlockedIncrement(&g.motionCalls);
    RecordMotionCaller(NIOH1FIX_RETURN_ADDRESS());
    return IsThirtyFpsProfile() ? 1.0F / 30.0F : ReadTimingScale() / 120.0F;
}

static void ClearTransientInput(void* manager) {
    auto* bytes = static_cast<std::uint8_t*>(manager);
    for (std::size_t player = 0; player < 4; ++player) {
        auto* block = bytes + player * 0x1C0;
        for (std::size_t offset : {0x30U, 0x34U, 0x38U})
            *reinterpret_cast<std::uint32_t*>(block + offset) = 0;
    }
}
static bool AcceptInputSample() {
    if (IsThirtyFpsProfile() || g.frequency.QuadPart <= 0) {
        g.previousInputTick = 0; g.inputAccumulator = 0.0; return true;
    }
    LARGE_INTEGER now{};
    if (!QueryPerformanceCounter(&now)) return true;
    if (g.previousInputTick <= 0 || now.QuadPart <= g.previousInputTick) {
        g.previousInputTick = now.QuadPart; g.inputAccumulator = 0.0; return true;
    }
    const LONG64 elapsed = now.QuadPart - g.previousInputTick;
    g.previousInputTick = now.QuadPart;
    if (double(elapsed) > double(g.frequency.QuadPart) * 0.1) {
        g.inputAccumulator = 0.0; return true;
    }
    const double cadence = double(g.frequency.QuadPart) / 60.0;
    g.inputAccumulator += double(elapsed);
    if (g.inputAccumulator < cadence) return false;
    g.inputAccumulator = std::fmod(g.inputAccumulator, cadence);
    return true;
}
void NormalizedInputUpdate(void* manager) {
    InterlockedIncrement(&g.inputUpdates);
    if (!g.originalInputUpdate || AcceptInputSample()) {
        InterlockedIncrement(&g.inputAccepted);
        if (g.originalInputUpdate) g.originalInputUpdate(manager);
    } else {
        InterlockedIncrement(&g.inputSkipped); ClearTransientInput(manager);
    }
}

bool NormalizedTextScrollUpdate(void* controller) {
    if (g.timingData) InterlockedIncrement(g.timingData + 8);
    if (!g.originalTextScrollUpdate) return false;
    auto* speed = reinterpret_cast<float*>(
        static_cast<std::uint8_t*>(controller) + 0x10);
    const float original = *speed;
    *speed = original * ReadTimingScale();
    const bool result = g.originalTextScrollUpdate(controller);
    *speed = original;
    return result;
}

HRESULT AggressivePresent(void* renderer, const std::uint8_t* config) {
    auto* chain = *reinterpret_cast<void**>(static_cast<std::uint8_t*>(renderer) + 0x2FB0);
    UINT flags = 0x8;
    if (*config) {
        if (auto* alternate = *reinterpret_cast<void* const*>(config + sizeof(void*)))
            chain = alternate;
        else flags = 0x1;
    }
    using Present = HRESULT(STDMETHODCALLTYPE*)(void*, UINT, UINT);
    const auto present = reinterpret_cast<Present>((*reinterpret_cast<void***>(chain))[8]);
    LARGE_INTEGER start{}, end{}; QueryPerformanceCounter(&start);
    const LONG64 previous = InterlockedExchange64(&g.previousPresentTick, start.QuadPart);
    if (previous > 0 && start.QuadPart > previous) {
        const LONG64 ticks = start.QuadPart - previous;
        InterlockedExchange64(&g.lastPresentInterval, ticks);
        if (g.frequency.QuadPart > 0)
            PublishTimingScale(UpdateTimingScale(g.timing,
                double(ticks) / double(g.frequency.QuadPart), kInternalTargetFps));
    }
    const HRESULT result = present(chain, 0, flags); QueryPerformanceCounter(&end);
    InterlockedIncrement(&g.presentCalls);
    InterlockedAdd64(&g.presentTicks, end.QuadPart - start.QuadPart);
    if (result == static_cast<HRESULT>(0x887A000AUL))
        InterlockedIncrement(&g.presentWouldBlock);
    else if (FAILED(result)) InterlockedIncrement(&g.presentFailures);
    return result;
}

void LogDiagnostics(std::uint8_t* table, DWORD elapsed) {
    const LONG profile = g.activeProfile ? *g.activeProfile : -1;
    std::ostringstream out; out << "Frame diagnostics at " << elapsed
        << " ms: active_profile=" << profile << ", active_target=";
    if (profile >= 0 && profile < 4)
        out << *reinterpret_cast<volatile float*>(table + std::size_t(profile) * 12);
    else out << "unavailable";
    out << ", present_calls=" << g.presentCalls << ", present_would_block="
        << g.presentWouldBlock << ", present_failures=" << g.presentFailures
        << ", gameplay_reference_fps=" << GetGameplayReferenceFps()
        << ", timing_scale=" << ReadTimingScale() << ", animation_delta="
        << (IsThirtyFpsProfile() ? 1.0F / 30.0F : ReadTimingScale() / 120.0F)
        << ", animation_delta_calls=" << g.motionCalls << ", aim_camera_updates="
        << Counter(2) << ", grass_wind_updates=" << Counter(1)
        << ", scl_animation_steps=" << Counter(3) << ", statistical_ocean_updates="
        << Counter(4) << ", cloud_plane_updates=" << Counter(5)
        << ", cloud_circle_updates=" << Counter(6) << ", cloud_particle_updates="
        << Counter(7) << ", text_scroll_updates=" << Counter(8)
        << CollectAnimationDiagnostics()
        << CollectMatrixDiagnostics()
        << ", input_updates=" << g.inputUpdates
        << ", input_accepted=" << g.inputAccepted << ", input_skipped=" << g.inputSkipped;
    const LONG64 ticks = InterlockedCompareExchange64(&g.lastPresentInterval, 0, 0);
    if (ticks > 0 && g.frequency.QuadPart > 0)
        out << ", measured_present_fps=" << double(g.frequency.QuadPart) / double(ticks);
    if (g.presentCalls > 0 && g.frequency.QuadPart > 0)
        out << ", average_present_us=" << static_cast<long long>(
            double(g.presentTicks) * 1'000'000.0 / double(g.frequency.QuadPart) /
            double(g.presentCalls));
    out << '.'; Log(out.str());
}
} // namespace nioh1fix::runtime
