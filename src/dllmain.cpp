#include "frame_profile.hpp"
#include "timing_scale.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cwctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <span>
#include <sstream>
#include <string>

namespace
{
constexpr wchar_t kSupportedExe[] = L"nioh.exe";
constexpr DWORD kSupportedTimestamp = 0x6307ABD5;
constexpr DWORD kSupportedImageSize = 0x0306E000;
constexpr int kDefaultTargetFps = 120;
constexpr int kMinTargetFps = 60;
constexpr int kMaxTargetFps = 360;
constexpr DWORD kMonitorIntervalMs = 250;
constexpr DWORD kMonitorDurationMs = 30'000;
constexpr DWORD kDiagnosticsIntervalMs = 2'000;
constexpr std::size_t kActiveFrameProfileRva = 0x01BB01E8;
constexpr std::size_t kFrameControllerRva = 0x019301D0;
constexpr std::size_t kCurrentFrameCountOffset = 0xC0;
constexpr std::size_t kCompletedFrameCountOffset = 0xC4;
constexpr LONG kFrameProfileCount = 4;
constexpr std::size_t kFrameProfileSize = 12;
constexpr std::array<std::uint8_t, 56> kPresentSignature{
    0x80, 0x7D, 0x00, 0x00, 0x74, 0x18, 0x48, 0x8B, 0x4D, 0x08, 0x33, 0xD2,
    0x48, 0x85, 0xC9, 0x75, 0x1A, 0x48, 0x8B, 0x8F, 0xB0, 0x2F, 0x00, 0x00,
    0x44, 0x8D, 0x42, 0x01, 0xEB, 0x10, 0x48, 0x8B, 0x8F, 0xB0, 0x2F, 0x00,
    0x00, 0x8B, 0x97, 0x8C, 0x2F, 0x00, 0x00, 0x45, 0x33, 0xC0, 0x48, 0x8B,
    0x01, 0xFF, 0x50, 0x40, 0x48, 0x8B, 0x4F, 0x30,
};
constexpr UINT kDxgiPresentTest = 0x1;
constexpr UINT kDxgiPresentDoNotWait = 0x8;
constexpr HRESULT kDxgiErrorWasStillDrawing =
    static_cast<HRESULT>(0x887A000AUL);
constexpr std::size_t kRendererSwapChainOffset = 0x2FB0;
constexpr std::array<std::uint8_t, 44> kMainFrameLimiterSignature{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10,
    0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20,
    0x41, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x80, 0x39, 0x00, 0x48,
    0x8B, 0xF9, 0x48, 0x63, 0xEA, 0x75, 0x31, 0x8B, 0x41, 0x04,
    0x85, 0xC0, 0x74, 0x13,
};
constexpr std::array<std::uint8_t, 6> kMainFrameLimiterPatch{
    0xC3, 0x90, 0x90, 0x90, 0x90, 0x90,
};
constexpr std::array<std::uint8_t, 24> kGameplayFpsAccessorSignature{
    0x48, 0x63, 0x05, 0x91, 0x30, 0xD3, 0x00, 0x48,
    0x8D, 0x0C, 0x40, 0x48, 0x8D, 0x05, 0x76, 0xD7,
    0x92, 0x00, 0xF3, 0x0F, 0x10, 0x04, 0x88, 0xC3,
};
constexpr std::array<std::uint8_t, 45> kMotionSlotsSignature{
    0x48, 0x8B, 0x4C, 0x1F, 0x08, 0xFF, 0x50, 0x48, 0x48, 0x8B, 0xC8,
    0x48, 0x8D, 0x54, 0x24, 0x20, 0xE8, 0xEB, 0xD2, 0x87, 0xFF, 0xE8,
    0x36, 0x18, 0x52, 0x00, 0x48, 0x8B, 0x44, 0x1F, 0x10, 0x0F, 0x28,
    0xC8, 0x48, 0x8B, 0x4C, 0x1F, 0x08, 0xFF, 0x90, 0x40, 0x01, 0x00,
    0x00,
};
constexpr std::array<std::uint8_t, 72> kLinkedMotionSignature{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x83, 0xB9, 0x70, 0x01,
    0x00, 0x00, 0x00, 0x48, 0x8B, 0xD9, 0x0F, 0x29, 0x7C, 0x24, 0x20,
    0x0F, 0x28, 0xF9, 0x74, 0x49, 0x0F, 0x29, 0x74, 0x24, 0x30, 0xE8,
    0x9B, 0x17, 0x52, 0x00, 0x48, 0x8B, 0x53, 0x18, 0x0F, 0x28, 0xF0,
    0x48, 0x8B, 0x83, 0x78, 0x01, 0x00, 0x00, 0x48, 0x8B, 0x8B, 0x70,
    0x01, 0x00, 0x00, 0xF3, 0x0F, 0x59, 0xF7, 0x48, 0x8B, 0x52, 0x08,
    0xFF, 0x90, 0x50, 0x01, 0x00, 0x00,
};
constexpr std::array<std::uint8_t, 33> kMotionComponentSignature{
    0x0F, 0x29, 0x78, 0xB8, 0x44, 0x0F, 0x29, 0x48, 0x98, 0x44, 0x0F,
    0x29, 0x50, 0x88, 0xE8, 0x6A, 0x06, 0x52, 0x00, 0x44, 0x0F, 0x28,
    0xD0, 0xF3, 0x45, 0x0F, 0x59, 0xD0, 0xE8, 0x9C, 0xDA, 0x06, 0x00,
};
constexpr std::array<std::uint8_t, 42> kInputUpdateSignature{
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B, 0x0D, 0x2D, 0xF3, 0xCA, 0x01,
    0x48, 0x85, 0xC9, 0x74, 0x15, 0xE8, 0x0B, 0x02, 0x00, 0x00, 0x48,
    0x8B, 0x0D, 0x1C, 0xF3, 0xCA, 0x01, 0x48, 0x83, 0xC4, 0x28, 0xE9,
    0x4B, 0xDA, 0xFF, 0xFF, 0x48, 0x83, 0xC4, 0x28, 0xC3,
};
constexpr std::array<std::uint8_t, 63> kCameraInputSignature{
    0x48, 0x8B, 0x0D, 0xEE, 0xBF, 0x04, 0x01, 0xF3, 0x44, 0x0F, 0x10,
    0x3D, 0xF5, 0x57, 0xD3, 0x00, 0x44, 0x39, 0xA1, 0xAC, 0x00, 0x00,
    0x00, 0x75, 0x05, 0xF3, 0x41, 0x0F, 0x59, 0xFF, 0x44, 0x39, 0xA1,
    0xB0, 0x00, 0x00, 0x00, 0x75, 0x05, 0xF3, 0x45, 0x0F, 0x59, 0xC7,
    0xF3, 0x45, 0x0F, 0x58, 0xD3, 0xF3, 0x45, 0x0F, 0x58, 0xCC, 0x8B,
    0x81, 0xB4, 0x00, 0x00, 0x00, 0x0F, 0x57, 0xC0,
};
constexpr std::array<std::uint8_t, 72> kAimCameraInputSignature{
    0xF3, 0x0F, 0x58, 0x9F, 0xB0, 0x00, 0x00, 0x00, 0xF3, 0x0F, 0x58, 0xA7,
    0x00, 0x01, 0x00, 0x00, 0xF3, 0x0F, 0x5E, 0x1D, 0x83, 0xA6, 0xD2, 0x00,
    0xF3, 0x0F, 0x5E, 0x25, 0x7B, 0xA6, 0xD2, 0x00, 0xF3, 0x0F, 0x58, 0xDE,
    0xF3, 0x0F, 0x58, 0xE7, 0xF3, 0x0F, 0x5A, 0xC4, 0x0F, 0x54, 0xC5, 0x66,
    0x0F, 0x5A, 0xC0, 0x0F, 0x2F, 0xC1, 0x76, 0x35, 0xF3, 0x0F, 0x10, 0x0D,
    0x1B, 0x15, 0xF1, 0x00, 0xF3, 0x0F, 0x59, 0x8F, 0x90, 0x01, 0x00, 0x00,
};
constexpr std::array<std::uint8_t, 78> kGrassWindSignature{
    0x48, 0x8B, 0x87, 0x08, 0x04, 0x00, 0x00, 0x48, 0x8D, 0x8F, 0xC0, 0x2E,
    0x18, 0x00, 0x4C, 0x8D, 0xA8, 0x00, 0x05, 0x00, 0x00, 0x48, 0x85, 0xC0,
    0x75, 0x07, 0x4C, 0x8D, 0x2D, 0x36, 0xD9, 0xF8, 0x00, 0xF3, 0x0F, 0x10,
    0x71, 0x34, 0xF3, 0x0F, 0x10, 0x79, 0x30, 0xE8, 0x97, 0xC6, 0xA0, 0xFF,
    0x4C, 0x8B, 0xC0, 0x0F, 0x28, 0xDE, 0x0F, 0x28, 0xCF, 0x49, 0x8B, 0xCD,
    0xE8, 0xF6, 0xBE, 0xA2, 0xFF, 0x48, 0x8B, 0xB7, 0x08, 0x04, 0x00, 0x00,
    0x41, 0xBE, 0x00, 0x00, 0x00, 0x00,
};
constexpr std::size_t kMotionSlotsCallOffset = 21;
constexpr std::size_t kLinkedMotionCallOffset = 32;
constexpr std::size_t kMotionComponentCallOffset = 14;
constexpr std::size_t kInputUpdateCallOffset = 16;
constexpr std::size_t kCameraScaleBlockOffset = 44;
constexpr std::size_t kCameraScaleBlockSize = 10;
constexpr std::size_t kAimCameraScaleBlockOffset = 32;
constexpr std::size_t kAimCameraScaleBlockSize = 8;
constexpr std::size_t kGrassWindScaleBlockOffset = 51;
constexpr std::size_t kGrassWindScaleBlockSize = 9;
constexpr std::size_t kInputPlayerCount = 4;
constexpr std::size_t kInputPlayerStride = 0x1C0;
constexpr std::size_t kInputPressedMaskOffset = 0x30;
constexpr std::size_t kInputReleasedMaskOffset = 0x34;
constexpr std::size_t kInputRepeatMaskOffset = 0x38;
constexpr double kInputCadenceFps = 60.0;
constexpr double kInputStallSeconds = 0.1;

HMODULE gThisModule{};
std::uint8_t* gImageBase{};
std::ofstream gLog;
volatile LONG gPresentCallCount{};
volatile LONG gPresentWouldBlockCount{};
volatile LONG gPresentFailureCount{};
volatile LONG64 gPresentTotalTicks{};
volatile LONG64 gPreviousPresentTick{};
volatile LONG64 gLastPresentIntervalTicks{};
LARGE_INTEGER gPerformanceFrequency{};
int gConfiguredTargetFps{kDefaultTargetFps};
nioh1fix::TimingScaleState gTimingScaleState{};
volatile LONG gTimingScaleBits{0x3F800000};
volatile LONG* gCameraScaleBits{};
volatile LONG* gAimCameraCallCount{};
volatile LONG* gGrassWindCallCount{};
volatile LONG gMotionDeltaCallCount{};
volatile LONG gInputUpdateCallCount{};
volatile LONG gInputAcceptedCount{};
volatile LONG gInputSkippedCount{};
LONG64 gPreviousInputTick{};
double gInputCadenceAccumulatorTicks{};

using InputUpdateFunction = void (*)(void*);
InputUpdateFunction gOriginalInputUpdate{};

void Log(const std::string& message)
{
    if (gLog.is_open()) {
        gLog << message << '\n';
        gLog.flush();
    }
    OutputDebugStringA((std::string("Nioh1Fix: ") + message + "\n").c_str());
}

std::filesystem::path GetModulePath(HMODULE module)
{
    std::wstring buffer(32768, L'\0');
    const DWORD length =
        GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return {};
    }
    buffer.resize(length);
    return std::filesystem::path(buffer);
}

