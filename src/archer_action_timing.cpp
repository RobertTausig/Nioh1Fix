#include "archer_diagnostic.hpp"
#include "signatures.hpp"

#include <cstring>

namespace nioh1fix::runtime {
namespace {
void TrackedArcherActionUpdate(void* action) {
    CaptureActionEventUpdate(action);
    if (!g.originalArcherActionUpdate) return;
    g.originalArcherActionUpdate(action);
}

template <std::size_t Size>
bool AppendRelativeJump(std::array<std::uint8_t, Size>& bytes,
    std::size_t offset, std::uint8_t* source, const void* target) {
    if (!IsReachable(source + offset + 5, target)) return false;
    bytes[offset] = 0xE9;
    const auto displacement = static_cast<std::int32_t>(
        reinterpret_cast<std::intptr_t>(target) -
        reinterpret_cast<std::intptr_t>(source + offset + 5));
    std::memcpy(bytes.data() + offset + 1, &displacement, sizeof(displacement));
    return true;
}
} // namespace

PatchStatus InstallArcherActionTiming(const PeImage& image,
    const CompatibilityPlan& plan, PatchSet& patches) {
    auto& state = patches.actionEventDiagnostic;
    auto* block = plan.actionEventUpdateBlock;
    constexpr std::size_t kOriginalOffset = 2560;
    constexpr std::size_t kDispatchOffset = 2688;
    if (!block || !EnsureHookResources(image, patches.resources) ||
        !IsImageRange(image, block, kActionEventUpdateDiagnostic.blockSize,
                      IMAGE_SCN_MEM_EXECUTE)) return PatchStatus::unavailable;

    auto* originalStub = patches.resources.code + kOriginalOffset;
    std::array<std::uint8_t, 32> original{};
    std::memcpy(original.data(), block, kActionEventUpdateDiagnostic.blockSize);
    if (!AppendRelativeJump(original, kActionEventUpdateDiagnostic.blockSize,
            originalStub, block + kActionEventUpdateDiagnostic.blockSize) ||
        !WriteExecutable(originalStub, original)) return PatchStatus::unavailable;
    g.originalArcherActionUpdate = reinterpret_cast<ArcherActionUpdateFunction>(
        originalStub);

    auto* dispatch = patches.resources.code + kDispatchOffset;
    std::array<std::uint8_t, 12> dispatchBytes{0x48,0xB8};
    const auto wrapper = reinterpret_cast<std::uintptr_t>(
        &TrackedArcherActionUpdate);
    std::memcpy(dispatchBytes.data() + 2, &wrapper, sizeof(wrapper));
    dispatchBytes[10] = 0xFF;
    dispatchBytes[11] = 0xE0;
    if (!WriteExecutable(dispatch, dispatchBytes)) return PatchStatus::unavailable;

    state.patch = {};
    state.patch.address = block;
    state.patch.size = kActionEventUpdateDiagnostic.blockSize;
    std::memcpy(state.patch.original.data(), block, state.patch.size);
    std::fill_n(state.patch.applied.data(), state.patch.size, 0x90);
    if (!AppendRelativeJump(state.patch.applied, 0, block, dispatch) ||
        !PatchCode(block, {state.patch.original.data(), state.patch.size},
            {state.patch.applied.data(), state.patch.size},
            "the tracked-archer action cadence")) return PatchStatus::unavailable;
    Log("Enabled tracked-archer action-event correlation without suppressing updates.");
    return PatchStatus::installed;
}
} // namespace nioh1fix::runtime
