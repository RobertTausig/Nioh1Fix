#include "archer_diagnostic.hpp"

#include <array>
#include <bit>
#include <climits>
#include <cstdlib>
#include <sstream>
#include <cstdint>

namespace nioh1fix::runtime {
namespace {
struct Correlation { void* shot{}; void* action{}; LONG64 constructionTick{}; };

thread_local void* currentAction{};
SRWLOCK correlationLock = SRWLOCK_INIT;
std::array<Correlation, 128> correlations{};
std::size_t nextCorrelation{};
} // namespace

void CaptureActionEventUpdate(void* action) noexcept {
    currentAction = action;
    auto* tracked = InterlockedCompareExchangePointer(
        &g.trackedArcherAction, nullptr, nullptr);
    if (action != tracked || !action) return;
    auto** vtable = *reinterpret_cast<void***>(action);
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    InterlockedExchange(&g.trackedActionVtableRva, static_cast<LONG>(
        reinterpret_cast<std::uintptr_t>(vtable) - base));
    InterlockedExchange(&g.trackedFrameGetterRva, static_cast<LONG>(
        reinterpret_cast<std::uintptr_t>(vtable[0x118 / sizeof(void*)]) - base));
    auto* owner = InterlockedCompareExchangePointer(
        &g.trackedArcherOwner, nullptr, nullptr);
    SampleArcherOwner(owner);
    if (SampleActionFrameProgress(action)) CaptureArcherOwnerReset(owner);
    InterlockedIncrement(&g.trackedActionEventCalls);
    InterlockedExchange(&g.trackedActionDeltaBits,
        std::bit_cast<LONG>(*reinterpret_cast<float*>(
            static_cast<std::uint8_t*>(action) + 0x24)));
}

void CaptureShotConstruction(void* shot) noexcept {
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    AcquireSRWLockExclusive(&correlationLock);
    correlations[nextCorrelation] = {shot, currentAction, now.QuadPart};
    nextCorrelation = (nextCorrelation + 1) % correlations.size();
    ReleaseSRWLockExclusive(&correlationLock);
}

LONG64 CorrelatedShotConstructionTick(void* shot) noexcept {
    LONG64 tick{};
    AcquireSRWLockShared(&correlationLock);
    for (const auto& sample : correlations)
        if (sample.shot == shot) tick = sample.constructionTick;
    ReleaseSRWLockShared(&correlationLock);
    return tick;
}

void* CorrelatedShotAction(void* shot) noexcept {
    void* action{};
    AcquireSRWLockShared(&correlationLock);
    for (const auto& sample : correlations)
        if (sample.shot == shot) action = sample.action;
    ReleaseSRWLockShared(&correlationLock);
    return action;
}

void AppendArcherActionIdentity(std::ostringstream& out) {
    out << ", tracked_action_vtable_rva=0x" << std::hex
        << g.trackedActionVtableRva << ", tracked_frame_getter_rva=0x"
        << g.trackedFrameGetterRva << std::dec;
    AppendArcherActionProgress(out);
}

void InsertActionCandidate(const ArcherActionCandidate& candidate,
    std::array<ArcherActionCandidate, 8>& candidates) noexcept {
    const unsigned score = unsigned(std::abs(
        std::abs(int(candidate.streak)) - 30));
    for (std::size_t slot = 0; slot < candidates.size(); ++slot) {
        const unsigned current = candidates[slot].streak ? unsigned(std::abs(
            std::abs(int(candidates[slot].streak)) - 30)) : UINT_MAX;
        if (score >= current) continue;
        for (std::size_t move = candidates.size() - 1; move > slot; --move)
            candidates[move] = candidates[move - 1];
        candidates[slot] = candidate;
        return;
    }
}
} // namespace nioh1fix::runtime