bool EqualsIgnoreCase(const std::wstring& left, const std::wstring& right)
{
    return left.size() == right.size() &&
           std::equal(left.begin(), left.end(), right.begin(),
                      [](wchar_t a, wchar_t b) {
                          return towlower(a) == towlower(b);
                      });
}

bool ReadIniBool(const std::filesystem::path& path,
                 const wchar_t* section,
                 const wchar_t* key,
                 bool defaultValue)
{
    wchar_t value[32]{};
    const wchar_t* fallback = defaultValue ? L"true" : L"false";
    GetPrivateProfileStringW(
        section, key, fallback, value, static_cast<DWORD>(std::size(value)), path.c_str());

    std::wstring normalized(value);
    normalized.erase(
        normalized.begin(),
        std::find_if(normalized.begin(), normalized.end(),
                     [](wchar_t character) { return !iswspace(character); }));
    normalized.erase(
        std::find_if(normalized.rbegin(), normalized.rend(),
                     [](wchar_t character) { return !iswspace(character); })
            .base(),
        normalized.end());
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](wchar_t character) { return towlower(character); });

    if (normalized == L"true" || normalized == L"yes" || normalized == L"1" ||
        normalized == L"on") {
        return true;
    }
    if (normalized == L"false" || normalized == L"no" || normalized == L"0" ||
        normalized == L"off") {
        return false;
    }
    return defaultValue;
}

struct PeImage
{
    std::uint8_t* base{};
    IMAGE_NT_HEADERS64* headers{};
};

PeImage ReadPeImage(HMODULE module)
{
    auto* base = reinterpret_cast<std::uint8_t*>(module);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) {
        return {};
    }

    auto* headers = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (headers->Signature != IMAGE_NT_SIGNATURE ||
        headers->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        headers->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        return {};
    }
    return {base, headers};
}

std::uint8_t* FindUniqueFrameProfileTable(const PeImage& image)
{
    std::uint8_t* match{};
    std::size_t matchCount{};
    auto* section = IMAGE_FIRST_SECTION(image.headers);
    const auto imageSize = image.headers->OptionalHeader.SizeOfImage;

    for (WORD index = 0; index < image.headers->FileHeader.NumberOfSections;
         ++index, ++section) {
        const DWORD required = IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ;
        if ((section->Characteristics & required) != required) {
            continue;
        }

        const std::size_t rva = section->VirtualAddress;
        const std::size_t size = section->Misc.VirtualSize;
        if (rva >= imageSize || size > imageSize - rva) {
            Log("Rejected malformed PE section bounds.");
            return nullptr;
        }

        const auto result = nioh1fix::FindFrameProfileTable(
            std::span<const std::uint8_t>(image.base + rva, size));
        if (result.status == nioh1fix::MatchStatus::ambiguous) {
            Log("Frame profile signature was ambiguous inside a PE section.");
            return nullptr;
        }
        if (result.status == nioh1fix::MatchStatus::unique) {
            match = image.base + rva + result.offset;
            matchCount += result.count;
        }
    }

    if (matchCount != 1) {
        std::ostringstream message;
        message << "Expected one frame profile table, found " << matchCount << '.';
        Log(message.str());
        return nullptr;
    }
    return match;
}

struct PatternSearchResult
{
    std::uint8_t* address{};
    std::size_t count{};
};

PatternSearchResult FindCodePattern(const PeImage& image,
                                    std::span<const std::uint8_t> pattern)
{
    PatternSearchResult result{};
    auto* section = IMAGE_FIRST_SECTION(image.headers);
    const auto imageSize = image.headers->OptionalHeader.SizeOfImage;

    for (WORD index = 0; index < image.headers->FileHeader.NumberOfSections;
         ++index, ++section) {
        const DWORD required = IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE;
        if ((section->Characteristics & required) != required) {
            continue;
        }

        const std::size_t rva = section->VirtualAddress;
        const std::size_t size = section->Misc.VirtualSize;
        if (rva >= imageSize || size > imageSize - rva || size < pattern.size()) {
            continue;
        }

        auto* bytes = image.base + rva;
        const auto lastStart = size - pattern.size();
        for (std::size_t offset = 0; offset <= lastStart; ++offset) {
            if (std::memcmp(bytes + offset, pattern.data(), pattern.size()) != 0) {
                continue;
            }
            result.address = bytes + offset;
            ++result.count;
            if (result.count > 1) {
                return result;
            }
        }
    }
    return result;
}

LONG FloatBits(float value)
{
    return std::bit_cast<LONG>(value);
}

float ReadTimingScale()
{
    const LONG bits =
        InterlockedCompareExchange(&gTimingScaleBits, 0, 0);
    return std::bit_cast<float>(bits);
}

LONG ReadHookCounter(volatile LONG* counter)
{
    return counter ? InterlockedCompareExchange(counter, 0, 0) : 0;
}

bool IsStockThirtyFpsProfile()
{
    if (!gImageBase) {
        return false;
    }
    const LONG activeProfile = *reinterpret_cast<volatile LONG*>(
        gImageBase + kActiveFrameProfileRva);
    return activeProfile == 1 || activeProfile == 2;
}

void PublishTimingScale(double scale)
{
    const float published = IsStockThirtyFpsProfile()
                                ? 1.0F
                                : static_cast<float>(scale);
    const LONG bits = FloatBits(published);
    InterlockedExchange(&gTimingScaleBits, bits);
    if (gCameraScaleBits) {
        InterlockedExchange(gCameraScaleBits, bits);
    }
}

