#include "input_core.hpp"

#include <array>
#include <cassert>

int main() {
    std::array<std::uint32_t, nioh1fix::kGameplayActionCounterCount> counters{};
    counters[3] = 19;

    nioh1fix::UpdateGameplayActionCounters(counters, 1ULL << 3, 0, false);
    assert(counters[3] == 1);

    nioh1fix::UpdateGameplayActionCounters(
        counters, 1ULL << 3, 1ULL << 3, false);
    assert(counters[3] == 1);

    nioh1fix::UpdateGameplayActionCounters(
        counters, 1ULL << 3, 1ULL << 3, true);
    assert(counters[3] == 2);

    counters[55] = 8;
    nioh1fix::UpdateGameplayActionCounters(counters, 1ULL << 55, 0, true);
    assert(counters[55] == 1);
}
