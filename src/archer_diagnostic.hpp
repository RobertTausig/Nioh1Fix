#pragma once

#include "runtime.hpp"

#include <array>
#include <cstdint>
#include <iosfwd>

namespace nioh1fix::runtime {
struct ArcherActionCandidate {
    std::uint16_t pointerOffset{0xFFFF};
    std::uint16_t offset{};
    std::int16_t streak{};
    LONG value{};
    bool byteValue{};
};
struct ArcherActionPlateau {
    float frame{};
    LONG durationMs{-1};
    LONG ageMs{-1};
};
struct ArcherActionReset {
    LONG ageMs{-1};
    float before{}, after{};
};
using ArcherActionResets = std::array<ArcherActionReset, 6>;
bool ReadableArcherMemory(const void* address, std::size_t size) noexcept;
ResolveStatus ResolveArcherCompatibility(
    const PeImage& image, CompatibilityPlan& plan);
void CaptureArrowRelease(void* childShot) noexcept;
void CaptureActionEventUpdate(void* action) noexcept;
void CaptureShotConstruction(void* shot) noexcept;
void RunTrackedArcherMotion(void* action, void* source);
void FinishArcherHeldPhase(void* action) noexcept;
void* CorrelatedShotAction(void* shot) noexcept;
LONG64 CorrelatedShotConstructionTick(void* shot) noexcept;
bool SampleActionFrameProgress(void* action) noexcept;
ArcherActionPlateau SnapshotActionPlateau(void* action) noexcept;
ArcherActionResets SnapshotActionResets(void* action) noexcept;
void AppendArcherActionProgress(std::ostringstream& out);
void AppendArcherActionIdentity(std::ostringstream& out);
void AppendArcherMotionDiagnostics(std::ostringstream& out);
void SampleTrackedAction(void* action) noexcept;
void SampleNestedAction(void* action) noexcept;
void SampleActionBytes(void* action) noexcept;
void SampleArcherOwner(void* owner) noexcept;
void CaptureArcherOwnerReset(void* owner) noexcept;
void AppendArcherOwnerResetDiagnostics(std::ostringstream& out);
void SnapshotArcherOwnerCandidates(void* owner,
    std::array<ArcherActionCandidate, 8>& candidates) noexcept;
void SnapshotActionCandidates(void* action,
    std::array<ArcherActionCandidate, 8>& candidates) noexcept;
void SnapshotNestedActionCandidates(void* action,
    std::array<ArcherActionCandidate, 8>& candidates) noexcept;
void SnapshotActionByteCandidates(void* action,
    std::array<ArcherActionCandidate, 8>& candidates) noexcept;
void InsertActionCandidate(const ArcherActionCandidate& candidate,
    std::array<ArcherActionCandidate, 8>& candidates) noexcept;
void AppendArcherHoldDiagnostics(std::ostringstream& out);
bool MaintainArcherHoldDiagnostic(const PeImage& image,
    const CompatibilityPlan& plan, PatchSet& patches);
PatchStatus InstallArcherActionTiming(const PeImage& image,
    const CompatibilityPlan& plan, PatchSet& patches);
PatchStatus InstallArcherMotionTiming(const PeImage& image,
    const CompatibilityPlan& plan, PatchSet& patches);
bool MaintainActionEventDiagnostic(const PeImage& image,
    const CompatibilityPlan& plan, PatchSet& patches);
} // namespace nioh1fix::runtime