float GetNormalizedMotionDelta()
{
    InterlockedIncrement(&gMotionDeltaCallCount);
    if (IsStockThirtyFpsProfile()) {
        return 1.0F / 30.0F;
    }

    // The validated player path requires a 2 * presentation-FPS timing
    // divisor. Motion components use its reciprocal.
    return ReadTimingScale() / 120.0F;
}

void ClearTransientInputMasks(void* inputManager)
{
    auto* bytes = static_cast<std::uint8_t*>(inputManager);
    for (std::size_t player = 0; player < kInputPlayerCount; ++player) {
        auto* block = bytes + player * kInputPlayerStride;
        *reinterpret_cast<std::uint32_t*>(
            block + kInputPressedMaskOffset) = 0;
        *reinterpret_cast<std::uint32_t*>(
            block + kInputReleasedMaskOffset) = 0;
        *reinterpret_cast<std::uint32_t*>(
            block + kInputRepeatMaskOffset) = 0;
    }
}

bool ShouldRunInputUpdate()
{
    if (IsStockThirtyFpsProfile() ||
        gPerformanceFrequency.QuadPart <= 0) {
        gPreviousInputTick = 0;
        gInputCadenceAccumulatorTicks = 0.0;
        return true;
    }

    LARGE_INTEGER now{};
    if (!QueryPerformanceCounter(&now)) {
        return true;
    }
    if (gPreviousInputTick <= 0 || now.QuadPart <= gPreviousInputTick) {
        gPreviousInputTick = now.QuadPart;
        gInputCadenceAccumulatorTicks = 0.0;
        return true;
    }

    const LONG64 elapsedTicks = now.QuadPart - gPreviousInputTick;
    gPreviousInputTick = now.QuadPart;
    const double stallTicks =
        static_cast<double>(gPerformanceFrequency.QuadPart) *
        kInputStallSeconds;
    if (static_cast<double>(elapsedTicks) > stallTicks) {
        gInputCadenceAccumulatorTicks = 0.0;
        return true;
    }

    const double cadenceTicks =
        static_cast<double>(gPerformanceFrequency.QuadPart) /
        kInputCadenceFps;
    gInputCadenceAccumulatorTicks += static_cast<double>(elapsedTicks);
    if (gInputCadenceAccumulatorTicks < cadenceTicks) {
        return false;
    }

    gInputCadenceAccumulatorTicks =
        std::fmod(gInputCadenceAccumulatorTicks, cadenceTicks);
    return true;
}

void NormalizedInputUpdate(void* inputManager)
{
    InterlockedIncrement(&gInputUpdateCallCount);
    if (!gOriginalInputUpdate || ShouldRunInputUpdate()) {
        InterlockedIncrement(&gInputAcceptedCount);
        if (gOriginalInputUpdate) {
            gOriginalInputUpdate(inputManager);
        }
        return;
    }

    InterlockedIncrement(&gInputSkippedCount);
    ClearTransientInputMasks(inputManager);
}

HRESULT AggressivePresent(void* renderer, const std::uint8_t* presentConfig)
{
    auto* swapChain = *reinterpret_cast<void**>(
        static_cast<std::uint8_t*>(renderer) + kRendererSwapChainOffset);
    UINT flags = kDxgiPresentDoNotWait;

    if (*presentConfig != 0) {
        if (auto* alternateSwapChain =
                *reinterpret_cast<void* const*>(presentConfig + sizeof(void*))) {
            swapChain = alternateSwapChain;
        } else {
            flags = kDxgiPresentTest;
        }
    }

    using PresentFunction = HRESULT(STDMETHODCALLTYPE*)(void*, UINT, UINT);
    auto** vtable = *reinterpret_cast<void***>(swapChain);
    const auto present = reinterpret_cast<PresentFunction>(vtable[8]);
    LARGE_INTEGER start{};
    LARGE_INTEGER end{};
    QueryPerformanceCounter(&start);
    const LONG64 previousPresentTick =
        InterlockedExchange64(&gPreviousPresentTick, start.QuadPart);
    if (previousPresentTick > 0 && start.QuadPart > previousPresentTick) {
        const LONG64 intervalTicks = start.QuadPart - previousPresentTick;
        InterlockedExchange64(&gLastPresentIntervalTicks, intervalTicks);
        if (gPerformanceFrequency.QuadPart > 0) {
            const double intervalSeconds =
                static_cast<double>(intervalTicks) /
                static_cast<double>(gPerformanceFrequency.QuadPart);
            PublishTimingScale(nioh1fix::UpdateTimingScale(
                gTimingScaleState,
                intervalSeconds,
                static_cast<double>(gConfiguredTargetFps)));
        }
    }
    const HRESULT result = present(swapChain, 0, flags);
    QueryPerformanceCounter(&end);

    InterlockedIncrement(&gPresentCallCount);
    InterlockedAdd64(&gPresentTotalTicks, end.QuadPart - start.QuadPart);
    if (result == kDxgiErrorWasStillDrawing) {
        InterlockedIncrement(&gPresentWouldBlockCount);
    } else if (FAILED(result)) {
        InterlockedIncrement(&gPresentFailureCount);
    }
    return result;
}

float GetGameplayReferenceFps()
{
    const LONG activeProfile =
        *reinterpret_cast<volatile LONG*>(
            gImageBase + kActiveFrameProfileRva);
    if (activeProfile == 1 || activeProfile == 2) {
        return 30.0f;
    }

    const LONG64 intervalTicks = InterlockedCompareExchange64(
        &gLastPresentIntervalTicks, 0, 0);
    if (intervalTicks <= 0 || gPerformanceFrequency.QuadPart <= 0) {
        return static_cast<float>(gConfiguredTargetFps * 2);
    }

    const double measuredFps =
        static_cast<double>(gPerformanceFrequency.QuadPart) /
        static_cast<double>(intervalTicks);
    return static_cast<float>(
        std::clamp(measuredFps * 2.0, 30.0, 2000.0));
}

bool PatchCode(std::uint8_t* address,
               std::span<const std::uint8_t> original,
               std::span<const std::uint8_t> replacement,
               const char* description)
{
    if (original.size() != replacement.size()) {
        Log(std::string(description) + " patch has an invalid size.");
        return false;
    }
    if (std::memcmp(address, original.data(), original.size()) != 0) {
        Log(std::string(description) + " verification failed.");
        return false;
    }

    DWORD oldProtection{};
    if (!VirtualProtect(address,
                        replacement.size(),
                        PAGE_EXECUTE_READWRITE,
                        &oldProtection)) {
        Log(std::string("VirtualProtect failed before patching ") +
            description + '.');
        return false;
    }

    std::memcpy(address, replacement.data(), replacement.size());
    FlushInstructionCache(GetCurrentProcess(), address, replacement.size());

    DWORD ignored{};
    if (!VirtualProtect(address,
                        replacement.size(),
                        oldProtection,
                        &ignored)) {
        Log(std::string(description) +
            " was patched, but restoring page protection failed.");
        return false;
    }
    return true;
}

enum class OptionalPatchStatus
{
    pending,
    installed,
    unavailable,
};

struct RelativePatch
{
    std::uint8_t* address{};
    std::array<std::uint8_t, 5> original{};
    std::array<std::uint8_t, 5> bytes{};
};

struct CameraPatch
{
    std::uint8_t* address{};
    std::array<std::uint8_t, kCameraScaleBlockSize> bytes{};
};

struct GrassWindPatch
{
    std::uint8_t* address{};
    std::array<std::uint8_t, kGrassWindScaleBlockSize> bytes{};
};

struct AimCameraPatch
{
    std::uint8_t* address{};
    std::array<std::uint8_t, kAimCameraScaleBlockSize> bytes{};
};

struct TimingHookResources
{
    std::uint8_t* code{};
    std::uint8_t* data{};
    std::uint8_t* motionRelay{};
    std::uint8_t* inputRelay{};
    std::uint8_t* cameraStub{};
    std::uint8_t* grassWindStub{};
    std::uint8_t* aimCameraStub{};
};

struct OptionalTimingPatches
{
    OptionalPatchStatus animation{OptionalPatchStatus::pending};
    OptionalPatchStatus camera{OptionalPatchStatus::pending};
    OptionalPatchStatus aimCamera{OptionalPatchStatus::pending};
    OptionalPatchStatus vegetation{OptionalPatchStatus::pending};
    OptionalPatchStatus menu{OptionalPatchStatus::pending};
    TimingHookResources resources{};
    std::array<RelativePatch, 3> animationCalls{};
    RelativePatch inputCall{};
    CameraPatch cameraBlock{};
    GrassWindPatch grassWindBlock{};
    AimCameraPatch aimCameraBlock{};
};

bool IsRelativeReachable(const void* instructionEnd, const void* destination)
{
    const auto distance =
        reinterpret_cast<std::intptr_t>(destination) -
        reinterpret_cast<std::intptr_t>(instructionEnd);
    return distance >= INT32_MIN && distance <= INT32_MAX;
}

