#include "archer_diagnostic.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace nioh1fix::runtime {
namespace {
struct HeldMotionState {
    void* action{};
    void* source{};
    double accumulator{};
    ULONGLONG lastTick{};
    LONG calls{}, transitions{}, starts{}, steps{}, skipped{};
    bool held{};
};

SRWLOCK motionLock = SRWLOCK_INIT;
HeldMotionState heldMotion{};

int TakeMotionSteps(ULONGLONG now) {
    int steps = 1;
    if (heldMotion.lastTick) {
        const ULONGLONG elapsed = now - heldMotion.lastTick;
        if (elapsed > 500) {
            heldMotion.accumulator = 0.0;
        } else {
            heldMotion.accumulator += double(elapsed) * 0.06;
            steps = std::min(4, static_cast<int>(heldMotion.accumulator));
            heldMotion.accumulator -= steps;
        }
    }
    heldMotion.lastTick = now;
    return steps;
}

bool IsHeldStart(float before, float after) {
    return std::isfinite(before) && std::isfinite(after) &&
        before >= 40.0F && before <= 46.0F && after < 5.0F &&
        after + 1.0F < before;
}
} // namespace

void RunTrackedArcherMotion(void* action, void* source) {
    auto original = g.originalArcherMotionUpdate;
    if (!original) return;
    auto* tracked = InterlockedCompareExchangePointer(
        &g.trackedArcherAction, nullptr, nullptr);
    if (action != tracked || !action || !source || IsThirtyFpsProfile() ||
        !ReadableArcherMemory(source, 0x64)) {
        original(action, source);
        return;
    }

    const ULONGLONG now = GetTickCount64();
    AcquireSRWLockExclusive(&motionLock);
    if (heldMotion.action != action || heldMotion.source != source)
        heldMotion = {.action = action, .source = source};
    ++heldMotion.calls;
    const bool held = heldMotion.held;
    const int steps = held ? TakeMotionSteps(now) : 1;
    if (held) {
        heldMotion.steps += steps;
        if (!steps) ++heldMotion.skipped;
    }
    ReleaseSRWLockExclusive(&motionLock);

    const float before = *reinterpret_cast<float*>(
        static_cast<std::uint8_t*>(source) + 0x60);
    for (int step = 0; step < steps; ++step) original(action, source);
    if (held || !ReadableArcherMemory(source, 0x64)) return;
    const float after = *reinterpret_cast<float*>(
        static_cast<std::uint8_t*>(source) + 0x60);
    if (!IsHeldStart(before, after)) return;

    AcquireSRWLockExclusive(&motionLock);
    if (heldMotion.action == action && heldMotion.source == source) {
        ++heldMotion.transitions;
        ++heldMotion.starts;
        heldMotion.held = true;
        heldMotion.accumulator = 0.0;
        heldMotion.lastTick = now;
    }
    ReleaseSRWLockExclusive(&motionLock);
}

void FinishArcherHeldPhase(void* action) noexcept {
    AcquireSRWLockExclusive(&motionLock);
    if (heldMotion.action == action) {
        heldMotion.held = false;
        heldMotion.accumulator = 0.0;
        heldMotion.lastTick = 0;
    }
    ReleaseSRWLockExclusive(&motionLock);
}

void AppendArcherMotionDiagnostics(std::ostringstream& out) {
    AcquireSRWLockExclusive(&motionLock);
    out << ", archer_motion=" << heldMotion.calls << '/'
        << heldMotion.transitions << '/' << heldMotion.starts << '/'
        << heldMotion.steps << '/' << heldMotion.skipped << '/'
        << heldMotion.held;
    heldMotion.calls = heldMotion.transitions = heldMotion.starts = 0;
    heldMotion.steps = heldMotion.skipped = 0;
    ReleaseSRWLockExclusive(&motionLock);
}
} // namespace nioh1fix::runtime
