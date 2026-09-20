#include "archer_diagnostic.hpp"
#include "signatures.hpp"

#include <array>
#include <cstring>

namespace nioh1fix::runtime {
namespace {
template <std::size_t Size>
bool AppendJump(std::array<std::uint8_t, Size>& bytes, std::size_t offset,
    std::uint8_t* source, const void* target) {
    if (offset + 5 > bytes.size() ||
        !IsReachable(source + offset + 5, target)) return false;
    bytes[offset] = 0xE9;
    const auto displacement = static_cast<std::int32_t>(
        reinterpret_cast<std::intptr_t>(target) -
        reinterpret_cast<std::intptr_t>(source + offset + 5));
    std::memcpy(bytes.data() + offset + 1, &displacement, sizeof(displacement));
    return true;
}
} // namespace

PatchStatus InstallArcherMotionTiming(const PeImage& image,
    const CompatibilityPlan& plan, PatchSet& patches) {
    auto& state = patches.archerMotionTiming;
    auto* block = plan.archerMotionUpdateBlock;
    if (!block || !IsImageRange(image, block,
            kArcherMotionUpdateOverwriteSize, IMAGE_SCN_MEM_EXECUTE) ||
        !EnsureHookResources(image, patches.resources))
        return PatchStatus::unavailable;

    auto* originalStub = patches.resources.code + 2816;
    std::array<std::uint8_t, 16> original{};
    std::memcpy(original.data(), block, kArcherMotionUpdateOverwriteSize);
    if (!AppendJump(original, kArcherMotionUpdateOverwriteSize, originalStub,
            block + kArcherMotionUpdateOverwriteSize) ||
        !WriteExecutable(originalStub, original))
        return PatchStatus::unavailable;
    g.originalArcherMotionUpdate =
        reinterpret_cast<ArcherMotionUpdateFunction>(originalStub);

    auto* dispatch = patches.resources.code + 2944;
    std::array<std::uint8_t, 12> dispatchBytes{0x48, 0xB8};
    const auto wrapper = reinterpret_cast<std::uintptr_t>(
        &RunTrackedArcherMotion);
    std::memcpy(dispatchBytes.data() + 2, &wrapper, sizeof(wrapper));
    dispatchBytes[10] = 0xFF;
    dispatchBytes[11] = 0xE0;
    if (!WriteExecutable(dispatch, dispatchBytes))
        return PatchStatus::unavailable;

    state.patch = {};
    state.patch.address = block;
    state.patch.size = kArcherMotionUpdateOverwriteSize;
    std::memcpy(state.patch.original.data(), block, state.patch.size);
    std::fill_n(state.patch.applied.data(), state.patch.size, 0x90);
    if (!AppendJump(state.patch.applied, 0, block, dispatch) ||
        !PatchCode(block, {state.patch.original.data(), state.patch.size},
            {state.patch.applied.data(), state.patch.size},
            "the archer held-motion cadence"))
        return PatchStatus::unavailable;
    Log("Normalized the tracked archer holding animation to a 60 Hz cadence.");
    return PatchStatus::installed;
}
} // namespace nioh1fix::runtime
