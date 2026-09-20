#include "archer_diagnostic.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>
#include <sstream>

namespace nioh1fix::runtime {
namespace {
constexpr std::size_t kWords = 2048;
struct OwnerTracker {
    void* owner{};
    LONG presentFrame{-1};
    std::array<LONG, kWords> previous{}, recentValues{};
    std::array<std::int16_t, kWords> streaks{}, recentStreaks{};
    std::array<std::uint8_t, kWords> recentAges{};
    bool initialized{};
};
struct ResetEvent {
    LONG sequence{};
    std::array<ArcherActionCandidate, 8> candidates{};
};

SRWLOCK ownerLock = SRWLOCK_INIT;
OwnerTracker tracker{};
std::array<ResetEvent, 16> resetEvents{};
LONG resetSequence{};

void CollectCandidates(std::array<ArcherActionCandidate, 8>& candidates) {
    for (std::size_t i = 0; i < kWords; ++i) {
        auto streak = tracker.streaks[i];
        LONG value = tracker.previous[i];
        if (tracker.recentAges[i] <= 6 &&
            std::abs(int(tracker.recentStreaks[i])) > std::abs(int(streak))) {
            streak = tracker.recentStreaks[i];
            value = tracker.recentValues[i];
        }
        const unsigned length = std::abs(int(streak));
        if (length < 3 || length > 180) continue;
        InsertActionCandidate({0xFFFF,
            static_cast<std::uint16_t>(i * sizeof(LONG)), streak,
            value, false}, candidates);
    }
}
} // namespace

void SampleArcherOwner(void* owner) noexcept {
    const LONG frame = g.presentCalls;
    AcquireSRWLockExclusive(&ownerLock);
    if (tracker.owner != owner) tracker = {.owner = owner};
    if (tracker.presentFrame == frame ||
        !ReadableArcherMemory(owner, kWords * sizeof(LONG))) {
        ReleaseSRWLockExclusive(&ownerLock);
        return;
    }
    tracker.presentFrame = frame;
    const auto* words = static_cast<const LONG*>(owner);
    if (!tracker.initialized) {
        std::copy_n(words, kWords, tracker.previous.begin());
        tracker.recentAges.fill(std::numeric_limits<std::uint8_t>::max());
        tracker.initialized = true;
        ReleaseSRWLockExclusive(&ownerLock);
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
            tracker.streaks[i] = static_cast<std::int16_t>(std::clamp(
                int(old) + (difference > 0 ? 1 : -1), -32'000, 32'000));
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
    ReleaseSRWLockExclusive(&ownerLock);
}

void SnapshotArcherOwnerCandidates(void* owner,
    std::array<ArcherActionCandidate, 8>& candidates) noexcept {
    AcquireSRWLockShared(&ownerLock);
    if (tracker.owner == owner) CollectCandidates(candidates);
    ReleaseSRWLockShared(&ownerLock);
}

void CaptureArcherOwnerReset(void* owner) noexcept {
    AcquireSRWLockExclusive(&ownerLock);
    if (tracker.owner == owner) {
        auto& event = resetEvents[std::size_t(resetSequence) % resetEvents.size()];
        event = {};
        event.sequence = ++resetSequence;
        CollectCandidates(event.candidates);
    }
    ReleaseSRWLockExclusive(&ownerLock);
}

void AppendArcherOwnerResetDiagnostics(std::ostringstream& out) {
    static LONG reported{};
    AcquireSRWLockShared(&ownerLock);
    if (resetSequence > reported) {
        out << ", owner_resets=[";
        const LONG first = std::max<LONG>(reported + 1,
            resetSequence - static_cast<LONG>(resetEvents.size()) + 1);
        for (LONG number = first; number <= resetSequence; ++number) {
            if (number != first) out << ';';
            const auto& event = resetEvents[std::size_t(number - 1) %
                                            resetEvents.size()];
            out << event.sequence << ':';
            for (const auto& candidate : event.candidates)
                if (candidate.streak)
                    out << "+0x" << std::hex << candidate.offset << std::dec
                        << '=' << candidate.value << ':' << candidate.streak << ',';
        }
        out << ']';
        reported = resetSequence;
    }
    ReleaseSRWLockShared(&ownerLock);
}
} // namespace nioh1fix::runtime
