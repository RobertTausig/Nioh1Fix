#include "signatures.hpp"

#include <cstring>
#include <iomanip>
#include <sstream>

namespace nioh1fix::runtime {
static bool ApplyRecord(std::uint8_t* address,
                        std::span<const std::uint8_t> expected,
                        std::span<const std::uint8_t> applied,
                        const char* name, PatchRecord& record) {
    if (expected.size() > record.applied.size()) return false;
    record = {}; record.address = address; record.size = expected.size();
    std::memcpy(record.original.data(), expected.data(), expected.size());
    std::memcpy(record.applied.data(), applied.data(), applied.size());
    if (PatchCode(address, expected, applied, name)) return true;
    record = {}; return false;
}
static bool PatchRelative(std::uint8_t* address, const void* destination,
                          const char* name, PatchRecord& record) {
    if (address[0] != 0xE8 || !IsReachable(address + 5, destination)) return false;
    std::array<std::uint8_t, 5> replacement{0xE8,0,0,0,0};
    const auto relative = static_cast<std::int32_t>(
        reinterpret_cast<std::intptr_t>(destination) -
        reinterpret_cast<std::intptr_t>(address + 5));
    std::memcpy(replacement.data() + 1, &relative, sizeof(relative));
    return ApplyRecord(address, {address, 5}, replacement, name, record);
}
PatchStatus InstallMotionHooks(const PeImage& image, PatchSet& patches) {
    const std::array<SearchResult, 3> found{
        FindCode(image, kMotionSlots), FindCode(image, kLinkedMotion),
        FindCode(image, kMotionComponent)};
    for (const auto& match : found) if (match.count > 1) {
        Log("An animation timing signature was ambiguous; normalization was not installed.");
        return PatchStatus::unavailable;
    }
    for (const auto& match : found) if (!match.count) return PatchStatus::pending;
    if (!EnsureHookResources(image, patches.resources)) return PatchStatus::unavailable;
    const std::array<std::size_t, 3> offsets{21,32,14};
    for (std::size_t i = 0; i < 3; ++i) {
        auto* call = found[i].address + offsets[i];
        if (call[0] != 0xE8 || !IsReachable(call + 5, patches.resources.code)) {
            Log("An animation call did not match the verified call boundary.");
            return PatchStatus::unavailable;
        }
    }
    for (std::size_t i = 0; i < 3; ++i) {
        auto* call = found[i].address + offsets[i];
        if (PatchRelative(call, patches.resources.code, "an animation-delta call",
                          patches.motion[i])) continue;
        for (std::size_t done = 0; done < i; ++done)
            PatchCode(patches.motion[done].address,
                      {patches.motion[done].applied.data(), 5},
                      {patches.motion[done].original.data(), 5},
                      "an animation-delta rollback");
        return PatchStatus::unavailable;
    }
    Log("Normalized three verified motion-component delta paths to the presentation cadence.");
    return PatchStatus::installed;
}
PatchStatus InstallInputHook(const PeImage& image, PatchSet& patches) {
    const auto found = FindCode(image, kInput);
    if (found.count > 1) {
        Log("The input-update signature was ambiguous; 60 Hz gating was not installed.");
        return PatchStatus::unavailable;
    }
    if (!found.count) return PatchStatus::pending;
    if (!EnsureHookResources(image, patches.resources)) return PatchStatus::unavailable;
    auto* call = found.address + 16;
    if (call[0] != 0xE8) { Log("The verified input-update call boundary changed."); return PatchStatus::unavailable; }
    std::int32_t displacement{}; std::memcpy(&displacement, call + 1, 4);
    g.originalInputUpdate = reinterpret_cast<InputUpdateFunction>(call + 5 + displacement);
    if (!PatchRelative(call, patches.resources.code + 16,
                       "the 60 Hz input-cadence gate", patches.input)) {
        g.originalInputUpdate = nullptr; return PatchStatus::unavailable;
    }
    Log("Gated transient and repeat input state to the original 60 Hz cadence.");
    return PatchStatus::installed;
}
static bool InstallAbsolute(const PeImage& image, std::span<const std::uint8_t> signature,
                            const void* destination, const char* name,
                            PatchRecord& patch) {
    const auto found = FindCode(image, signature);
    if (found.count > 1) { Log(std::string(name) + " signature was ambiguous."); return false; }
    if (!found.count) return true;
    std::array<std::uint8_t, 96> bytes{};
    std::fill_n(bytes.data(), signature.size(), 0x90);
    bytes[0] = 0x48; bytes[1] = 0xB8;
    const auto target = reinterpret_cast<std::uintptr_t>(destination);
    std::memcpy(bytes.data() + 2, &target, sizeof(target));
    bytes[10] = 0xFF; bytes[11] = 0xE0;
    return ApplyRecord(found.address, signature, {bytes.data(), signature.size()}, name, patch);
}
bool InstallGameplayHook(const PeImage& image, PatchRecord& patch) {
    if (!InstallAbsolute(image, kGameplay, reinterpret_cast<const void*>(
            &GetGameplayReferenceFps), "the gameplay FPS accessor", patch)) return false;
    if (patch.address) {
        std::ostringstream out; out << "Separated gameplay timing from the render target at RVA=0x"
            << std::hex << std::uppercase << std::size_t(patch.address - image.base) << '.';
        Log(out.str());
    }
    return true;
}

bool InstallLimiter(const PeImage& image, PatchRecord& patch) {
    const auto found = FindCode(image, kLimiter);
    if (found.count > 1) { Log("The main frame-limiter signature was ambiguous."); return false; }
    if (!found.count) return true;
    if (!ApplyRecord(found.address, {kLimiter.data(), kLimiterPatch.size()}, kLimiterPatch,
                     "the main post-Present frame limiter", patch)) return false;
    std::ostringstream out; out << "Disabled the main post-Present frame limiter at RVA=0x"
        << std::hex << std::uppercase << std::size_t(patch.address - image.base) << '.';
    Log(out.str()); return true;
}

bool InstallPresentHook(const PeImage& image, PatchRecord& patch, DWORD elapsed) {
    const auto found = FindCode(image, kPresent);
    if (found.count > 1) { Log("Present signature was ambiguous; no patch was applied."); return false; }
    if (!found.count) return true;
    std::array<std::uint8_t, 34> stub{0x48,0x89,0xF9,0x48,0x89,0xEA,0x48,0xB8,
        0,0,0,0,0,0,0,0,0xFF,0xD0,0x48,0x8B,0x4F,0x30,0x48,0xB8,
        0,0,0,0,0,0,0,0,0xFF,0xE0};
    const auto helper = reinterpret_cast<std::uintptr_t>(&AggressivePresent);
    const auto continuation = reinterpret_cast<std::uintptr_t>(found.address + kPresent.size());
    std::memcpy(stub.data() + 8, &helper, 8); std::memcpy(stub.data() + 24, &continuation, 8);
    auto* code = static_cast<std::uint8_t*>(VirtualAlloc(
        nullptr, stub.size(), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!code) { Log("VirtualAlloc failed while creating the Present stub."); return false; }
    if (!WriteExecutable(code, stub)) {
        VirtualFree(code, 0, MEM_RELEASE); return false;
    }
    if (!InstallAbsolute(image, kPresent, code, "the Present dispatch", patch))
        return false;
    std::ostringstream out; out << "Forced non-blocking Present at RVA=0x" << std::hex
        << std::uppercase << std::size_t(patch.address - image.base) << " after "
        << std::dec << elapsed << " ms."; Log(out.str()); return true;
}

bool ApplyFrameProfiles(std::uint8_t* table) {
    constexpr SIZE_T span = kGameplayFpsOffsets.back() + sizeof(float);
    DWORD protection{}; if (!VirtualProtect(table, span, PAGE_READWRITE, &protection)) { Log("VirtualProtect failed before writing the frame profiles."); return false; }
    const bool applied = PatchGameplayProfiles({table, kFrameProfileSignature.size()},
                                               float(kInternalTargetFps));
    DWORD ignored{};
    const bool restored = VirtualProtect(table, span, protection, &ignored) != FALSE;
    if (!applied) Log("Frame profile verification failed; no values were changed.");
    if (!restored) Log("The frame-profile page protection could not be restored.");
    return applied && restored;
}
} // namespace nioh1fix::runtime
