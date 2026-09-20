#include "archer_diagnostic.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>

namespace nioh1fix::runtime {
namespace {
constexpr std::size_t kBytes = 0x800;
constexpr std::size_t kChildBytes = 0x400;
constexpr std::size_t kChildren = 24;
template <std::size_t Size> struct ByteTracker {
    std::array<std::uint8_t, Size> previous{}, recentValues{}, recentAges{};
    std::array<std::int16_t, Size> streaks{}, recentStreaks{};
    bool initialized{};
};
struct Child {
    std::uint16_t pointerOffset{0xFFFF};
    void* address{};
    ByteTracker<kChildBytes> bytes{};
};

SRWLOCK byteLock = SRWLOCK_INIT;
void* trackedAction{};
LONG sampledFrame{-1};
ByteTracker<kBytes> direct{};
std::array<Child, kChildren> children{};

bool Writable(const void* address, std::size_t size) {
    MEMORY_BASIC_INFORMATION region{};
    if (!address || !VirtualQuery(address, &region, sizeof(region)) ||
        region.State != MEM_COMMIT ||
        (region.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    constexpr DWORD writable = PAGE_READWRITE | PAGE_WRITECOPY |
        PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    const auto begin = reinterpret_cast<std::uintptr_t>(address);
    const auto end = reinterpret_cast<std::uintptr_t>(region.BaseAddress) +
                     region.RegionSize;
    return (region.Protect & writable) && begin <= end && size <= end - begin;
}

template <std::size_t Size>
void Update(ByteTracker<Size>& tracker, const std::uint8_t* bytes) {
    if (!tracker.initialized) {
        std::copy_n(bytes, Size, tracker.previous.begin());
        tracker.recentAges.fill(std::numeric_limits<std::uint8_t>::max());
        tracker.initialized = true;
        return;
    }
    for (std::size_t i = 0; i < Size; ++i) {
        if (tracker.recentAges[i] != std::numeric_limits<std::uint8_t>::max())
            ++tracker.recentAges[i];
        const int difference = int(bytes[i]) - int(tracker.previous[i]);
        const auto old = tracker.streaks[i];
        if ((difference == 1 && old >= 0) ||
            (difference == -1 && old <= 0)) {
            tracker.streaks[i] = static_cast<std::int16_t>(std::clamp(
                int(old) + difference, -32'000, 32'000));
        } else {
            if (std::abs(int(old)) >= 3) {
                tracker.recentStreaks[i] = old;
                tracker.recentValues[i] = tracker.previous[i];
                tracker.recentAges[i] = 0;
            }
            tracker.streaks[i] = 0;
        }
        tracker.previous[i] = bytes[i];
    }
}

void Reset(void* action) {
    trackedAction = action;
    direct = {};
    children = {};
    std::size_t count{};
    const auto* bytes = static_cast<const std::uint8_t*>(action);
    for (std::size_t offset = 0; offset < kBytes && count < children.size();
         offset += sizeof(void*)) {
        auto* address = *reinterpret_cast<void* const*>(bytes + offset);
        if (address == action || !Writable(address, kChildBytes)) continue;
        bool duplicate = false;
        for (std::size_t i = 0; i < count; ++i)
            duplicate |= children[i].address == address;
        if (duplicate) continue;
        children[count].pointerOffset = static_cast<std::uint16_t>(offset);
        children[count++].address = address;
    }
}

template <std::size_t Size>
void Select(const ByteTracker<Size>& tracker, std::uint16_t pointerOffset,
    std::array<ArcherActionCandidate, 8>& candidates) {
    for (std::size_t i = 0; i < Size; ++i) {
        auto streak = tracker.streaks[i];
        LONG value = tracker.previous[i];
        if (tracker.recentAges[i] <= 4 && std::abs(int(
            tracker.recentStreaks[i])) > std::abs(int(streak))) {
            streak = tracker.recentStreaks[i];
            value = tracker.recentValues[i];
        }
        const unsigned length = std::abs(int(streak));
        if (length >= 3 && length <= 120)
            InsertActionCandidate({pointerOffset, static_cast<std::uint16_t>(i),
                streak, value, true}, candidates);
    }
}
} // namespace

void SampleActionBytes(void* action) noexcept {
    AcquireSRWLockExclusive(&byteLock);
    if (trackedAction != action) Reset(action);
    if (sampledFrame != g.presentCalls && Writable(action, kBytes)) {
        sampledFrame = g.presentCalls;
        Update(direct, static_cast<const std::uint8_t*>(action));
        const auto* root = static_cast<const std::uint8_t*>(action);
        for (auto& child : children) {
            if (!child.address) continue;
            auto* current = *reinterpret_cast<void* const*>(
                root + child.pointerOffset);
            if (current != child.address || !Writable(child.address, kChildBytes))
                child = {};
            else Update(child.bytes,
                static_cast<const std::uint8_t*>(child.address));
        }
    }
    ReleaseSRWLockExclusive(&byteLock);
}

void SnapshotActionByteCandidates(void* action,
    std::array<ArcherActionCandidate, 8>& candidates) noexcept {
    AcquireSRWLockShared(&byteLock);
    if (trackedAction == action) {
        Select(direct, 0xFFFF, candidates);
        for (const auto& child : children)
            if (child.address) Select(child.bytes, child.pointerOffset, candidates);
    }
    ReleaseSRWLockShared(&byteLock);
}
} // namespace nioh1fix::runtime
