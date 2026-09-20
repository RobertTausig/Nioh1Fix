#include "archer_diagnostic.hpp"
#include "signatures.hpp"

namespace nioh1fix::runtime {
ResolveStatus ResolveArcherCompatibility(
    const PeImage& image, CompatibilityPlan& plan) {
    const SearchResult childShotMatch = FindCode(image, kChildShotTransform);
    const SearchResult actionUpdateMatch = FindCode(image, kActionEventUpdate);
    const SearchResult constructorMatch = FindCode(image, kShotConstructor);
    const SearchResult motionUpdateMatch = FindCode(image, kArcherMotionUpdate);
    if (childShotMatch.count > 1 || actionUpdateMatch.count > 1 ||
        constructorMatch.count > 1 || motionUpdateMatch.count > 1) {
        Log("An archer-hold diagnostic signature was ambiguous; no changes were made.");
        return ResolveStatus::incompatible;
    }
    if (!childShotMatch.count || !actionUpdateMatch.count ||
        !constructorMatch.count || !motionUpdateMatch.count)
        return ResolveStatus::pending;

    plan.childShotTransformBlock = childShotMatch.address;
    if (!IsImageRange(image, plan.childShotTransformBlock,
            kChildShotReleaseDiagnostic.blockSize, IMAGE_SCN_MEM_EXECUTE)) {
        Log("The archer release-timer block was invalid; no changes were made.");
        return ResolveStatus::incompatible;
    }

    plan.actionEventUpdateBlock = actionUpdateMatch.address;
    if (!IsImageRange(image, plan.actionEventUpdateBlock,
            kActionEventUpdateDiagnostic.blockSize,
            IMAGE_SCN_MEM_EXECUTE)) {
        Log("The action-event diagnostic block was invalid; no changes were made.");
        return ResolveStatus::incompatible;
    }
    plan.shotConstructorBlock = constructorMatch.address;
    if (!IsImageRange(image, plan.shotConstructorBlock,
            kShotConstructorDiagnostic.blockSize, IMAGE_SCN_MEM_EXECUTE)) {
        Log("The Shot-constructor diagnostic block was invalid; no changes were made.");
        return ResolveStatus::incompatible;
    }
    plan.archerMotionUpdateBlock = motionUpdateMatch.address;
    if (!IsImageRange(image, plan.archerMotionUpdateBlock,
            kArcherMotionUpdateOverwriteSize, IMAGE_SCN_MEM_EXECUTE)) {
        Log("The archer motion-node update was invalid; no changes were made.");
        return ResolveStatus::incompatible;
    }
    return ResolveStatus::compatible;
}
} // namespace nioh1fix::runtime
