#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace nioh1fix
{
inline constexpr std::array<std::uint8_t, 48> kFrameProfileSignature{
    0x00, 0x00, 0x70, 0x42, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xF0, 0x41, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xF0, 0x41, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x42, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
};

inline constexpr std::array<std::size_t, 2> kGameplayFpsOffsets{0, 36};

enum class MatchStatus
{
    not_found,
    unique,
    ambiguous,
};

enum class ProfileState
{
    original,
    patched,
    invalid,
};

struct MatchResult
{
    MatchStatus status{MatchStatus::not_found};
    std::size_t offset{};
    std::size_t count{};
};

inline MatchResult FindFrameProfileTable(std::span<const std::uint8_t> bytes)
{
    MatchResult result{};
    if (bytes.size() < kFrameProfileSignature.size()) {
        return result;
    }

    const auto lastStart = bytes.size() - kFrameProfileSignature.size();
    for (std::size_t offset = 0; offset <= lastStart; ++offset) {
        if (std::memcmp(bytes.data() + offset,
                        kFrameProfileSignature.data(),
                        kFrameProfileSignature.size()) != 0) {
            continue;
        }

        if (result.count == 0) {
            result.offset = offset;
        }
        ++result.count;
    }

    if (result.count == 1) {
        result.status = MatchStatus::unique;
    } else if (result.count > 1) {
        result.status = MatchStatus::ambiguous;
    }
    return result;
}

inline ProfileState InspectGameplayProfiles(std::span<const std::uint8_t> table,
                                            float targetFps)
{
    if (table.size() < kFrameProfileSignature.size()) {
        return ProfileState::invalid;
    }

    std::array<std::uint8_t, kFrameProfileSignature.size()> normalized{};
    std::memcpy(normalized.data(), table.data(), normalized.size());

    float firstFps{};
    float secondFps{};
    std::memcpy(&firstFps, normalized.data() + kGameplayFpsOffsets[0], sizeof(firstFps));
    std::memcpy(&secondFps, normalized.data() + kGameplayFpsOffsets[1], sizeof(secondFps));
    for (const auto offset : kGameplayFpsOffsets) {
        std::memcpy(normalized.data() + offset,
                    kFrameProfileSignature.data() + offset,
                    sizeof(float));
    }

    if (normalized != kFrameProfileSignature) {
        return ProfileState::invalid;
    }
    if (firstFps == 60.0F && secondFps == 60.0F) {
        return ProfileState::original;
    }
    if (firstFps == targetFps && secondFps == targetFps) {
        return ProfileState::patched;
    }
    return ProfileState::invalid;
}

inline bool PatchGameplayProfiles(std::span<std::uint8_t> table, float targetFps)
{
    if (table.size() < kFrameProfileSignature.size() || targetFps < 60.0F) {
        return false;
    }
    if (InspectGameplayProfiles(table, targetFps) != ProfileState::original) {
        return false;
    }

    for (const auto offset : kGameplayFpsOffsets) {
        std::memcpy(table.data() + offset, &targetFps, sizeof(targetFps));
    }
    return true;
}
} // namespace nioh1fix
