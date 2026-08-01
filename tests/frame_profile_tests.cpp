#include "core.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <span>
#include <vector>

namespace
{
bool Check(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
    }
    return condition;
}

float ReadFloat(std::span<const std::uint8_t> bytes, std::size_t offset)
{
    float value{};
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}
} // namespace

int main()
{
    bool ok = true;

    std::vector<std::uint8_t> image(256, 0xCC);
    constexpr std::size_t tableOffset = 73;
    std::memcpy(image.data() + tableOffset,
                nioh1fix::kFrameProfileSignature.data(),
                nioh1fix::kFrameProfileSignature.size());

    const auto match = nioh1fix::FindFrameProfileTable(image);
    ok &= Check(match.status == nioh1fix::MatchStatus::unique,
                "expected one frame profile match");
    ok &= Check(match.offset == tableOffset, "reported the wrong match offset");

    auto table = std::span<std::uint8_t>(
        image.data() + tableOffset, nioh1fix::kFrameProfileSignature.size());
    ok &= Check(nioh1fix::InspectGameplayProfiles(table, 120.0F) ==
                    nioh1fix::ProfileState::original,
                "original profile state was not recognized");
    ok &= Check(nioh1fix::PatchGameplayProfiles(table, 120.0F),
                "valid profile patch was rejected");
    ok &= Check(nioh1fix::InspectGameplayProfiles(table, 120.0F) ==
                    nioh1fix::ProfileState::patched,
                "patched profile state was not recognized");
    ok &= Check(ReadFloat(table, 0) == 120.0F, "first gameplay profile was not patched");
    ok &= Check(ReadFloat(table, 12) == 30.0F, "first 30 FPS profile changed");
    ok &= Check(ReadFloat(table, 24) == 30.0F, "second 30 FPS profile changed");
    ok &= Check(ReadFloat(table, 36) == 120.0F, "second gameplay profile was not patched");

    std::vector<std::uint8_t> ambiguous(160, 0);
    std::memcpy(ambiguous.data() + 5,
                nioh1fix::kFrameProfileSignature.data(),
                nioh1fix::kFrameProfileSignature.size());
    std::memcpy(ambiguous.data() + 90,
                nioh1fix::kFrameProfileSignature.data(),
                nioh1fix::kFrameProfileSignature.size());
    const auto duplicate = nioh1fix::FindFrameProfileTable(ambiguous);
    ok &= Check(duplicate.status == nioh1fix::MatchStatus::ambiguous,
                "duplicate signatures did not fail closed");

    std::array<std::uint8_t, nioh1fix::kFrameProfileSignature.size()> corrupt =
        nioh1fix::kFrameProfileSignature;
    corrupt[7] ^= 1;
    ok &= Check(nioh1fix::InspectGameplayProfiles(corrupt, 120.0F) ==
                    nioh1fix::ProfileState::invalid,
                "corrupt table state was accepted");
    ok &= Check(!nioh1fix::PatchGameplayProfiles(corrupt, 120.0F),
                "corrupt table was accepted");
    ok &= Check(!nioh1fix::PatchGameplayProfiles(
                    std::span<std::uint8_t>(image.data(), 8), 120.0F),
                "short table was accepted");

    constexpr std::array<std::uint8_t, 3> patternBytes{0xAA,0x00,0xCC};
    constexpr std::array<std::uint8_t, 3> patternMask{0xFF,0x00,0xFF};
    const nioh1fix::BytePattern pattern{patternBytes, patternMask};
    const std::array<std::uint8_t, 5> maskedImage{0x00,0xAA,0x47,0xCC,0x00};
    const auto masked = nioh1fix::FindPattern(maskedImage, pattern);
    ok &= Check(masked.status == nioh1fix::MatchStatus::unique && masked.offset == 1,
                "masked signature did not match its wildcard byte");
    const std::array<std::uint8_t, 6> maskedDuplicate{0xAA,0x01,0xCC,
                                                     0xAA,0x02,0xCC};
    ok &= Check(nioh1fix::FindPattern(maskedDuplicate, pattern).status ==
                    nioh1fix::MatchStatus::ambiguous,
                "duplicate masked signatures did not fail closed");
    const std::array<std::uint8_t, 3> fixedMismatch{0xAA,0x47,0xCD};
    ok &= Check(!nioh1fix::MatchesPattern(fixedMismatch, pattern),
                "masked signature ignored a fixed byte");

    if (ok) {
        std::cout << "All frame profile tests passed.\n";
        return 0;
    }
    return 1;
}
