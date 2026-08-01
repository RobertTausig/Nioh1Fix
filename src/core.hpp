#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace nioh1fix {
inline constexpr std::array<std::uint8_t, 48> kFrameProfileSignature{
    0x00,0x00,0x70,0x42,0x01,0x00,0x00,0x00,0x01,0x00,0x00,0x00,
    0x00,0x00,0xF0,0x41,0x01,0x00,0x00,0x00,0x02,0x00,0x00,0x00,
    0x00,0x00,0xF0,0x41,0x01,0x00,0x00,0x00,0x02,0x00,0x00,0x00,
    0x00,0x00,0x70,0x42,0x01,0x00,0x00,0x00,0x02,0x00,0x00,0x00};
inline constexpr std::array<std::size_t, 2> kGameplayFpsOffsets{0, 36};
enum class MatchStatus { not_found, unique, ambiguous };
enum class ProfileState { original, patched, invalid };
struct MatchResult { MatchStatus status{}; std::size_t offset{}, count{}; };

inline MatchResult FindFrameProfileTable(std::span<const std::uint8_t> bytes) {
    MatchResult result{};
    if (bytes.size() < kFrameProfileSignature.size()) return result;
    for (std::size_t offset = 0;
         offset <= bytes.size() - kFrameProfileSignature.size(); ++offset) {
        if (std::memcmp(bytes.data() + offset, kFrameProfileSignature.data(),
                        kFrameProfileSignature.size()) != 0) continue;
        if (result.count++ == 0) result.offset = offset;
    }
    result.status = result.count == 1 ? MatchStatus::unique :
                    result.count > 1 ? MatchStatus::ambiguous :
                                       MatchStatus::not_found;
    return result;
}

inline ProfileState InspectGameplayProfiles(
    std::span<const std::uint8_t> table, float target) {
    if (table.size() < kFrameProfileSignature.size()) return ProfileState::invalid;
    std::array<std::uint8_t, kFrameProfileSignature.size()> normalized{};
    std::memcpy(normalized.data(), table.data(), normalized.size());
    float first{}, second{};
    std::memcpy(&first, normalized.data(), sizeof(first));
    std::memcpy(&second, normalized.data() + 36, sizeof(second));
    for (auto offset : kGameplayFpsOffsets)
        std::memcpy(normalized.data() + offset,
                    kFrameProfileSignature.data() + offset, sizeof(float));
    if (normalized != kFrameProfileSignature) return ProfileState::invalid;
    if (first == 60.0F && second == 60.0F) return ProfileState::original;
    if (first == target && second == target) return ProfileState::patched;
    return ProfileState::invalid;
}

inline bool PatchGameplayProfiles(std::span<std::uint8_t> table, float target) {
    if (table.size() < kFrameProfileSignature.size() || target < 60.0F ||
        InspectGameplayProfiles(table, target) != ProfileState::original) return false;
    for (auto offset : kGameplayFpsOffsets)
        std::memcpy(table.data() + offset, &target, sizeof(target));
    return true;
}

inline constexpr double kReferenceFps = 60.0;
inline constexpr double kMinimumMeasuredFps = 10.0;
inline constexpr double kMaximumMeasuredFps = 2000.0;
inline constexpr double kMinimumTimingScale = kReferenceFps / kMaximumMeasuredFps;
inline constexpr double kMaximumTimingScale = kReferenceFps / kMinimumMeasuredFps;
inline constexpr double kMaximumPresentationIntervalSeconds =
    1.0 / kMinimumMeasuredFps;
inline constexpr double kTimingSmoothing = 0.25;
struct TimingScaleState { double scale{1.0}; bool initialized{}; };

inline double BoundedFallbackScale(double fps) {
    if (!std::isfinite(fps) || fps <= 0.0) return 1.0;
    return std::clamp(kReferenceFps / fps, kMinimumTimingScale,
                      kMaximumTimingScale);
}

inline double UpdateTimingScale(TimingScaleState& state, double interval,
                                double fallbackFps) {
    if (!std::isfinite(interval) || interval <= 0.0 ||
        interval > kMaximumPresentationIntervalSeconds) {
        if (!state.initialized) state.scale = BoundedFallbackScale(fallbackFps);
        return state.scale;
    }
    const double measured = std::clamp(interval * kReferenceFps,
                                       kMinimumTimingScale, kMaximumTimingScale);
    if (!state.initialized) { state.scale = measured; state.initialized = true; }
    else state.scale += (measured - state.scale) * kTimingSmoothing;
    return state.scale = std::clamp(state.scale, kMinimumTimingScale,
                                    kMaximumTimingScale);
}
} // namespace nioh1fix
