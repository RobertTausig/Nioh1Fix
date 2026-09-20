#include "archer_diagnostic.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>

namespace nioh1fix::runtime {
namespace {
constexpr std::size_t kWords = 512;
struct Tracker {
    void* action{};
    LONG presentFrame{-1};
    std::array<LONG, kWords> previous{}, recentValues{};
    std::array<std::int16_t, kWords> streaks{}, recentStreaks{};
    std::array<std::uint8_t, kWords> recentAges{};
    bool initialized{};
};

SRWLOCK scanLock = SRWLOCK_INIT;
Tracker tracker{};

bool Readable(const void* address, std::size_t size) {
    MEMORY_BASIC_INFORMATION region{};
    if (!address || !VirtualQuery(address, &region, sizeof(region)) ||
        region.State != MEM_COMMIT ||
        (region.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    const auto begin = reinterpret_cast<std::uintptr_t>(address);
    const auto end = reinterpret_cast<std::uintptr_t>(region.BaseAddress) +
                     region.RegionSize;
    return begin <= end && size <= end - begin;
}
} // namespace

void SampleTrackedAction(void* action) noexcept {
    const LONG frame = g.presentCalls;
    AcquireSRWLockExclusive(&scanLock);
    if (tracker.action != action) tracker = {.action = action};
    if (tracker.presentFrame == frame || !Readable(action, kWords * 4)) {
        ReleaseSRWLockExclusive(&scanLock);
        return;
    }
    tracker.presentFrame = frame;
    const auto* words = static_cast<const LONG*>(action);
    if (!tracker.initialized) {
        std::copy_n(words, kWords, tracker.previous.begin());
        tracker.recentAges.fill(std::numeric_limits<std::uint8_t>::max());
        tracker.initialized = true;
        ReleaseSRWLockExclusive(&scanLock);
        return;
    }
    for (std::size_t i = 0; i < kWords; ++i) {
        if (tracker.recentAges[i] != std::numeric_limits<std::uint8_t>::max())
            ++tracker.recentAges[i];
        const LONG64 difference = LONG64(words[i]) -
                                  LONG64(tracker.previous[i]);
        const auto old = tracker.streaks[i];
        if ((difference == 1 && old >= 0) ||
            (difference == -1 && old <= 0)) {
            const int next = old + (difference > 0 ? 1 : -1);
            tracker.streaks[i] = static_cast<std::int16_t>(
                std::clamp(next, -32'000, 32'000));
        } else {
            if (std::abs(int(old)) >= 3) {
                tracker.recentStreaks[i] = old;
                tracker.recentValues[i] = tracker.previous[i];
                tracker.recentAges[i] = 0;
            }
            tracker.streaks[i] = 0;
        }
        tracker.previous[i] = words[i];
    }
    ReleaseSRWLockExclusive(&scanLock);
    SampleNestedAction(action);
}

void SnapshotActionCandidates(void* action,
    std::array<ArcherActionCandidate, 8>& candidates) noexcept {
    std::array<unsigned, 8> scores{};
    scores.fill(std::numeric_limits<unsigned>::max());
    AcquireSRWLockShared(&scanLock);
    if (tracker.action == action)
        for (std::size_t i = 0; i < kWords; ++i) {
            auto streak = tracker.streaks[i];
            LONG value = tracker.previous[i];
            if (tracker.recentAges[i] <= 4 &&
                std::abs(int(tracker.recentStreaks[i])) >
                    std::abs(int(streak))) {
                streak = tracker.recentStreaks[i];
                value = tracker.recentValues[i];
            }
            const unsigned length = std::abs(int(streak));
            if (length < 3 || length > 120) continue;
            const unsigned score = unsigned(std::abs(int(length) - 30));
            for (std::size_t slot = 0; slot < candidates.size(); ++slot) {
                if (score >= scores[slot]) continue;
                for (std::size_t move = candidates.size() - 1;
                     move > slot; --move) {
                    scores[move] = scores[move - 1];
                    candidates[move] = candidates[move - 1];
                }
                scores[slot] = score;
                candidates[slot] = {0xFFFF,
                    static_cast<std::uint16_t>(i * 4), streak, value, false};
                break;
            }
        }
    ReleaseSRWLockShared(&scanLock);
    SnapshotNestedActionCandidates(action, candidates);
    SnapshotActionByteCandidates(action, candidates);
}
} // namespace nioh1fix::runtime
