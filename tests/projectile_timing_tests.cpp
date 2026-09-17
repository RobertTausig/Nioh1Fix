#include "projectile_core.hpp"

#include <cmath>
#include <iostream>
#include <limits>

namespace {
bool Check(bool condition, const char* message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

bool Near(double actual, double expected, double tolerance = 0.0001) {
    return std::abs(actual - expected) <= tolerance;
}

struct CadenceResult {
    int steps{};
    double remainder{};
};

CadenceResult SimulateDynamicCadence(int fps, int seconds,
                                     double initialRemainder = 0.0) {
    CadenceResult result{0, initialRemainder};
    const double delta = nioh1fix::kShotDeltaUnitsPerSecond / double(fps);
    for (int call = 0; call < fps * seconds; ++call)
        result.steps += nioh1fix::AdvanceProjectileCadence(
            result.remainder, delta, false);
    return result;
}
} // namespace

int main() {
    bool ok = true;
    ok &= Check(Near(nioh1fix::kProjectilePostureCadenceHz, 30.0),
                "projectile cadence target is not the validated 30 Hz value");
    for (const int fps : {45, 60, 135}) {
        const auto result = SimulateDynamicCadence(fps, 10);
        ok &= Check(result.steps == 299 || result.steps == 300,
                    "dynamic cadence did not produce 30 steps per second");
        ok &= Check(Near(result.steps + result.remainder, 300.0),
                    "dynamic cadence lost accumulated step progress");
    }

    double accumulator = 0.0;
    ok &= Check(nioh1fix::AdvanceProjectileCadence(
                    accumulator, 0.5, false) == 0,
                "first 60 Hz call was not skipped");
    ok &= Check(nioh1fix::AdvanceProjectileCadence(
                    accumulator, 0.5, false) == 1,
                "second 60 Hz call did not emit one posture step");

    const auto at45 = SimulateDynamicCadence(45, 1);
    const auto transitioned = SimulateDynamicCadence(135, 1, at45.remainder);
    const int transitionSteps = at45.steps + transitioned.steps;
    ok &= Check(transitionSteps == 59 || transitionSteps == 60,
                "45-to-135 Hz transition did not preserve cadence");
    ok &= Check(Near(transitionSteps + transitioned.remainder, 60.0),
                "45-to-135 Hz transition lost accumulated progress");

    accumulator = 0.25;
    int stockSteps = 0;
    for (int call = 0; call < 30; ++call)
        stockSteps += nioh1fix::AdvanceProjectileCadence(
            accumulator, 1.0, true);
    ok &= Check(stockSteps == 30,
                "stock 30 FPS profile no longer advances once per call");
    ok &= Check(Near(accumulator, 0.25),
                "stock 30 FPS profile changed the dynamic accumulator");

    const double beforeInvalid = accumulator;
    ok &= Check(nioh1fix::AdvanceProjectileCadence(
                    accumulator, std::numeric_limits<double>::quiet_NaN(), false) == 1,
                "invalid delta no longer preserves the original call");
    ok &= Check(Near(accumulator, beforeInvalid),
                "invalid delta changed the dynamic accumulator");

    if (ok) {
        std::cout << "All projectile timing tests passed.\n";
        return 0;
    }
    return 1;
}
