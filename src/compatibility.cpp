#include "signatures.hpp"

namespace nioh1fix::runtime {
ResolveStatus ResolveCompatibility(const PeImage& image, CompatibilityPlan& plan) {
    std::array<SearchResult, 7> coreMatches{
        FindCode(image, kGameplay), FindCode(image, kLimiter),
        FindCode(image, kPresent), FindCode(image, kMotionSlots),
        FindCode(image, kLinkedMotion), FindCode(image, kMotionComponent),
        FindCode(image, kInput)};
    std::array<SearchResult, kHooks.size()> hookMatches{};
    for (std::size_t i = 0; i < kHooks.size(); ++i)
        hookMatches[i] = FindCode(image, kHooks[i].signature);
    const SearchResult textScrollMatch = FindCode(image, kTextScroll);
    const std::array<SearchResult, 2> matrixCopyMatches{
        FindCode(image, kModelMatrixCopy), FindCode(image, kClothMatrixCopy)};

    for (const auto& match : coreMatches) if (match.count > 1) {
        Log("A required signature was ambiguous; no changes were made.");
        return ResolveStatus::incompatible;
    }
    for (const auto& match : hookMatches) if (match.count > 1) {
        Log("A required signature was ambiguous; no changes were made.");
        return ResolveStatus::incompatible;
    }
    if (textScrollMatch.count > 1) {
        Log("The text-scroll signature was ambiguous; no changes were made.");
        return ResolveStatus::incompatible;
    }
    for (const auto& match : matrixCopyMatches) if (match.count > 1) {
        Log("A model-matrix signature was ambiguous; no changes were made.");
        return ResolveStatus::incompatible;
    }
    for (const auto& match : coreMatches)
        if (!match.count) return ResolveStatus::pending;
    for (const auto& match : hookMatches)
        if (!match.count) return ResolveStatus::pending;
    if (!textScrollMatch.count) return ResolveStatus::pending;
    for (const auto& match : matrixCopyMatches)
        if (!match.count) return ResolveStatus::pending;

    auto* active = DecodeRelative(coreMatches[0].address, 3, 7);
    auto* table = DecodeRelative(coreMatches[0].address + 11, 3, 7);
    const DWORD dataFlags = IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ;
    if (!IsImageRange(image, active, sizeof(LONG), dataFlags) ||
        !IsImageRange(image, table, kFrameProfileSignature.size(), dataFlags) ||
        *reinterpret_cast<volatile LONG*>(active) < 0 ||
        *reinterpret_cast<volatile LONG*>(active) >= 4 ||
        InspectGameplayProfiles({table, kFrameProfileSignature.size()},
                                float(kInternalTargetFps)) != ProfileState::original) {
        Log("Derived gameplay globals did not validate; no changes were made.");
        return ResolveStatus::incompatible;
    }

    constexpr std::array<std::size_t, 3> motionOffsets{21, 32, 14};
    std::array<std::uint8_t*, 3> motionTargets{};
    for (std::size_t i = 0; i < motionOffsets.size(); ++i) {
        plan.motionCalls[i] = coreMatches[3 + i].address + motionOffsets[i];
        motionTargets[i] = DecodeRelative(plan.motionCalls[i], 1, 5);
    }
    if (motionTargets[0] != motionTargets[1] ||
        motionTargets[0] != motionTargets[2] ||
        !IsImageRange(image, motionTargets[0], 1, IMAGE_SCN_MEM_EXECUTE)) {
        Log("Animation-delta call targets did not validate; no changes were made.");
        return ResolveStatus::incompatible;
    }

    plan.inputCall = coreMatches[6].address + 16;
    auto* inputTarget = DecodeRelative(plan.inputCall, 1, 5);
    if (!ValidateInputTarget(image, inputTarget)) {
        Log("The input-update layout did not validate; no changes were made.");
        return ResolveStatus::incompatible;
    }
    for (std::size_t i = 0; i < kHooks.size(); ++i) {
        plan.hookBlocks[i] = hookMatches[i].address + kHooks[i].blockOffset;
        if (!IsImageRange(image, plan.hookBlocks[i], kHooks[i].blockSize,
                          IMAGE_SCN_MEM_EXECUTE)) {
            Log("A timing-hook block was outside executable code; no changes were made.");
            return ResolveStatus::incompatible;
        }
    }
    if (!IsImageRange(image, textScrollMatch.address, kTextScrollOverwriteSize,
                      IMAGE_SCN_MEM_EXECUTE)) {
        Log("The text-scroll hook was outside executable code; no changes were made.");
        return ResolveStatus::incompatible;
    }
    std::array<std::uint8_t*, 2> matrixCopyTargets{};
    for (std::size_t i = 0; i < matrixCopyMatches.size(); ++i) {
        plan.matrixCopyCalls[i] = matrixCopyMatches[i].address +
                                  kMatrixCopyCallOffset;
        matrixCopyTargets[i] = DecodeRelative(plan.matrixCopyCalls[i], 1, 5);
    }
    if (matrixCopyTargets[0] != matrixCopyTargets[1] ||
        !IsImageRange(image, matrixCopyTargets[0], 1, IMAGE_SCN_MEM_EXECUTE)) {
        Log("Model-matrix copy targets did not validate; no changes were made.");
        return ResolveStatus::incompatible;
    }

    plan.gameplay = coreMatches[0].address;
    plan.limiter = coreMatches[1].address;
    plan.present = coreMatches[2].address;
    plan.table = table;
    plan.textScroll = textScrollMatch.address;
    plan.activeProfile = reinterpret_cast<volatile LONG*>(active);
    plan.inputTarget = reinterpret_cast<InputUpdateFunction>(inputTarget);
    plan.memoryCopyTarget = reinterpret_cast<MemoryCopyFunction>(matrixCopyTargets[0]);
    plan.knownBuild = image.headers->FileHeader.TimeDateStamp == kSupportedTimestamp &&
        image.headers->OptionalHeader.SizeOfImage == kSupportedImageSize;
    return ResolveStatus::compatible;
}
} // namespace nioh1fix::runtime
