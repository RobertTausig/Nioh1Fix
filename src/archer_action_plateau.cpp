#include "archer_diagnostic.hpp"

#include <cmath>
#include <cstdint>
#include <sstream>

namespace nioh1fix::runtime {
namespace {
struct FrameProgress {
    void* action{};
    LONG presentFrame{-1};
    LONG calls{}, uniquePresents{}, changes{}, resets{};
    LONG64 lastChangeTick{}, lastPlateauEnd{};
    float previous{}, current{}, positiveAdvance{}, lastPlateauFrame{};
    LONG lastPlateauMs{-1};
    std::array<LONG64, 8> resetTicks{};
    std::array<float, 8> resetBefore{}, resetAfter{};
    std::size_t nextReset{};
    bool initialized{};
};

SRWLOCK progressLock = SRWLOCK_INIT;
FrameProgress progress{};

LONG Milliseconds(LONG64 ticks) {
    if (ticks < 0 || g.frequency.QuadPart <= 0) return -1;
    return static_cast<LONG>(double(ticks) * 1000.0 /
                             double(g.frequency.QuadPart));
}
} // namespace

bool ReadableArcherMemory(const void* address, std::size_t size) noexcept {
    MEMORY_BASIC_INFORMATION region{};
    if (!address || !size ||
        !VirtualQuery(address, &region, sizeof(region)) ||
        region.State != MEM_COMMIT ||
        (region.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    const auto begin = reinterpret_cast<std::uintptr_t>(address);
    const auto end = reinterpret_cast<std::uintptr_t>(region.BaseAddress) +
                     region.RegionSize;
    return begin <= end && size <= end - begin;
}

bool SampleActionFrameProgress(void* action) noexcept {
    if (!ReadableArcherMemory(action, 0x2C)) return false;
    const float current = *reinterpret_cast<float*>(
        static_cast<std::uint8_t*>(action) + 0x28);
    LARGE_INTEGER now{};
    if (!std::isfinite(current) || !QueryPerformanceCounter(&now)) return false;

    AcquireSRWLockExclusive(&progressLock);
    if (progress.action != action)
        progress = {.action = action, .lastChangeTick = now.QuadPart};
    ++progress.calls;
    const LONG presentFrame = g.presentCalls;
    if (progress.presentFrame != presentFrame) {
        progress.presentFrame = presentFrame;
        ++progress.uniquePresents;
    }
    bool reset = false;
    if (progress.initialized) {
        const float difference = current - progress.previous;
        if (std::abs(difference) > 0.0001F) {
            const LONG stableMs = Milliseconds(
                now.QuadPart - progress.lastChangeTick);
            if (stableMs >= 50) {
                progress.lastPlateauFrame = progress.previous;
                progress.lastPlateauMs = stableMs;
                progress.lastPlateauEnd = now.QuadPart;
            }
            progress.lastChangeTick = now.QuadPart;
        }
        if (difference > 0.0F && difference < 10.0F) {
            progress.positiveAdvance += difference;
            ++progress.changes;
        } else if (difference < -0.01F) {
            ++progress.resets;
            reset = true;
            progress.resetTicks[progress.nextReset] = now.QuadPart;
            progress.resetBefore[progress.nextReset] = progress.previous;
            progress.resetAfter[progress.nextReset] = current;
            progress.nextReset = (progress.nextReset + 1) %
                                 progress.resetTicks.size();
        }
    }
    progress.previous = current;
    progress.current = current;
    progress.initialized = true;
    ReleaseSRWLockExclusive(&progressLock);
    return reset;
}

ArcherActionResets SnapshotActionResets(void* action) noexcept {
    ArcherActionResets result{};
    LARGE_INTEGER now{};
    if (!QueryPerformanceCounter(&now)) return result;
    AcquireSRWLockShared(&progressLock);
    if (progress.action == action)
        for (std::size_t i = 0; i < result.size(); ++i) {
            const std::size_t index = (progress.nextReset +
                progress.resetTicks.size() - 1 - i) % progress.resetTicks.size();
            if (progress.resetTicks[index] > 0)
                result[i] = {
                    Milliseconds(now.QuadPart - progress.resetTicks[index]),
                    progress.resetBefore[index], progress.resetAfter[index]};
        }
    ReleaseSRWLockShared(&progressLock);
    return result;
}

ArcherActionPlateau SnapshotActionPlateau(void* action) noexcept {
    ArcherActionPlateau result{};
    LARGE_INTEGER now{};
    if (!QueryPerformanceCounter(&now)) return result;
    AcquireSRWLockShared(&progressLock);
    if (progress.action == action) {
        const LONG currentMs = Milliseconds(now.QuadPart - progress.lastChangeTick);
        if (currentMs >= 50) {
            result = {progress.current, currentMs, 0};
        } else {
            result = {progress.lastPlateauFrame, progress.lastPlateauMs,
                Milliseconds(now.QuadPart - progress.lastPlateauEnd)};
        }
    }
    ReleaseSRWLockShared(&progressLock);
    return result;
}

void AppendArcherActionProgress(std::ostringstream& out) {
    AcquireSRWLockExclusive(&progressLock);
    out << ", tracked_frame=" << progress.current
        << ", frame_progress=" << progress.positiveAdvance
        << ", frame_changes=" << progress.changes
        << ", frame_resets=" << progress.resets
        << ", tracked_calls_interval=" << progress.calls
        << ", tracked_presents_interval=" << progress.uniquePresents;
    progress.calls = progress.uniquePresents = progress.changes = 0;
    progress.resets = 0;
    progress.positiveAdvance = 0.0F;
    ReleaseSRWLockExclusive(&progressLock);
}
} // namespace nioh1fix::runtime
