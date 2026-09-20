#include "archer_diagnostic.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>

namespace nioh1fix::runtime {
namespace {
constexpr std::size_t kRootBytes = 0x800;
constexpr std::size_t kRegionCount = 24;
constexpr std::size_t kWords = 256;
struct Region {
    std::uint16_t pointerOffset{0xFFFF};
    void* address{};
    std::array<LONG, kWords> previous{}, recentValues{};
    std::array<std::int16_t, kWords> streaks{}, recentStreaks{};
    std::array<std::uint8_t, kWords> recentAges{};
    bool initialized{};
};

SRWLOCK nestedLock = SRWLOCK_INIT;
void* trackedRoot{};
LONG sampledFrame{-1};
std::array<Region, kRegionCount> regions{};

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

void Discover(void* action) {
    trackedRoot = action;
    regions = {};
    std::size_t count{};
    const auto* bytes = static_cast<const std::uint8_t*>(action);
    for (std::size_t offset = 0; offset < kRootBytes &&
         count < regions.size(); offset += sizeof(void*)) {
        auto* address = *reinterpret_cast<void* const*>(bytes + offset);
        if (address == action || !Writable(address, kWords * 4)) continue;
        bool duplicate = false;
        for (std::size_t i = 0; i < count; ++i)
            duplicate |= regions[i].address == address;
        if (duplicate) continue;
        regions[count].pointerOffset = static_cast<std::uint16_t>(offset);
        regions[count].address = address;
        ++count;
    }
}

void Update(Region& region) {
    const auto* words = static_cast<const LONG*>(region.address);
    if (!region.initialized) {
        std::copy_n(words, kWords, region.previous.begin());
        region.recentAges.fill(std::numeric_limits<std::uint8_t>::max());
        region.initialized = true;
        return;
    }
    for (std::size_t i = 0; i < kWords; ++i) {
        if (region.recentAges[i] != std::numeric_limits<std::uint8_t>::max())
            ++region.recentAges[i];
        const LONG64 difference = LONG64(words[i]) - LONG64(region.previous[i]);
        const auto old = region.streaks[i];
        if ((difference == 1 && old >= 0) ||
            (difference == -1 && old <= 0)) {
            region.streaks[i] = static_cast<std::int16_t>(std::clamp(
                int(old) + (difference > 0 ? 1 : -1), -32'000, 32'000));
        } else {
            if (std::abs(int(old)) >= 3) {
                region.recentStreaks[i] = old;
                region.recentValues[i] = region.previous[i];
                region.recentAges[i] = 0;
            }
            region.streaks[i] = 0;
        }
        region.previous[i] = words[i];
    }
}

} // namespace

void SampleNestedAction(void* action) noexcept {
    AcquireSRWLockExclusive(&nestedLock);
    if (trackedRoot != action) Discover(action);
    if (sampledFrame != g.presentCalls) {
        sampledFrame = g.presentCalls;
        const auto* root = static_cast<const std::uint8_t*>(action);
        for (auto& region : regions) {
            if (!region.address) continue;
            auto* current = *reinterpret_cast<void* const*>(
                root + region.pointerOffset);
            if (current != region.address || !Writable(region.address, kWords * 4))
                region = {};
            else Update(region);
        }
    }
    ReleaseSRWLockExclusive(&nestedLock);
}

void SnapshotNestedActionCandidates(void* action,
    std::array<ArcherActionCandidate, 8>& candidates) noexcept {
    AcquireSRWLockShared(&nestedLock);
    if (trackedRoot == action)
        for (const auto& region : regions) {
            if (!region.address) continue;
            for (std::size_t i = 0; i < kWords; ++i) {
                auto streak = region.streaks[i];
                LONG value = region.previous[i];
                if (region.recentAges[i] <= 4 && std::abs(int(
                    region.recentStreaks[i])) > std::abs(int(streak))) {
                    streak = region.recentStreaks[i];
                    value = region.recentValues[i];
                }
                const unsigned length = std::abs(int(streak));
                if (length >= 3 && length <= 120)
                    InsertActionCandidate({region.pointerOffset,
                        static_cast<std::uint16_t>(i * 4), streak, value,
                        false},
                        candidates);
            }
        }
    ReleaseSRWLockShared(&nestedLock);
}
} // namespace nioh1fix::runtime
