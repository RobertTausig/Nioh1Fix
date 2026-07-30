#include "timing_scale.hpp"

#include <cmath>
#include <iostream>
#include <limits>

namespace
{
bool Check(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
    }
    return condition;
}

bool Near(double actual, double expected, double tolerance = 0.0001)
{
    return std::abs(actual - expected) <= tolerance;
}
} // namespace

int main()
{
    bool ok = true;

    nioh1fix::TimingScaleState at60;
    ok &= Check(Near(nioh1fix::UpdateTimingScale(
                         at60, 1.0 / 60.0, 120.0),
                     1.0),
                "60 Hz did not produce scale 1");

    nioh1fix::TimingScaleState at120;
    ok &= Check(Near(nioh1fix::UpdateTimingScale(
                         at120, 1.0 / 120.0, 120.0),
                     0.5),
                "120 Hz did not produce scale 0.5");

    nioh1fix::TimingScaleState transition;
    nioh1fix::UpdateTimingScale(transition, 1.0 / 60.0, 120.0);
    double previous = transition.scale;
    for (int frame = 0; frame < 24; ++frame) {
        const double current = nioh1fix::UpdateTimingScale(
            transition, 1.0 / 120.0, 120.0);
        ok &= Check(current <= previous && current >= 0.5,
                    "60-to-120 transition was not bounded and monotonic");
        previous = current;
    }
    ok &= Check(Near(transition.scale, 0.5, 0.001),
                "60-to-120 transition did not converge");

    for (int frame = 0; frame < 24; ++frame) {
        previous = transition.scale;
        const double current = nioh1fix::UpdateTimingScale(
            transition, 1.0 / 60.0, 120.0);
        ok &= Check(current >= previous && current <= 1.0,
                    "120-to-60 transition was not bounded and monotonic");
    }
    ok &= Check(Near(transition.scale, 1.0, 0.001),
                "120-to-60 transition did not converge");

    nioh1fix::TimingScaleState invalid;
    ok &= Check(Near(nioh1fix::UpdateTimingScale(
                         invalid,
                         std::numeric_limits<double>::quiet_NaN(),
                         120.0),
                     0.5),
                "invalid first interval did not use the bounded fallback");
    ok &= Check(Near(nioh1fix::UpdateTimingScale(invalid, 1.0, 120.0), 0.5),
                "stalled interval did not preserve the safe fallback");

    nioh1fix::TimingScaleState stable;
    nioh1fix::UpdateTimingScale(stable, 1.0 / 60.0, 120.0);
    ok &= Check(Near(nioh1fix::UpdateTimingScale(stable, 0.5, 120.0), 1.0),
                "stall disturbed a stable timing scale");
    ok &= Check(nioh1fix::UpdateTimingScale(stable, 1.0 / 10000.0, 120.0) >=
                    nioh1fix::kMinimumTimingScale,
                "extreme interval escaped the lower bound");

    if (ok) {
        std::cout << "All timing scale tests passed.\n";
        return 0;
    }
    return 1;
}
