#pragma once

#include "core.hpp"
#include "diagnostic_types.hpp"
#include <windows.h>
#ifdef _MSC_VER
#include <intrin.h>
#endif

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>

#ifdef _MSC_VER
#define NIOH1FIX_RETURN_ADDRESS() _ReturnAddress()
#else
#define NIOH1FIX_RETURN_ADDRESS() __builtin_return_address(0)
#endif

namespace nioh1fix::runtime {
inline constexpr wchar_t kSupportedExe[] = L"nioh.exe";
inline constexpr DWORD kSupportedTimestamp = 0x6307ABD5;
inline constexpr DWORD kSupportedImageSize = 0x0306E000;
inline constexpr int kInternalTargetFps = 120;
inline constexpr DWORD kMonitorIntervalMs = 250;
inline constexpr DWORD kMonitorDurationMs = 120'000;
inline constexpr DWORD kDiagnosticsIntervalMs = 2'000;
inline constexpr std::size_t kHookCount = 10;
inline constexpr std::size_t kTimingCounterCount = 11;

struct PeImage { std::uint8_t* base{}; IMAGE_NT_HEADERS64* headers{}; };
struct SearchResult { std::uint8_t* address{}; std::size_t count{}; };
enum class PatchStatus { pending, installed, unavailable };
enum class ResolveStatus { pending, compatible, incompatible };
struct PatchRecord {
    std::uint8_t* address{};
    std::array<std::uint8_t, 96> original{}, applied{};
    std::size_t size{};
};
struct HookSpec {
    BytePattern signature;
    std::uint16_t blockOffset{}, blockSize{}, stubOffset{};
    std::int8_t counterIndex{-1};
    std::array<std::uint8_t, 2> xmm{};
    std::uint8_t xmmCount{};
    const char* name{};
    const char* success{};
};
struct HookState { PatchStatus status{}; PatchRecord patch{}; };
struct HookResources { std::uint8_t* code{}; std::uint8_t* data{}; };
using InputUpdateFunction = void (*)(void*);
using TextScrollUpdateFunction = bool (*)(void*);
using MemoryCopyFunction = void* (*)(void*, const void*, std::size_t);
struct CompatibilityPlan {
    std::uint8_t* gameplay{}, *limiter{}, *present{}, *table{}, *inputCall{},
        *textScroll{};
    InputUpdateFunction inputTarget{};
    std::array<std::uint8_t*, 3> motionCalls{};
    std::array<std::uint8_t*, kHookCount> hookBlocks{};
    std::array<std::uint8_t*, 2> matrixCopyCalls{};
    MemoryCopyFunction memoryCopyTarget{};
    volatile LONG* activeProfile{}; bool knownBuild{};
};
struct PatchSet {
    PatchRecord gameplay{}, limiter{}, present{}, input{}, textScroll{};
    std::array<PatchRecord, 2> matrixCopies{};
    std::array<PatchRecord, 3> motion{};
    PatchStatus motionStatus{}, inputStatus{}, textScrollStatus{},
        matrixDiagnosticsStatus{};
    std::array<HookState, kHookCount> hooks{};
    HookResources resources{};
};
struct State {
    HMODULE module{};
    volatile LONG* activeProfile{};
    std::ofstream log;
    LARGE_INTEGER frequency{};
    TimingScaleState timing{};
    volatile LONG timingScaleBits{0x3F800000};
    volatile LONG* timingData{};
    volatile LONG presentCalls{}, presentWouldBlock{}, presentFailures{};
    volatile LONG64 presentTicks{}, previousPresentTick{}, lastPresentInterval{};
    volatile LONG motionCalls{}, inputUpdates{}, inputAccepted{}, inputSkipped{};
    std::array<volatile LONG, 3> motionPathCalls{};
    std::array<const void*, 3> motionReturnAddresses{};
    std::array<AccessorCallerSample, kAccessorCallerSlots> accessorCallers{};
    volatile LONG animationDiagnosticsActive{};
    volatile LONG accessorCallerOverflow{};
    std::uint8_t* imageBase{};
    std::size_t imageSize{};
    LONG64 previousInputTick{};
    double inputAccumulator{};
    InputUpdateFunction originalInputUpdate{};
    TextScrollUpdateFunction originalTextScrollUpdate{};
};
extern State g;

void Log(const std::string& message);
std::filesystem::path ModulePath(HMODULE module);
bool EqualsIgnoreCase(const std::wstring& left, const std::wstring& right);
bool ReadIniBool(const std::filesystem::path& path, const wchar_t* section,
                 const wchar_t* key, bool fallback);
PeImage ReadPeImage(HMODULE module);
SearchResult FindCode(const PeImage& image, BytePattern pattern);
bool IsImageRange(const PeImage& image, const void* address,
                  std::size_t size, DWORD characteristics);
std::uint8_t* DecodeRelative(std::uint8_t* instruction,
                             std::size_t displacementOffset,
                             std::size_t instructionSize);
bool PatchCode(std::uint8_t* address, std::span<const std::uint8_t> expected,
               std::span<const std::uint8_t> replacement, const char* name);
bool WriteExecutable(std::uint8_t* address, std::span<const std::uint8_t> bytes);
bool IsReachable(const void* end, const void* destination);
bool IsApplied(const PatchRecord& patch);

float ReadTimingScale();
LONG Counter(std::size_t index);
void RecordAccessorCaller(const void* returnAddress);
void RecordMotionCaller(const void* returnAddress);
void InitializeMotionDiagnostics(const CompatibilityPlan& plan);
std::string CollectAnimationDiagnostics();
bool IsThirtyFpsProfile();
float GetNormalizedMotionDelta();
void NormalizedInputUpdate(void* manager);
bool NormalizedTextScrollUpdate(void* controller);
HRESULT AggressivePresent(void* renderer, const std::uint8_t* config);
float GetGameplayReferenceFps();
void LogDiagnostics(std::uint8_t* table, DWORD elapsed);

bool EnsureHookResources(const PeImage& image, HookResources& resources);
PatchStatus InstallBlockHook(const PeImage& image, const HookSpec& spec,
                             std::uint8_t* block,
                             HookState& state, HookResources& resources);
PatchStatus InstallMotionHooks(const PeImage& image,
                               const CompatibilityPlan& plan, PatchSet& patches);
PatchStatus InstallInputHook(const PeImage& image,
                             const CompatibilityPlan& plan, PatchSet& patches);
PatchStatus InstallTextScrollHook(const PeImage& image,
    const CompatibilityPlan& plan, PatchSet& patches);
bool ValidateInputTarget(const PeImage& image, std::uint8_t* target);
ResolveStatus ResolveCompatibility(const PeImage& image, CompatibilityPlan& plan);
bool InstallCore(const PeImage& image, const CompatibilityPlan& plan,
                 PatchSet& patches, DWORD elapsed);
bool SetFrameProfiles(std::uint8_t* table, bool enable);
bool Monitor(const PeImage& image);
DWORD WINAPI MainThread(void*);
} // namespace nioh1fix::runtime
