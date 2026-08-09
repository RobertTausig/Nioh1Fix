#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace nioh1fix {
inline constexpr std::size_t kGameplayActionCounterCount = 56;

inline void UpdateGameplayActionCounters(
    std::span<std::uint32_t, kGameplayActionCounterCount> counters,
    std::uint64_t current, std::uint64_t previous, bool advanceHeld) {
    for (std::size_t action = 0; action < counters.size(); ++action) {
        const bool isCurrent = (current & 1) != 0;
        const bool wasCurrent = (previous & 1) != 0;
        if (isCurrent) {
            if (!wasCurrent)
                counters[action] = 1;
            else if (advanceHeld)
                ++counters[action];
        }
        current >>= 1;
        previous >>= 1;
    }
}
} // namespace nioh1fix
