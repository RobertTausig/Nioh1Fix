#include "runtime.hpp"
#include "matrix_diagnostics.hpp"

#include <algorithm>
#include <cstring>
#include <sstream>
#include <vector>
namespace nioh1fix::runtime {
namespace {
MemoryCopyFunction originalMemoryCopy{};
std::array<MatrixStreamDiagnostics, 2> matrixDiagnostics{};

void RecordMatrixStream(std::size_t category, const void* source,
                        std::size_t bytes) {
    if (!g.animationDiagnosticsActive || !source || bytes < 64 ||
        bytes % 64 || bytes > 64 * 2048) return;
    auto& diagnostics = matrixDiagnostics[category];
    const LONG64 key = static_cast<LONG64>(
        reinterpret_cast<std::uintptr_t>(source));
    std::size_t slot = (std::size_t(key >> 4) * 2654435761U) &
                       (kMatrixStreamSlots - 1);
    MatrixStreamSample* sample{};
    for (std::size_t probe = 0; probe < kMatrixStreamSlots; ++probe) {
        auto& candidate = diagnostics.streams[slot];
        const LONG64 found = InterlockedCompareExchange64(
            &candidate.source, key, 0);
        if (!found || found == key) { sample = &candidate; break; }
        slot = (slot + 1) & (kMatrixStreamSlots - 1);
    }
    if (!sample) {
        InterlockedIncrement(&diagnostics.overflow); return;
    }
    const LONG present = InterlockedCompareExchange(&g.presentCalls, 0, 0) + 1;
    if (InterlockedExchange(&sample->lastPresent, present) == present) return;

    std::uint64_t hash = 1469598103934665603ULL;
    const auto* data = static_cast<const std::uint8_t*>(source);
    for (std::size_t i = 0; i < bytes; ++i)
        hash = (hash ^ data[i]) * 1099511628211ULL;
    const LONG64 previous = InterlockedExchange64(
        &sample->hash, static_cast<LONG64>(hash));
    InterlockedExchange(&sample->matrixCount, static_cast<LONG>(bytes / 64));
    InterlockedIncrement(&sample->samples);
    InterlockedIncrement(&diagnostics.samples);
    if (InterlockedExchange(&sample->hashValid, 1)) {
        if (previous == static_cast<LONG64>(hash)) {
            InterlockedIncrement(&sample->repeated);
            InterlockedIncrement(&diagnostics.repeated);
        } else {
            InterlockedIncrement(&sample->changed);
            InterlockedIncrement(&diagnostics.changed);
        }
    }
}
void* MatrixCopy(std::size_t category, void* destination,
                 const void* source, std::size_t bytes) {
    RecordMatrixStream(category, source, bytes);
    return originalMemoryCopy ? originalMemoryCopy(destination, source, bytes)
                              : destination;
}
bool PatchCall(std::uint8_t* call, const void* destination,
               PatchRecord& record, const char* name) {
    if (call[0] != 0xE8 || !IsReachable(call + 5, destination)) return false;
    std::array<std::uint8_t, 5> applied{0xE8, 0, 0, 0, 0};
    const auto relative = std::int32_t(reinterpret_cast<std::intptr_t>(destination) -
                                       reinterpret_cast<std::intptr_t>(call + 5));
    std::memcpy(applied.data() + 1, &relative, sizeof(relative));
    record.address = call; record.size = applied.size();
    std::memcpy(record.original.data(), call, applied.size());
    std::memcpy(record.applied.data(), applied.data(), applied.size());
    return PatchCode(call, {record.original.data(), record.size},
                     {record.applied.data(), record.size}, name);
}

std::string StreamSummary(MatrixStreamDiagnostics& diagnostics) {
    struct Activity { std::size_t slot{}; LONG matrices{}, samples{}, changed{}, repeated{}; };
    std::vector<Activity> active;
    for (std::size_t i = 0; i < diagnostics.streams.size(); ++i) {
        auto& stream = diagnostics.streams[i];
        const LONG samples = InterlockedCompareExchange(&stream.samples, 0, 0);
        const LONG changed = InterlockedCompareExchange(&stream.changed, 0, 0);
        const LONG repeated = InterlockedCompareExchange(&stream.repeated, 0, 0);
        const LONG delta = samples - stream.previousSamples;
        if (delta > 0) active.push_back({i, stream.matrixCount, delta,
            changed - stream.previousChanged, repeated - stream.previousRepeated});
        stream.previousSamples = samples;
        stream.previousChanged = changed;
        stream.previousRepeated = repeated;
    }
    std::ranges::sort(active, [](const Activity& left, const Activity& right) {
        return left.samples > right.samples;
    });
    std::ostringstream out;
    const std::size_t count = std::min<std::size_t>(active.size(), 8);
    if (!count) return "none";
    for (std::size_t i = 0; i < count; ++i) {
        if (i) out << '/';
        out << active[i].slot << '@' << active[i].matrices << ':'
            << active[i].samples << ',' << active[i].changed << ','
            << active[i].repeated;
    }
    return out.str();
}
} // namespace

void* DiagnosticModelMatrixCopy(void* destination, const void* source,
                                std::size_t bytes) {
    return MatrixCopy(0, destination, source, bytes);
}
void* DiagnosticClothMatrixCopy(void* destination, const void* source,
                                std::size_t bytes) {
    return MatrixCopy(1, destination, source, bytes);
}

std::string CollectMatrixDiagnostics() {
    auto& model = matrixDiagnostics[0]; auto& cloth = matrixDiagnostics[1];
    std::ostringstream out;
    out << ", model_matrix_samples=" << model.samples
        << ", model_matrix_changed=" << model.changed
        << ", model_matrix_repeated=" << model.repeated
        << ", model_matrix_top_streams=" << StreamSummary(model)
        << ", model_matrix_overflow=" << model.overflow
        << ", cloth_matrix_samples=" << cloth.samples
        << ", cloth_matrix_changed=" << cloth.changed
        << ", cloth_matrix_repeated=" << cloth.repeated
        << ", cloth_matrix_top_streams=" << StreamSummary(cloth)
        << ", cloth_matrix_overflow=" << cloth.overflow;
    return out.str();
}

PatchStatus InstallMatrixDiagnostics(const PeImage& image,
    const CompatibilityPlan& plan, PatchSet& patches) {
    if (!EnsureHookResources(image, patches.resources)) return PatchStatus::unavailable;
    originalMemoryCopy = plan.memoryCopyTarget;
    constexpr std::array<const char*, 2> names{
        "the model-matrix diagnostic copy", "the cloth-matrix diagnostic copy"};
    for (std::size_t i = 0; i < plan.matrixCopyCalls.size(); ++i)
        if (!PatchCall(plan.matrixCopyCalls[i], patches.resources.code + 48 + i * 16,
                       patches.matrixCopies[i], names[i])) {
            for (std::size_t j = 0; j < i; ++j) PatchCode(
                patches.matrixCopies[j].address,
                {patches.matrixCopies[j].applied.data(), patches.matrixCopies[j].size},
                {patches.matrixCopies[j].original.data(), patches.matrixCopies[j].size},
                "a matrix-diagnostics rollback");
            originalMemoryCopy = nullptr; return PatchStatus::unavailable;
        }
    Log("Instrumented completed model and cloth matrix copies into render commands.");
    return PatchStatus::installed;
}
} // namespace nioh1fix::runtime
