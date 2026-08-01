#include "signatures.hpp"

#include <cstring>
#include <iomanip>
#include <sstream>

namespace nioh1fix::runtime {
static void Prepare(PatchRecord& record, std::uint8_t* address,
                    std::span<const std::uint8_t> applied) {
    record = {}; record.address = address; record.size = applied.size();
    std::memcpy(record.original.data(), address, applied.size());
    std::memcpy(record.applied.data(), applied.data(), applied.size());
}
static bool Commit(PatchRecord& record, const char* name) {
    return PatchCode(record.address, {record.original.data(), record.size},
                     {record.applied.data(), record.size}, name);
}
static void Rollback(std::span<PatchRecord*> records) {
    for (auto it = records.rbegin(); it != records.rend(); ++it) if (IsApplied(**it))
        PatchCode((*it)->address, {(*it)->applied.data(), (*it)->size},
                  {(*it)->original.data(), (*it)->size}, "a core rollback");
}
static bool PatchRelative(std::uint8_t* call, const void* destination,
                          const char* name, PatchRecord& record) {
    if (call[0] != 0xE8 || !IsReachable(call + 5, destination)) return false;
    std::array<std::uint8_t, 5> bytes{0xE8,0,0,0,0};
    const auto relative = std::int32_t(reinterpret_cast<std::intptr_t>(destination) -
                                       reinterpret_cast<std::intptr_t>(call + 5));
    std::memcpy(bytes.data() + 1, &relative, 4); Prepare(record, call, bytes);
    return Commit(record, name);
}

PatchStatus InstallMotionHooks(const PeImage& image,
                               const CompatibilityPlan& plan, PatchSet& patches) {
    if (!EnsureHookResources(image, patches.resources)) return PatchStatus::unavailable;
    for (std::size_t i = 0; i < plan.motionCalls.size(); ++i)
        if (!PatchRelative(plan.motionCalls[i], patches.resources.code,
                           "an animation-delta call", patches.motion[i])) {
            std::array<PatchRecord*, 3> records{
                &patches.motion[0], &patches.motion[1], &patches.motion[2]};
            Rollback({records.data(), i + 1}); return PatchStatus::unavailable;
        }
    Log("Normalized three verified motion-component delta paths to the presentation cadence.");
    return PatchStatus::installed;
}

bool ValidateInputTarget(const PeImage& image, std::uint8_t* target) {
    constexpr std::array<std::uint8_t, 4> base{0x4D,0x8D,0x66,0x34};
    constexpr std::array<std::uint8_t, 5> pressed{0x41,0x89,0x54,0x24,0xFC};
    constexpr std::array<std::uint8_t, 4> repeat{0x41,0x89,0x04,0x24};
    constexpr std::array<std::uint8_t, 5> released{0x41,0x89,0x54,0x24,0x04};
    constexpr std::array<std::uint8_t, 7> stride{0x49,0x81,0xC4,0xC0,0x01,0x00,0x00};
    if (!IsImageRange(image, target, 0x567, IMAGE_SCN_MEM_EXECUTE)) return false;
    return !std::memcmp(target + 0x1FA, base.data(), base.size()) &&
        !std::memcmp(target + 0x4F4, pressed.data(), pressed.size()) &&
        !std::memcmp(target + 0x502, repeat.data(), repeat.size()) &&
        !std::memcmp(target + 0x50E, released.data(), released.size()) &&
        !std::memcmp(target + 0x560, stride.data(), stride.size());
}
PatchStatus InstallInputHook(const PeImage& image,
                             const CompatibilityPlan& plan, PatchSet& patches) {
    if (!EnsureHookResources(image, patches.resources)) return PatchStatus::unavailable;
    g.originalInputUpdate = plan.inputTarget;
    if (!PatchRelative(plan.inputCall, patches.resources.code + 16,
                       "the 60 Hz input-cadence gate", patches.input)) {
        g.originalInputUpdate = nullptr; return PatchStatus::unavailable;
    }
    Log("Gated transient and repeat input state to the original 60 Hz cadence.");
    return PatchStatus::installed;
}

PatchStatus InstallTextScrollHook(const PeImage& image,
    const CompatibilityPlan& plan, PatchSet& patches) {
    if (!EnsureHookResources(image, patches.resources))
        return PatchStatus::unavailable;
    constexpr std::size_t copied = 8;
    auto* trampoline = patches.resources.code + 576;
    std::array<std::uint8_t, copied + 12> bytes{};
    std::memcpy(bytes.data(), plan.textScroll, copied);
    bytes[copied] = 0x48; bytes[copied + 1] = 0xB8;
    const auto continuation = reinterpret_cast<std::uintptr_t>(
        plan.textScroll + copied);
    std::memcpy(bytes.data() + copied + 2, &continuation, 8);
    bytes[copied + 10] = 0xFF; bytes[copied + 11] = 0xE0;
    if (!WriteExecutable(trampoline, bytes) ||
        !IsReachable(plan.textScroll + 5, patches.resources.code + 32))
        return PatchStatus::unavailable;
    std::array<std::uint8_t, copied> jump{0xE9,0,0,0,0,0x90,0x90,0x90};
    const auto relative = std::int32_t(reinterpret_cast<std::intptr_t>(
        patches.resources.code + 32) - reinterpret_cast<std::intptr_t>(
        plan.textScroll + 5));
    std::memcpy(jump.data() + 1, &relative, 4);
    Prepare(patches.textScroll, plan.textScroll, jump);
    g.originalTextScrollUpdate = reinterpret_cast<TextScrollUpdateFunction>(trampoline);
    if (!Commit(patches.textScroll, "the overflow-text scroll update")) {
        g.originalTextScrollUpdate = nullptr; return PatchStatus::unavailable;
    }
    Log("Normalized overflow-text scrolling to the presentation interval.");
    return PatchStatus::installed;
}

static void PrepareAbsolute(PatchRecord& record, std::uint8_t* address,
                            std::size_t size, const void* destination) {
    std::array<std::uint8_t, 96> bytes{}; std::fill_n(bytes.data(), size, 0x90);
    bytes[0] = 0x48; bytes[1] = 0xB8;
    const auto target = reinterpret_cast<std::uintptr_t>(destination);
    std::memcpy(bytes.data() + 2, &target, sizeof(target));
    bytes[10] = 0xFF; bytes[11] = 0xE0;
    Prepare(record, address, {bytes.data(), size});
}
bool InstallCore(const PeImage& image, const CompatibilityPlan& plan,
                 PatchSet& patches, DWORD elapsed) {
    std::array<std::uint8_t, 34> stub{0x48,0x89,0xF9,0x48,0x89,0xEA,0x48,0xB8,
        0,0,0,0,0,0,0,0,0xFF,0xD0,0x48,0x8B,0x4F,0x30,0x48,0xB8,
        0,0,0,0,0,0,0,0,0xFF,0xE0};
    const auto helper = reinterpret_cast<std::uintptr_t>(&AggressivePresent);
    const auto continuation = reinterpret_cast<std::uintptr_t>(plan.present + kPresent.size());
    std::memcpy(stub.data() + 8, &helper, 8); std::memcpy(stub.data() + 24, &continuation, 8);
    auto* code = static_cast<std::uint8_t*>(VirtualAlloc(
        nullptr, stub.size(), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!code) return false;
    if (!WriteExecutable(code, stub)) {
        VirtualFree(code, 0, MEM_RELEASE); return false;
    }
    PrepareAbsolute(patches.gameplay, plan.gameplay, kGameplay.size(),
                    reinterpret_cast<const void*>(&GetGameplayReferenceFps));
    Prepare(patches.limiter, plan.limiter,
            {kLimiterPatch.data(), kLimiterPatch.size()});
    PrepareAbsolute(patches.present, plan.present, kPresent.size(), code);
    std::array<PatchRecord*, 3> records{
        &patches.gameplay, &patches.limiter, &patches.present};
    const std::array<const char*, 3> names{
        "the gameplay FPS accessor", "the main frame limiter", "the Present dispatch"};
    for (std::size_t i = 0; i < records.size(); ++i) if (!Commit(*records[i], names[i])) {
        Rollback({records.data(), i + 1}); return false;
    }
    if (!SetFrameProfiles(plan.table, true)) {
        Rollback(records); SetFrameProfiles(plan.table, false); return false;
    }
    std::ostringstream out; out << "Installed compatible core patches after " << elapsed
        << " ms at RVAs 0x" << std::hex << std::uppercase
        << std::size_t(plan.gameplay - image.base) << ", 0x"
        << std::size_t(plan.limiter - image.base) << ", and 0x"
        << std::size_t(plan.present - image.base) << '.'; Log(out.str());
    return true;
}
} // namespace nioh1fix::runtime
