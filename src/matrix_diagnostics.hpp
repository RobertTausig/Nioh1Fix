#pragma once

#include <cstddef>
#include <string>

namespace nioh1fix::runtime {
struct CompatibilityPlan;
struct PatchSet;
struct PeImage;
enum class PatchStatus;

std::string CollectMatrixDiagnostics();
void* DiagnosticModelMatrixCopy(void*, const void*, std::size_t);
void* DiagnosticClothMatrixCopy(void*, const void*, std::size_t);
PatchStatus InstallMatrixDiagnostics(const PeImage&, const CompatibilityPlan&,
                                     PatchSet&);
} // namespace nioh1fix::runtime
