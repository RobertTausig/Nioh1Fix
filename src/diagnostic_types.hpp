#pragma once

#include <windows.h>

#include <array>
#include <cstddef>

namespace nioh1fix::runtime {
inline constexpr std::size_t kAccessorCallerSlots = 256;
inline constexpr std::size_t kMatrixStreamSlots = 1024;

struct AccessorCallerSample {
    volatile LONG rva{}, calls{};
    LONG previousCalls{};
};

struct MatrixStreamSample {
    volatile LONG64 source{};
    volatile LONG lastPresent{}, hashValid{};
    volatile LONG64 hash{};
    volatile LONG samples{}, changed{}, repeated{}, matrixCount{};
    LONG previousSamples{}, previousChanged{}, previousRepeated{};
};

struct MatrixStreamDiagnostics {
    std::array<MatrixStreamSample, kMatrixStreamSlots> streams{};
    volatile LONG samples{}, changed{}, repeated{}, overflow{};
};
} // namespace nioh1fix::runtime