std::uint8_t* AllocateNearImage(const PeImage& image, SIZE_T size)
{
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    const auto granularity =
        static_cast<std::uintptr_t>(systemInfo.dwAllocationGranularity);
    const auto pageSize =
        static_cast<SIZE_T>(systemInfo.dwPageSize);
    const SIZE_T allocationSize =
        (size + pageSize - 1) & ~(pageSize - 1);
    const auto imageEnd =
        reinterpret_cast<std::uintptr_t>(image.base) +
        image.headers->OptionalHeader.SizeOfImage;
    std::uintptr_t cursor =
        (imageEnd + granularity - 1) & ~(granularity - 1);
    const std::uintptr_t limit =
        reinterpret_cast<std::uintptr_t>(image.base) + 0x60000000ULL;

    while (cursor < limit) {
        MEMORY_BASIC_INFORMATION region{};
        if (VirtualQuery(reinterpret_cast<void*>(cursor),
                         &region,
                         sizeof(region)) == 0) {
            break;
        }

        const auto regionBase =
            reinterpret_cast<std::uintptr_t>(region.BaseAddress);
        const auto regionEnd = regionBase + region.RegionSize;
        if (region.State == MEM_FREE) {
            const auto candidate =
                (regionBase + granularity - 1) & ~(granularity - 1);
            if (candidate >= cursor && candidate < regionEnd &&
                allocationSize <= regionEnd - candidate) {
                if (auto* memory = static_cast<std::uint8_t*>(VirtualAlloc(
                        reinterpret_cast<void*>(candidate),
                        allocationSize,
                        MEM_COMMIT | MEM_RESERVE,
                        PAGE_READWRITE))) {
                    return memory;
                }
            }
        }
        if (regionEnd <= cursor) {
            break;
        }
        cursor = regionEnd;
    }
    return nullptr;
}

bool WriteExecutableRegion(std::uint8_t* address,
                           std::span<const std::uint8_t> bytes)
{
    DWORD oldProtection{};
    if (!VirtualProtect(
            address, bytes.size(), PAGE_EXECUTE_READWRITE, &oldProtection)) {
        return false;
    }
    std::memcpy(address, bytes.data(), bytes.size());
    FlushInstructionCache(GetCurrentProcess(), address, bytes.size());
    DWORD ignored{};
    return VirtualProtect(
               address, bytes.size(), PAGE_EXECUTE_READ, &ignored) != FALSE;
}

std::array<std::uint8_t, 12> MakeAbsoluteJump(const void* destination)
{
    std::array<std::uint8_t, 12> bytes{
        0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xE0,
    };
    const auto target = reinterpret_cast<std::uintptr_t>(destination);
    std::memcpy(bytes.data() + 2, &target, sizeof(target));
    return bytes;
}

bool EnsureTimingHookResources(const PeImage& image,
                               TimingHookResources& resources)
{
    if (resources.code) {
        return true;
    }

    auto* code = AllocateNearImage(image, 4096);
    auto* data = AllocateNearImage(image, 4096);
    if (!code || !data) {
        if (code) {
            VirtualFree(code, 0, MEM_RELEASE);
        }
        if (data) {
            VirtualFree(data, 0, MEM_RELEASE);
        }
        Log("Could not reserve memory near nioh.exe for optional timing hooks.");
        return false;
    }

    resources.code = code;
    resources.data = data;
    resources.motionRelay = code;
    resources.inputRelay = code + 16;
    resources.cameraStub = code + 64;
    resources.grassWindStub = code + 128;
    resources.aimCameraStub = code + 192;

    const auto motionJump =
        MakeAbsoluteJump(reinterpret_cast<const void*>(&GetNormalizedMotionDelta));
    const auto inputJump =
        MakeAbsoluteJump(reinterpret_cast<const void*>(&NormalizedInputUpdate));
    std::memcpy(resources.motionRelay, motionJump.data(), motionJump.size());
    std::memcpy(resources.inputRelay, inputJump.data(), inputJump.size());
    *reinterpret_cast<LONG*>(resources.data) =
        FloatBits(ReadTimingScale());
    *reinterpret_cast<LONG*>(resources.data + sizeof(LONG)) = 0;
    *reinterpret_cast<LONG*>(resources.data + 2 * sizeof(LONG)) = 0;

    DWORD oldProtection{};
    if (!VirtualProtect(code, 4096, PAGE_EXECUTE_READ, &oldProtection)) {
        VirtualFree(code, 0, MEM_RELEASE);
        VirtualFree(data, 0, MEM_RELEASE);
        resources = {};
        Log("Could not make the optional timing relays executable.");
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), code, 32);
    gCameraScaleBits = reinterpret_cast<volatile LONG*>(data);
    gGrassWindCallCount =
        reinterpret_cast<volatile LONG*>(data + sizeof(LONG));
    gAimCameraCallCount =
        reinterpret_cast<volatile LONG*>(data + 2 * sizeof(LONG));
    return true;
}

bool BuildRelativePatch(std::uint8_t* address,
                        std::uint8_t opcode,
                        const void* destination,
                        std::array<std::uint8_t, 5>& replacement)
{
    if (address[0] != opcode ||
        !IsRelativeReachable(address + 5, destination)) {
        return false;
    }
    replacement[0] = opcode;
    const auto distance =
        reinterpret_cast<std::intptr_t>(destination) -
        reinterpret_cast<std::intptr_t>(address + 5);
    const auto relative = static_cast<std::int32_t>(distance);
    std::memcpy(replacement.data() + 1, &relative, sizeof(relative));
    return true;
}

bool PatchRelativeCall(std::uint8_t* address,
                       const void* destination,
                       const char* description,
                       RelativePatch& result)
{
    std::memcpy(result.original.data(), address, result.original.size());
    if (!BuildRelativePatch(address, 0xE8, destination, result.bytes)) {
        Log(std::string(description) +
            " call could not reach its verified relay.");
        return false;
    }
    if (!PatchCode(
            address, result.original, result.bytes, description)) {
        return false;
    }
    result.address = address;
    return true;
}

OptionalPatchStatus TryInstallAnimationTiming(
    const PeImage& image,
    OptionalTimingPatches& patches)
{
    const auto slots =
        FindCodePattern(image, std::span<const std::uint8_t>(kMotionSlotsSignature));
    const auto linked =
        FindCodePattern(image, std::span<const std::uint8_t>(kLinkedMotionSignature));
    const auto component = FindCodePattern(
        image, std::span<const std::uint8_t>(kMotionComponentSignature));
    if (slots.count > 1 || linked.count > 1 || component.count > 1) {
        Log("An animation timing signature was ambiguous; animation "
            "normalization was not installed.");
        return OptionalPatchStatus::unavailable;
    }
    if (slots.count == 0 || linked.count == 0 || component.count == 0) {
        return OptionalPatchStatus::pending;
    }
    if (!EnsureTimingHookResources(image, patches.resources)) {
        return OptionalPatchStatus::unavailable;
    }

    std::array<std::uint8_t*, 3> calls{
        slots.address + kMotionSlotsCallOffset,
        linked.address + kLinkedMotionCallOffset,
        component.address + kMotionComponentCallOffset,
    };
    for (auto* call : calls) {
        std::array<std::uint8_t, 5> replacement{};
        if (!BuildRelativePatch(
                call, 0xE8, patches.resources.motionRelay, replacement)) {
            Log("An animation call did not match the verified call boundary.");
            return OptionalPatchStatus::unavailable;
        }
    }

    for (std::size_t index = 0; index < calls.size(); ++index) {
        if (!PatchRelativeCall(
                calls[index],
                patches.resources.motionRelay,
                "an animation-delta call",
                patches.animationCalls[index])) {
            for (std::size_t patched = 0; patched < index; ++patched) {
                const auto& record = patches.animationCalls[patched];
                PatchCode(record.address,
                          record.bytes,
                          record.original,
                          "an animation-delta rollback");
            }
            return OptionalPatchStatus::unavailable;
        }
    }

    Log("Normalized three verified motion-component delta paths to the "
        "presentation cadence.");
    return OptionalPatchStatus::installed;
}

