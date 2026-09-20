#include "archer_diagnostic.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <sstream>

namespace nioh1fix::runtime {
namespace {
struct ShotSample { void* shot{}; LONG64 lastTick{}; };
struct OwnerSample { void* owner{}; LONG64 previousRelease{}; };
struct ReleaseEvent {
    LONG sequence{}, elapsedMs{}, intervalMs{-1}, fpsTimesTen{};
    void* owner{};
    LONG ownerCategory{-1};
    ArcherActionResets resets{};
    std::array<ArcherActionCandidate, 8> candidates{};
};

SRWLOCK diagnosticLock = SRWLOCK_INIT;
std::array<ShotSample, 64> shots{};
std::array<OwnerSample, 16> owners{};
std::array<ReleaseEvent, 32> events{};
LONG sequence{};
LONG64 firstReleaseTick{};

ShotSample& FindShot(void* shot) {
    ShotSample* unused = nullptr;
    ShotSample* oldest = shots.data();
    for (auto& sample : shots) {
        if (sample.shot == shot) return sample;
        if (!sample.shot && !unused) unused = &sample;
        if (sample.lastTick < oldest->lastTick) oldest = &sample;
    }
    return unused ? *unused : *oldest;
}

OwnerSample& FindOwner(void* owner) {
    OwnerSample* unused = nullptr;
    OwnerSample* oldest = owners.data();
    for (auto& sample : owners) {
        if (sample.owner == owner) return sample;
        if (!sample.owner && !unused) unused = &sample;
        if (sample.previousRelease < oldest->previousRelease) oldest = &sample;
    }
    return unused ? *unused : *oldest;
}
} // namespace

void CaptureArrowRelease(void* childShot) noexcept {
    if (!ReadableArcherMemory(childShot, 0x28)) return;
    auto* shot = *reinterpret_cast<void**>(
        static_cast<std::uint8_t*>(childShot) + 0x20);
    if (!ReadableArcherMemory(shot, 0x208) ||
        *reinterpret_cast<std::uint16_t*>(
            static_cast<std::uint8_t*>(shot) + 4) != 4) return;
    LARGE_INTEGER now{};
    if (!QueryPerformanceCounter(&now)) return;

    AcquireSRWLockExclusive(&diagnosticLock);
    auto& shotSample = FindShot(shot);
    const LONG64 gap = now.QuadPart - shotSample.lastTick;
    const bool released = shotSample.shot != shot || shotSample.lastTick <= 0 ||
        (g.frequency.QuadPart > 0 && gap > g.frequency.QuadPart / 4);
    shotSample = {shot, now.QuadPart};
    if (released && g.frequency.QuadPart > 0) {
        auto* owner = *reinterpret_cast<void**>(
            static_cast<std::uint8_t*>(shot) + 0x200);
        LONG category = -1;
        if (ReadableArcherMemory(owner, 8)) category = *reinterpret_cast<
            std::uint16_t*>(static_cast<std::uint8_t*>(owner) + 4);
        auto* action = CorrelatedShotAction(shot);
        auto& ownerSample = FindOwner(owner);
        LONG interval = -1;
        if (ownerSample.owner == owner && ownerSample.previousRelease > 0 &&
            now.QuadPart > ownerSample.previousRelease)
            interval = static_cast<LONG>(double(
                now.QuadPart - ownerSample.previousRelease) * 1000.0 /
                double(g.frequency.QuadPart));
        ownerSample = {owner, now.QuadPart};
        if (!firstReleaseTick) firstReleaseTick = now.QuadPart;
        const LONG64 presentInterval = InterlockedCompareExchange64(
            &g.lastPresentInterval, 0, 0);
        ReleaseEvent& event = events[std::size_t(sequence) % events.size()];
        event = {++sequence, static_cast<LONG>(double(
            now.QuadPart - firstReleaseTick) * 1000.0 /
            double(g.frequency.QuadPart)), interval, static_cast<LONG>(
            double(g.frequency.QuadPart) * 10.0 /
            double(std::max<LONG64>(1, presentInterval))), owner, category};
        event.resets = SnapshotActionResets(action);
        if (category == 0) {
            FinishArcherHeldPhase(action);
            SnapshotArcherOwnerCandidates(owner, event.candidates);
            InterlockedExchangePointer(&g.trackedArcherAction, action);
            InterlockedExchangePointer(&g.trackedArcherOwner, owner);
        }
    }
    ReleaseSRWLockExclusive(&diagnosticLock);
}

void AppendArcherHoldDiagnostics(std::ostringstream& out) {
    static LONG reported{};
    out << ", child_shot_trace_calls=" << Counter(9)
        << ", action_event_trace_calls=" << Counter(10)
        << ", shot_constructor_trace_calls=" << Counter(11)
        << ", tracked_action_event_calls=" << g.trackedActionEventCalls
        << ", tracked_action_delta="
        << std::bit_cast<float>(g.trackedActionDeltaBits);
    AppendArcherActionIdentity(out);
    AppendArcherMotionDiagnostics(out);
    AppendArcherOwnerResetDiagnostics(out);
    AcquireSRWLockShared(&diagnosticLock);
    if (sequence > reported) {
        out << ", arrow_releases=[";
        const LONG first = std::max<LONG>(
            reported + 1, sequence - static_cast<LONG>(events.size()) + 1);
        for (LONG number = first; number <= sequence; ++number) {
            const auto& event = events[std::size_t(number - 1) % events.size()];
            if (number != first) out << ';';
            out << event.sequence << '@' << event.elapsedMs << "ms/"
                << event.fpsTimesTen / 10 << '.' << event.fpsTimesTen % 10
                << "fps/owner=" << event.owner << "/cat"
                << event.ownerCategory << "/interval=" << event.intervalMs
                << "ms/resets=";
            for (const auto& reset : event.resets)
                if (reset.ageMs >= 0)
                    out << reset.ageMs << ':' << reset.before << '>'
                        << reset.after << ',';
            out << "/scan=";
            bool separator = false;
            for (const auto& candidate : event.candidates) {
                if (!candidate.streak) continue;
                if (separator) out << ',';
                separator = true;
                if (candidate.pointerOffset != 0xFFFF)
                    out << "+0x" << std::hex << candidate.pointerOffset
                        << "->";
                out << "+0x" << std::hex << candidate.offset << std::dec
                    << ':' << (candidate.byteValue ? 'b' : 'i') << ':'
                    << candidate.value << ':' << candidate.streak;
            }
        }
        out << ']';
        reported = sequence;
    }
    ReleaseSRWLockShared(&diagnosticLock);
}
} // namespace nioh1fix::runtime
