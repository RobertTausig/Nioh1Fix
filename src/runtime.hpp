#pragma once

#include "core.hpp"
#include <windows.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>

namespace nioh1fix::runtime {
inline constexpr wchar_t kSupportedExe[] = L"nioh.exe";
inline constexpr DWORD kSupportedTimestamp = 0x6307ABD5;
inline constexpr DWORD kSupportedImageSize = 0x0306E000;
inline constexpr int kInternalTargetFps = 120;
inline constexpr DWORD kMonitorIntervalMs = 250;
inline constexpr DWORD kMonitorDurationMs = 30'000;
inline constexpr DWORD kDiagnosticsIntervalMs = 2'000;
inline constexpr std::size_t kActiveProfileRva = 0x01BB01E8;
inline constexpr std::size_t kFrameControllerRva = 0x019301D0;
inline constexpr std::size_t kCurrentFrameOffset = 0xC0;
inline constexpr std::size_t kCompletedFrameOffset = 0xC4;

struct PeImage { std::uint8_t* base{}; IMAGE_NT_HEADERS64* headers{}; };
struct SearchResult { std::uint8_t* address{}; std::size_t count{}; };
enum class PatchStatus { pending, installed, unavailable };
struct PatchRecord {
    std::uint8_t* address{};
    std::array<std::uint8_t, 96> original{}, applied{};
    std::size_t size{};
};
struct HookSpec {
    std::span<const std::uint8_t> signature;
    std::uint16_t blockOffset{}, blockSize{}, stubOffset{};
    std::int8_t counterIndex{-1};
    std::array<std::uint8_t, 2> xmm{};
    std::uint8_t xmmCount{};
    const char* name{};
    const char* success{};
};
struct HookState { PatchStatus status{}; PatchRecord patch{}; };
struct HookResources { std::uint8_t* code{}; std::uint8_t* data{}; };
struct PatchSet {
    PatchRecord gameplay{}, limiter{}, present{}, input{};
    std::array<PatchRecord, 3> motion{};
    PatchStatus motionStatus{}, inputStatus{};
    std::array<HookState, 8> hooks{};
    HookResources resources{};
};
using InputUpdateFunction = void (*)(void*);
struct State {
    HMODULE module{};
    std::uint8_t* imageBase{};
    std::ofstream log;
    LARGE_INTEGER frequency{};
    TimingScaleState timing{};
    volatile LONG timingScaleBits{0x3F800000};
    volatile LONG* timingData{};
    volatile LONG presentCalls{}, presentWouldBlock{}, presentFailures{};
    volatile LONG64 presentTicks{}, previousPresentTick{}, lastPresentInterval{};
    volatile LONG motionCalls{}, inputUpdates{}, inputAccepted{}, inputSkipped{};
    LONG64 previousInputTick{};
    double inputAccumulator{};
    InputUpdateFunction originalInputUpdate{};
};
extern State g;

void Log(const std::string& message);
std::filesystem::path ModulePath(HMODULE module);
bool EqualsIgnoreCase(const std::wstring& left, const std::wstring& right);
bool ReadIniBool(const std::filesystem::path& path, const wchar_t* section,
                 const wchar_t* key, bool fallback);
PeImage ReadPeImage(HMODULE module);
SearchResult FindCode(const PeImage& image, std::span<const std::uint8_t> pattern);
std::uint8_t* FindFrameProfiles(const PeImage& image);
bool PatchCode(std::uint8_t* address, std::span<const std::uint8_t> expected,
               std::span<const std::uint8_t> replacement, const char* name);
bool WriteExecutable(std::uint8_t* address, std::span<const std::uint8_t> bytes);
bool IsReachable(const void* end, const void* destination);
bool IsApplied(const PatchRecord& patch);

float ReadTimingScale();
LONG Counter(std::size_t index);
bool IsThirtyFpsProfile();
float GetNormalizedMotionDelta();
void NormalizedInputUpdate(void* manager);
HRESULT AggressivePresent(void* renderer, const std::uint8_t* config);
float GetGameplayReferenceFps();
void LogDiagnostics(std::uint8_t* table, DWORD elapsed);

bool EnsureHookResources(const PeImage& image, HookResources& resources);
PatchStatus InstallBlockHook(const PeImage& image, const HookSpec& spec,
                             HookState& state, HookResources& resources);
PatchStatus InstallMotionHooks(const PeImage& image, PatchSet& patches);
PatchStatus InstallInputHook(const PeImage& image, PatchSet& patches);
bool InstallGameplayHook(const PeImage& image, PatchRecord& patch);
bool InstallLimiter(const PeImage& image, PatchRecord& patch);
bool InstallPresentHook(const PeImage& image, PatchRecord& patch, DWORD elapsed);
bool ApplyFrameProfiles(std::uint8_t* table);
bool Monitor(const PeImage& image, std::uint8_t* table);
DWORD WINAPI MainThread(void*);
} // namespace nioh1fix::runtime