OptionalPatchStatus TryInstallInputCadence(
    const PeImage& image,
    OptionalTimingPatches& patches)
{
    const auto input = FindCodePattern(
        image, std::span<const std::uint8_t>(kInputUpdateSignature));
    if (input.count > 1) {
        Log("The input-update signature was ambiguous; 60 Hz menu-input "
            "gating was not installed.");
        return OptionalPatchStatus::unavailable;
    }
    if (input.count == 0) {
        return OptionalPatchStatus::pending;
    }
    if (!EnsureTimingHookResources(image, patches.resources)) {
        return OptionalPatchStatus::unavailable;
    }

    auto* call = input.address + kInputUpdateCallOffset;
    if (call[0] != 0xE8) {
        Log("The verified input-update call boundary changed.");
        return OptionalPatchStatus::unavailable;
    }
    std::int32_t originalDisplacement{};
    std::memcpy(&originalDisplacement, call + 1, sizeof(originalDisplacement));
    gOriginalInputUpdate = reinterpret_cast<InputUpdateFunction>(
        call + 5 + originalDisplacement);
    if (!PatchRelativeCall(call,
                           patches.resources.inputRelay,
                           "the 60 Hz input-cadence gate",
                           patches.inputCall)) {
        gOriginalInputUpdate = nullptr;
        return OptionalPatchStatus::unavailable;
    }

    Log("Gated transient and repeat input state to the original 60 Hz "
        "cadence.");
    return OptionalPatchStatus::installed;
}

OptionalPatchStatus TryInstallCameraTiming(
    const PeImage& image,
    OptionalTimingPatches& patches)
{
    const auto camera = FindCodePattern(
        image, std::span<const std::uint8_t>(kCameraInputSignature));
    if (camera.count > 1) {
        Log("The camera-input signature was ambiguous; camera normalization "
            "was not installed.");
        return OptionalPatchStatus::unavailable;
    }
    if (camera.count == 0) {
        return OptionalPatchStatus::pending;
    }
    if (!EnsureTimingHookResources(image, patches.resources)) {
        return OptionalPatchStatus::unavailable;
    }

    auto* block = camera.address + kCameraScaleBlockOffset;
    auto* continuation = block + kCameraScaleBlockSize;
    auto* stub = patches.resources.cameraStub;
    std::array<std::uint8_t, 33> stubBytes{};
    std::memcpy(stubBytes.data(), block, kCameraScaleBlockSize);

    const std::array<std::uint8_t, 5> multiplyXmm10{
        0xF3, 0x44, 0x0F, 0x59, 0x15,
    };
    const std::array<std::uint8_t, 5> multiplyXmm9{
        0xF3, 0x44, 0x0F, 0x59, 0x0D,
    };
    std::memcpy(stubBytes.data() + 10,
                multiplyXmm10.data(),
                multiplyXmm10.size());
    std::memcpy(stubBytes.data() + 19,
                multiplyXmm9.data(),
                multiplyXmm9.size());

    if (!IsRelativeReachable(stub + 19, patches.resources.data) ||
        !IsRelativeReachable(stub + 28, patches.resources.data)) {
        Log("The camera timing stub could not reach its scale value.");
        return OptionalPatchStatus::unavailable;
    }
    const auto scaleAddress =
        reinterpret_cast<std::intptr_t>(patches.resources.data);
    const auto firstDisplacement = static_cast<std::int32_t>(
        scaleAddress - reinterpret_cast<std::intptr_t>(stub + 19));
    const auto secondDisplacement = static_cast<std::int32_t>(
        scaleAddress - reinterpret_cast<std::intptr_t>(stub + 28));
    std::memcpy(stubBytes.data() + 15,
                &firstDisplacement,
                sizeof(firstDisplacement));
    std::memcpy(stubBytes.data() + 24,
                &secondDisplacement,
                sizeof(secondDisplacement));
    stubBytes[28] = 0xE9;
    if (!IsRelativeReachable(stub + 33, continuation)) {
        Log("The camera timing stub could not reach its continuation.");
        return OptionalPatchStatus::unavailable;
    }
    const auto continuationDisplacement = static_cast<std::int32_t>(
        reinterpret_cast<std::intptr_t>(continuation) -
        reinterpret_cast<std::intptr_t>(stub + 33));
    std::memcpy(stubBytes.data() + 29,
                &continuationDisplacement,
                sizeof(continuationDisplacement));
    if (!WriteExecutableRegion(stub, stubBytes)) {
        Log("Could not write the camera timing stub.");
        return OptionalPatchStatus::unavailable;
    }

    std::array<std::uint8_t, kCameraScaleBlockSize> original{};
    std::memcpy(original.data(), block, original.size());
    patches.cameraBlock.bytes.fill(0x90);
    patches.cameraBlock.bytes[0] = 0xE9;
    if (!IsRelativeReachable(block + 5, stub)) {
        Log("The camera timing branch could not reach its verified stub.");
        return OptionalPatchStatus::unavailable;
    }
    const auto stubDisplacement = static_cast<std::int32_t>(
        reinterpret_cast<std::intptr_t>(stub) -
        reinterpret_cast<std::intptr_t>(block + 5));
    std::memcpy(patches.cameraBlock.bytes.data() + 1,
                &stubDisplacement,
                sizeof(stubDisplacement));
    if (!PatchCode(block,
                   original,
                   patches.cameraBlock.bytes,
                   "the camera input scale")) {
        return OptionalPatchStatus::unavailable;
    }
    patches.cameraBlock.address = block;
    Log("Scaled the verified gameplay camera's controller and mouse input "
        "by the presentation interval.");
    return OptionalPatchStatus::installed;
}

OptionalPatchStatus TryInstallAimCameraTiming(
    const PeImage& image,
    OptionalTimingPatches& patches)
{
    const auto camera = FindCodePattern(
        image, std::span<const std::uint8_t>(kAimCameraInputSignature));
    if (camera.count > 1) {
        Log("The aiming-camera signature was ambiguous; aiming sensitivity "
            "normalization was not installed.");
        return OptionalPatchStatus::unavailable;
    }
    if (camera.count == 0) {
        return OptionalPatchStatus::pending;
    }
    if (!EnsureTimingHookResources(image, patches.resources)) {
        return OptionalPatchStatus::unavailable;
    }

    auto* block = camera.address + kAimCameraScaleBlockOffset;
    auto* continuation = block + kAimCameraScaleBlockSize;
    auto* stub = patches.resources.aimCameraStub;
    std::array<std::uint8_t, 36> stubBytes{};
    std::memcpy(stubBytes.data(), block, kAimCameraScaleBlockSize);

    const std::array<std::uint8_t, 8> multiplyXmm3{
        0xF3, 0x0F, 0x59, 0x1D, 0, 0, 0, 0,
    };
    const std::array<std::uint8_t, 8> multiplyXmm4{
        0xF3, 0x0F, 0x59, 0x25, 0, 0, 0, 0,
    };
    const std::array<std::uint8_t, 7> incrementCounter{
        0xF0, 0xFF, 0x05, 0, 0, 0, 0,
    };
    std::memcpy(stubBytes.data() + 8,
                multiplyXmm3.data(),
                multiplyXmm3.size());
    std::memcpy(stubBytes.data() + 16,
                multiplyXmm4.data(),
                multiplyXmm4.size());
    std::memcpy(stubBytes.data() + 24,
                incrementCounter.data(),
                incrementCounter.size());

    auto* scale = patches.resources.data;
    auto* counter = patches.resources.data + 2 * sizeof(LONG);
    if (!IsRelativeReachable(stub + 16, scale) ||
        !IsRelativeReachable(stub + 24, scale) ||
        !IsRelativeReachable(stub + 31, counter)) {
        Log("The aiming-camera stub could not reach its timing data.");
        return OptionalPatchStatus::unavailable;
    }
    const auto firstScaleDisplacement = static_cast<std::int32_t>(
        reinterpret_cast<std::intptr_t>(scale) -
        reinterpret_cast<std::intptr_t>(stub + 16));
    const auto secondScaleDisplacement = static_cast<std::int32_t>(
        reinterpret_cast<std::intptr_t>(scale) -
        reinterpret_cast<std::intptr_t>(stub + 24));
    const auto counterDisplacement = static_cast<std::int32_t>(
        reinterpret_cast<std::intptr_t>(counter) -
        reinterpret_cast<std::intptr_t>(stub + 31));
    std::memcpy(stubBytes.data() + 12,
                &firstScaleDisplacement,
                sizeof(firstScaleDisplacement));
    std::memcpy(stubBytes.data() + 20,
                &secondScaleDisplacement,
                sizeof(secondScaleDisplacement));
    std::memcpy(stubBytes.data() + 27,
                &counterDisplacement,
                sizeof(counterDisplacement));

    stubBytes[31] = 0xE9;
    if (!IsRelativeReachable(stub + 36, continuation)) {
        Log("The aiming-camera stub could not reach its continuation.");
        return OptionalPatchStatus::unavailable;
    }
    const auto continuationDisplacement = static_cast<std::int32_t>(
        reinterpret_cast<std::intptr_t>(continuation) -
        reinterpret_cast<std::intptr_t>(stub + 36));
    std::memcpy(stubBytes.data() + 32,
                &continuationDisplacement,
                sizeof(continuationDisplacement));
    if (!WriteExecutableRegion(stub, stubBytes)) {
        Log("Could not write the aiming-camera timing stub.");
        return OptionalPatchStatus::unavailable;
    }

    std::array<std::uint8_t, kAimCameraScaleBlockSize> original{};
    std::memcpy(original.data(), block, original.size());
    patches.aimCameraBlock.bytes.fill(0x90);
    patches.aimCameraBlock.bytes[0] = 0xE9;
    if (!IsRelativeReachable(block + 5, stub)) {
        Log("The aiming-camera timing branch could not reach its verified "
            "stub.");
        return OptionalPatchStatus::unavailable;
    }
    const auto stubDisplacement = static_cast<std::int32_t>(
        reinterpret_cast<std::intptr_t>(stub) -
        reinterpret_cast<std::intptr_t>(block + 5));
    std::memcpy(patches.aimCameraBlock.bytes.data() + 1,
                &stubDisplacement,
                sizeof(stubDisplacement));
    if (!PatchCode(block,
                   original,
                   patches.aimCameraBlock.bytes,
                   "the aiming-camera input scale")) {
        return OptionalPatchStatus::unavailable;
    }
    patches.aimCameraBlock.address = block;
    Log("Scaled the verified firearm and bow aiming camera input by the "
        "presentation interval.");
    return OptionalPatchStatus::installed;
}

