#pragma once

#include "runtime.hpp"

namespace nioh1fix::runtime {
struct CallbackDiagnosticOptions {
    const void* callback{};
    std::array<std::uint8_t, 4> preservedXmm{};
    std::uint8_t preservedXmmCount{};
    bool stackAlignedAfterBlock{true};
    const char* failure{};
    const char* success{};
};

PatchStatus InstallCallbackDiagnostic(const PeImage& image,
    const HookSpec& spec, std::uint8_t* block, HookState& state,
    HookResources& resources, const CallbackDiagnosticOptions& options);
} // namespace nioh1fix::runtime