OptionalPatchStatus TryInstallVegetationTiming(
    const PeImage& image,
    OptionalTimingPatches& patches)
{
    const auto grassWind = FindCodePattern(
        image, std::span<const std::uint8_t>(kGrassWindSignature));
    if (grassWind.count > 1) {
        Log("The grass-wind signature was ambiguous; vegetation animation "
            "normalization was not installed.");
        return OptionalPatchStatus::unavailable;
    }
    if (grassWind.count == 0) {
        return OptionalPatchStatus::pending;
    }
    if (!EnsureTimingHookResources(image, patches.resources)) {
        return OptionalPatchStatus::unavailable;
    }

    auto* block = grassWind.address + kGrassWindScaleBlockOffset;
    auto* continuation = block + kGrassWindScaleBlockSize;
    auto* stub = patches.resources.grassWindStub;
    std::array<std::uint8_t, 29> stubBytes{};
    std::memcpy(stubBytes.data(), block, kGrassWindScaleBlockSize);

    const std::array<std::uint8_t, 8> multiplyXmm3{
        0xF3, 0x0F, 0x59, 0x1D, 0, 0, 0, 0,
    };
    const std::array<std::uint8_t, 7> incrementCounter{
        0xF0, 0xFF, 0x05, 0, 0, 0, 0,
    };
    std::memcpy(stubBytes.data() + 9,
                multiplyXmm3.data(),
                multiplyXmm3.size());
    std::memcpy(stubBytes.data() + 17,
                incrementCounter.data(),
                incrementCounter.size());

    auto* scale = patches.resources.data;
    auto* counter = patches.resources.data + sizeof(LONG);
    if (!IsRelativeReachable(stub + 17, scale) ||
        !IsRelativeReachable(stub + 24, counter)) {
        Log("The grass-wind timing stub could not reach its timing data.");
        return OptionalPatchStatus::unavailable;
    }
    const auto scaleDisplacement = static_cast<std::int32_t>(
        reinterpret_cast<std::intptr_t>(scale) -
        reinterpret_cast<std::intptr_t>(stub + 17));
    const auto counterDisplacement = static_cast<std::int32_t>(
        reinterpret_cast<std::intptr_t>(counter) -
        reinterpret_cast<std::intptr_t>(stub + 24));
    std::memcpy(stubBytes.data() + 13,
                &scaleDisplacement,
                sizeof(scaleDisplacement));
    std::memcpy(stubBytes.data() + 20,
                &counterDisplacement,
                sizeof(counterDisplacement));

    stubBytes[24] = 0xE9;
    if (!IsRelativeReachable(stub + 29, continuation)) {
        Log("The grass-wind timing stub could not reach its continuation.");
        return OptionalPatchStatus::unavailable;
    }
    const auto continuationDisplacement = static_cast<std::int32_t>(
        reinterpret_cast<std::intptr_t>(continuation) -
        reinterpret_cast<std::intptr_t>(stub + 29));
    std::memcpy(stubBytes.data() + 25,
                &continuationDisplacement,
                sizeof(continuationDisplacement));
    if (!WriteExecutableRegion(stub, stubBytes)) {
        Log("Could not write the grass-wind timing stub.");
        return OptionalPatchStatus::unavailable;
    }

    std::array<std::uint8_t, kGrassWindScaleBlockSize> original{};
    std::memcpy(original.data(), block, original.size());
    patches.grassWindBlock.bytes.fill(0x90);
    patches.grassWindBlock.bytes[0] = 0xE9;
    if (!IsRelativeReachable(block + 5, stub)) {
        Log("The grass-wind timing branch could not reach its verified stub.");
        return OptionalPatchStatus::unavailable;
    }
    const auto stubDisplacement = static_cast<std::int32_t>(
        reinterpret_cast<std::intptr_t>(stub) -
        reinterpret_cast<std::intptr_t>(block + 5));
    std::memcpy(patches.grassWindBlock.bytes.data() + 1,
                &stubDisplacement,
                sizeof(stubDisplacement));
    if (!PatchCode(block,
                   original,
                   patches.grassWindBlock.bytes,
                   "the grass-wind animation scale")) {
        return OptionalPatchStatus::unavailable;
    }
    patches.grassWindBlock.address = block;
    Log("Scaled the verified grass and bush wind phase by the presentation "
        "interval.");
    return OptionalPatchStatus::installed;
}

struct GameplayFpsPatch
{
    std::uint8_t* address{};
    std::array<std::uint8_t, kGameplayFpsAccessorSignature.size()> bytes{};
};

GameplayFpsPatch PatchGameplayFpsAccessor(std::uint8_t* address)
{
    GameplayFpsPatch result{};
    if (std::memcmp(address,
                    kGameplayFpsAccessorSignature.data(),
                    kGameplayFpsAccessorSignature.size()) != 0) {
        Log("Gameplay FPS accessor verification failed.");
        return result;
    }

    result.address = address;
    result.bytes.fill(0x90);
    result.bytes[0] = 0x48;
    result.bytes[1] = 0xB8;
    const auto helperAddress =
        reinterpret_cast<std::uintptr_t>(&GetGameplayReferenceFps);
    std::memcpy(
        result.bytes.data() + 2, &helperAddress, sizeof(helperAddress));
    result.bytes[10] = 0xFF;
    result.bytes[11] = 0xE0;

    DWORD oldProtection{};
    if (!VirtualProtect(address,
                        result.bytes.size(),
                        PAGE_EXECUTE_READWRITE,
                        &oldProtection)) {
        Log("VirtualProtect failed before patching the gameplay FPS accessor.");
        return {};
    }

    std::memcpy(address, result.bytes.data(), result.bytes.size());
    FlushInstructionCache(
        GetCurrentProcess(), address, result.bytes.size());

    DWORD ignored{};
    if (!VirtualProtect(
            address, result.bytes.size(), oldProtection, &ignored)) {
        Log("Gameplay FPS accessor was patched, but restoring page protection "
            "failed.");
        return {};
    }
    return result;
}

struct PresentPatch
{
    std::uint8_t* address{};
    std::array<std::uint8_t, kPresentSignature.size()> bytes{};
};

PresentPatch PatchPresentDispatch(std::uint8_t* address)
{
    PresentPatch result{};
    if (std::memcmp(
            address, kPresentSignature.data(), kPresentSignature.size()) != 0) {
        Log("Present dispatch verification failed.");
        return result;
    }

    constexpr std::size_t stubSize = 34;
    auto* stub = static_cast<std::uint8_t*>(VirtualAlloc(
        nullptr, stubSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!stub) {
        Log("VirtualAlloc failed while creating the non-blocking Present stub.");
        return result;
    }

    std::array<std::uint8_t, stubSize> stubBytes{
        0x48, 0x89, 0xF9,                         // mov rcx,rdi
        0x48, 0x89, 0xEA,                         // mov rdx,rbp
        0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0,     // mov rax,helper
        0xFF, 0xD0,                               // call rax
        0x48, 0x8B, 0x4F, 0x30,                   // mov rcx,[rdi+30h]
        0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0,     // mov rax,continuation
        0xFF, 0xE0,                               // jmp rax
    };
    const auto helperAddress =
        reinterpret_cast<std::uintptr_t>(&AggressivePresent);
    const auto continuationAddress =
        reinterpret_cast<std::uintptr_t>(address + kPresentSignature.size());
    std::memcpy(stubBytes.data() + 8, &helperAddress, sizeof(helperAddress));
    std::memcpy(
        stubBytes.data() + 24, &continuationAddress, sizeof(continuationAddress));
    std::memcpy(stub, stubBytes.data(), stubBytes.size());

    DWORD oldStubProtection{};
    if (!VirtualProtect(stub, stubSize, PAGE_EXECUTE_READ, &oldStubProtection)) {
        VirtualFree(stub, 0, MEM_RELEASE);
        Log("VirtualProtect failed while enabling the non-blocking Present stub.");
        return result;
    }
    FlushInstructionCache(GetCurrentProcess(), stub, stubSize);

    result.address = address;
    result.bytes.fill(0x90);
    result.bytes[0] = 0x48;
    result.bytes[1] = 0xB8;
    const auto stubAddress = reinterpret_cast<std::uintptr_t>(stub);
    std::memcpy(result.bytes.data() + 2, &stubAddress, sizeof(stubAddress));
    result.bytes[10] = 0xFF;
    result.bytes[11] = 0xE0;

    DWORD oldProtection{};
    if (!VirtualProtect(
            address, result.bytes.size(), PAGE_EXECUTE_READWRITE, &oldProtection)) {
        VirtualFree(stub, 0, MEM_RELEASE);
        result = {};
        Log("VirtualProtect failed before patching the Present dispatch.");
        return result;
    }

    std::memcpy(address, result.bytes.data(), result.bytes.size());
    FlushInstructionCache(GetCurrentProcess(), address, result.bytes.size());

    DWORD ignored{};
    if (!VirtualProtect(address, result.bytes.size(), oldProtection, &ignored)) {
        Log("Present dispatch was patched, but restoring page protection failed.");
        return {};
    }
    return result;
}

bool ApplyFrameratePatch(std::uint8_t* table, int targetFps)
{
    DWORD oldProtection{};
    constexpr SIZE_T patchSpan =
        nioh1fix::kGameplayFpsOffsets.back() + sizeof(float);
    if (!VirtualProtect(table, patchSpan, PAGE_READWRITE, &oldProtection)) {
        Log("VirtualProtect failed before writing the frame profiles.");
        return false;
    }

    const bool patched = nioh1fix::PatchGameplayProfiles(
        std::span<std::uint8_t>(table, nioh1fix::kFrameProfileSignature.size()),
        static_cast<float>(targetFps));

    DWORD ignored{};
    const bool protectionRestored =
        VirtualProtect(table, patchSpan, oldProtection, &ignored) != FALSE;
    if (!patched) {
        Log("Frame profile verification failed; no values were changed.");
        return false;
    }
    if (!protectionRestored) {
        Log("The patch was written, but restoring page protection failed.");
        return false;
    }
    return true;
}

bool MonitorRuntimePatches(const PeImage& image,
                           std::uint8_t* table,
                           int targetFps)
{
    const auto bytes = std::span<const std::uint8_t>(
        table, nioh1fix::kFrameProfileSignature.size());
    unsigned int reapplyCount{};
    PresentPatch presentPatch{};
    GameplayFpsPatch gameplayFpsPatch{};
    std::uint8_t* mainFrameLimiterAddress{};
    OptionalTimingPatches optionalPatches{};

    for (DWORD elapsed = kMonitorIntervalMs; elapsed <= kMonitorDurationMs;
         elapsed += kMonitorIntervalMs) {
        Sleep(kMonitorIntervalMs);

        if (!gameplayFpsPatch.address) {
            const auto gameplayFpsAccessor = FindCodePattern(
                image,
                std::span<const std::uint8_t>(
                    kGameplayFpsAccessorSignature));
            if (gameplayFpsAccessor.count > 1) {
                Log("The gameplay FPS accessor signature was ambiguous.");
                return false;
            }
            if (gameplayFpsAccessor.count == 1) {
                gameplayFpsPatch = PatchGameplayFpsAccessor(
                    gameplayFpsAccessor.address);
                if (!gameplayFpsPatch.address) {
                    return false;
                }

                std::ostringstream message;
                message << "Separated gameplay timing from the render target at "
                           "RVA=0x"
                        << std::hex << std::uppercase
                        << static_cast<std::size_t>(
                               gameplayFpsPatch.address - image.base)
                        << '.';
                Log(message.str());
            }
        } else if (std::memcmp(gameplayFpsPatch.address,
                               gameplayFpsPatch.bytes.data(),
                               gameplayFpsPatch.bytes.size()) != 0) {
            Log("The gameplay FPS accessor changed unexpectedly after patching.");
            return false;
        }

        if (!mainFrameLimiterAddress) {
            const auto mainFrameLimiter = FindCodePattern(
                image,
                std::span<const std::uint8_t>(kMainFrameLimiterSignature));
            if (mainFrameLimiter.count > 1) {
                Log("The main frame-limiter signature was ambiguous.");
                return false;
            }
            if (mainFrameLimiter.count == 1) {
                mainFrameLimiterAddress = mainFrameLimiter.address;
                if (!PatchCode(
                        mainFrameLimiterAddress,
                        std::span<const std::uint8_t>(
                            kMainFrameLimiterSignature.data(),
                            kMainFrameLimiterPatch.size()),
                        std::span<const std::uint8_t>(
                            kMainFrameLimiterPatch),
                        "the main post-Present frame limiter")) {
                    return false;
                }

                std::ostringstream message;
                message << "Disabled the main post-Present frame limiter at "
                           "RVA=0x"
                        << std::hex << std::uppercase
                        << static_cast<std::size_t>(
                               mainFrameLimiterAddress - image.base)
                        << '.';
                Log(message.str());
            }
        } else if (std::memcmp(mainFrameLimiterAddress,
                               kMainFrameLimiterPatch.data(),
                               kMainFrameLimiterPatch.size()) != 0) {
            Log("The main frame limiter changed unexpectedly.");
            return false;
        }

        if (!presentPatch.address) {
            const auto present =
                FindCodePattern(image, std::span<const std::uint8_t>(kPresentSignature));
            if (present.count > 1) {
                Log("Present signature was ambiguous; no code patch was applied.");
                return false;
            }
            if (present.count == 1) {
                presentPatch = PatchPresentDispatch(present.address);
                if (!presentPatch.address) {
                    return false;
                }

                std::ostringstream message;
                message << "Forced non-blocking Present at RVA=0x" << std::hex
                        << std::uppercase
                        << static_cast<std::size_t>(presentPatch.address - image.base)
                        << " after " << std::dec << elapsed << " ms.";
                Log(message.str());
            }
        } else if (std::memcmp(presentPatch.address,
                               presentPatch.bytes.data(),
                               presentPatch.bytes.size()) != 0) {
            Log("Present dispatch changed unexpectedly after patching.");
            return false;
        }

        if (optionalPatches.animation == OptionalPatchStatus::pending) {
            optionalPatches.animation =
                TryInstallAnimationTiming(image, optionalPatches);
        } else if (optionalPatches.animation ==
                   OptionalPatchStatus::installed) {
            for (const auto& patch : optionalPatches.animationCalls) {
                if (!patch.address ||
                    std::memcmp(patch.address,
                                patch.bytes.data(),
                                patch.bytes.size()) != 0) {
                    Log("An animation timing call changed unexpectedly.");
                    return false;
                }
            }
        }

        if (optionalPatches.camera == OptionalPatchStatus::pending) {
            optionalPatches.camera =
                TryInstallCameraTiming(image, optionalPatches);
        } else if (optionalPatches.camera ==
                       OptionalPatchStatus::installed &&
                   (!optionalPatches.cameraBlock.address ||
                    std::memcmp(optionalPatches.cameraBlock.address,
                                optionalPatches.cameraBlock.bytes.data(),
                                optionalPatches.cameraBlock.bytes.size()) != 0)) {
            Log("The camera timing block changed unexpectedly.");
            return false;
        }

        if (optionalPatches.aimCamera == OptionalPatchStatus::pending) {
            optionalPatches.aimCamera =
                TryInstallAimCameraTiming(image, optionalPatches);
        } else if (
            optionalPatches.aimCamera == OptionalPatchStatus::installed &&
            (!optionalPatches.aimCameraBlock.address ||
             std::memcmp(optionalPatches.aimCameraBlock.address,
                         optionalPatches.aimCameraBlock.bytes.data(),
                         optionalPatches.aimCameraBlock.bytes.size()) != 0)) {
            Log("The aiming-camera timing block changed unexpectedly.");
            return false;
        }

        if (optionalPatches.vegetation == OptionalPatchStatus::pending) {
            optionalPatches.vegetation =
                TryInstallVegetationTiming(image, optionalPatches);
        } else if (
            optionalPatches.vegetation == OptionalPatchStatus::installed &&
            (!optionalPatches.grassWindBlock.address ||
             std::memcmp(optionalPatches.grassWindBlock.address,
                         optionalPatches.grassWindBlock.bytes.data(),
                         optionalPatches.grassWindBlock.bytes.size()) != 0)) {
            Log("The grass-wind timing block changed unexpectedly.");
            return false;
        }

        if (optionalPatches.menu == OptionalPatchStatus::pending) {
            optionalPatches.menu =
                TryInstallInputCadence(image, optionalPatches);
        } else if (optionalPatches.menu == OptionalPatchStatus::installed &&
                   (!optionalPatches.inputCall.address ||
                    std::memcmp(optionalPatches.inputCall.address,
                                optionalPatches.inputCall.bytes.data(),
                                optionalPatches.inputCall.bytes.size()) != 0)) {
            Log("The input cadence call changed unexpectedly.");
            return false;
        }

        if (elapsed % kDiagnosticsIntervalMs == 0) {
            const auto activeProfile =
                *reinterpret_cast<volatile LONG*>(image.base + kActiveFrameProfileRva);
            const auto currentFrameCount = *reinterpret_cast<volatile LONG*>(
                image.base + kFrameControllerRva + kCurrentFrameCountOffset);
            const auto completedFrameCount = *reinterpret_cast<volatile LONG*>(
                image.base + kFrameControllerRva + kCompletedFrameCountOffset);

            std::ostringstream diagnostics;
            diagnostics << "Frame diagnostics at " << elapsed
                        << " ms: active_profile=" << activeProfile;
            if (activeProfile >= 0 && activeProfile < kFrameProfileCount) {
                const auto activeTarget = *reinterpret_cast<volatile float*>(
                    table + static_cast<std::size_t>(activeProfile) *
                                kFrameProfileSize);
                diagnostics << ", active_target=" << activeTarget;
            } else {
                diagnostics << ", active_target=unavailable";
            }
            diagnostics << ", engine_fps=" << completedFrameCount
                        << ", current_frame_count=" << currentFrameCount
                        << ", present_calls=" << gPresentCallCount
                        << ", present_would_block=" << gPresentWouldBlockCount
                        << ", present_failures=" << gPresentFailureCount
                        << ", gameplay_reference_fps="
                        << GetGameplayReferenceFps()
                        << ", timing_scale=" << ReadTimingScale()
                        << ", animation_delta="
                        << (IsStockThirtyFpsProfile()
                                ? 1.0F / 30.0F
                                : ReadTimingScale() / 120.0F)
                        << ", animation_delta_calls="
                        << gMotionDeltaCallCount
                        << ", aim_camera_updates="
                        << ReadHookCounter(gAimCameraCallCount)
                        << ", grass_wind_updates="
                        << ReadHookCounter(gGrassWindCallCount)
                        << ", input_updates=" << gInputUpdateCallCount
                        << ", input_accepted=" << gInputAcceptedCount
                        << ", input_skipped=" << gInputSkippedCount;
            const LONG64 intervalTicks = InterlockedCompareExchange64(
                &gLastPresentIntervalTicks, 0, 0);
            if (intervalTicks > 0 && gPerformanceFrequency.QuadPart > 0) {
                diagnostics << ", measured_present_fps="
                            << static_cast<double>(
                                   gPerformanceFrequency.QuadPart) /
                                   static_cast<double>(intervalTicks);
            }
            if (gPresentCallCount > 0 && gPerformanceFrequency.QuadPart > 0) {
                const double averagePresentMicroseconds =
                    static_cast<double>(gPresentTotalTicks) * 1'000'000.0 /
                    static_cast<double>(gPerformanceFrequency.QuadPart) /
                    static_cast<double>(gPresentCallCount);
                diagnostics << ", average_present_us="
                            << static_cast<long long>(averagePresentMicroseconds);
            }
            diagnostics << '.';
            Log(diagnostics.str());
        }

        const auto state =
            nioh1fix::InspectGameplayProfiles(bytes, static_cast<float>(targetFps));
        if (state == nioh1fix::ProfileState::patched) {
            continue;
        }
        if (state == nioh1fix::ProfileState::invalid) {
            std::ostringstream message;
            message << "Frame profile data changed unexpectedly after " << elapsed
                    << " ms; monitoring stopped.";
            Log(message.str());
            return false;
        }

        std::ostringstream reset;
        reset << "Frame profile reset to 60 FPS after " << elapsed
              << " ms; reapplying the patch.";
        Log(reset.str());
        if (!ApplyFrameratePatch(table, targetFps)) {
            Log("Failed to reapply the framerate patch.");
            return false;
        }
        ++reapplyCount;
    }

    std::ostringstream result;
    result << "Runtime monitor completed after " << kMonitorDurationMs
           << " ms; profile state is patched, reapply_count=" << reapplyCount
           << ", present_dispatch="
           << (presentPatch.address ? "non_blocking" : "not_found")
           << ", engine_synchronization=original"
           << ", main_frame_limiter="
           << (mainFrameLimiterAddress ? "disabled" : "not_found")
           << ", gameplay_timing="
           << (gameplayFpsPatch.address ? "dynamic_compensation" : "not_found")
           << ", entity_animation="
           << (optionalPatches.animation == OptionalPatchStatus::installed
                   ? "normalized"
                   : "baseline")
           << ", vegetation_animation="
           << (optionalPatches.vegetation == OptionalPatchStatus::installed
                   ? "normalized"
                   : "baseline")
           << ", camera_input="
           << (optionalPatches.camera == OptionalPatchStatus::installed
                   ? "normalized"
                   : "baseline")
           << ", aiming_camera_input="
           << (optionalPatches.aimCamera == OptionalPatchStatus::installed
                   ? "normalized"
                   : "baseline")
           << ", menu_input="
           << (optionalPatches.menu == OptionalPatchStatus::installed
                   ? "60_hz_gated"
                   : "baseline")
           << '.';
    Log(result.str());
    return presentPatch.address != nullptr &&
           mainFrameLimiterAddress != nullptr &&
           gameplayFpsPatch.address != nullptr;
}

DWORD WINAPI MainThread(void*)
{
    const auto pluginPath = GetModulePath(gThisModule);
    const auto pluginDirectory = pluginPath.parent_path();
    gLog.open(pluginDirectory / L"Nioh1Fix.log", std::ios::trunc);
    Log("Nioh1Fix v1.4.0");
    QueryPerformanceFrequency(&gPerformanceFrequency);

    const auto exeModule = GetModuleHandleW(nullptr);
    const auto exePath = GetModulePath(exeModule);
    if (!EqualsIgnoreCase(exePath.filename().wstring(), kSupportedExe)) {
        Log("Unsupported process; expected nioh.exe. No changes were made.");
        return 0;
    }

    const auto image = ReadPeImage(exeModule);
    if (!image.headers) {
        Log("Could not validate the executable PE headers. No changes were made.");
        return 0;
    }
    gImageBase = image.base;

    const auto timestamp = image.headers->FileHeader.TimeDateStamp;
    const auto imageSize = image.headers->OptionalHeader.SizeOfImage;
    {
        std::ostringstream details;
        details << "Executable timestamp=0x" << std::hex << std::uppercase
                << timestamp << ", image_size=0x" << imageSize;
        Log(details.str());
    }
    if (timestamp != kSupportedTimestamp || imageSize != kSupportedImageSize) {
        Log("Unsupported Nioh executable version. No changes were made.");
        return 0;
    }

    const auto iniPath = pluginDirectory / L"Nioh1Fix.ini";
    const bool enabled =
        ReadIniBool(iniPath, L"Framerate", L"Enabled", true);
    const int targetFps = GetPrivateProfileIntW(
        L"Framerate", L"TargetFPS", kDefaultTargetFps, iniPath.c_str());
    gConfiguredTargetFps = targetFps;
    if (!enabled) {
        Log("Framerate patch is disabled in Nioh1Fix.ini.");
        return 0;
    }
    if (targetFps < kMinTargetFps || targetFps > kMaxTargetFps) {
        Log("TargetFPS must be between 60 and 360. No changes were made.");
        return 0;
    }

    auto* table = FindUniqueFrameProfileTable(image);
    if (!table) {
        Log("Compatible frame profile data was not found. No changes were made.");
        return 0;
    }

    {
        std::ostringstream location;
        location << "Frame profile table RVA=0x" << std::hex << std::uppercase
                 << static_cast<std::size_t>(table - image.base);
        Log(location.str());
    }
    if (!ApplyFrameratePatch(table, targetFps)) {
        Log("Failed to apply the framerate patch.");
        return 0;
    }

    std::ostringstream success;
    success << "Patched both 60 FPS gameplay profiles to " << targetFps
            << " FPS; 30 FPS profiles were left unchanged.";
    Log(success.str());
    MonitorRuntimePatches(image, table, targetFps);
    return 0;
}
} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        gThisModule = module;
        if (HANDLE thread = CreateThread(nullptr, 0, MainThread, nullptr, 0, nullptr)) {
            CloseHandle(thread);
        }
    }
    return TRUE;
}
