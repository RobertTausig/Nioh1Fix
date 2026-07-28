#include "frame_profile.hpp"

#include <windows.h>

#include <algorithm>
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
constexpr std::size_t kAnimationDeltaOffset = 0x18322C;
constexpr std::array<std::uint8_t, 32> kAnimationUpdateSignature{
    0x48, 0x8B, 0xC4, 0x48, 0x89, 0x50, 0x10, 0x41,
    0x56, 0x48, 0x81, 0xEC, 0xA0, 0x00, 0x00, 0x00,
    0xF3, 0x0F, 0x10, 0x81, 0x38, 0x32, 0x18, 0x00,
    0x4C, 0x8B, 0xF1, 0xF3, 0x0F, 0x5C, 0x82, 0x68,
};
constexpr std::size_t kAnimationUpdatePatchSize = 16;

HMODULE gThisModule{};
std::uint8_t* gImageBase{};
std::ofstream gLog;
volatile LONG gPresentCallCount{};
volatile LONG gPresentWouldBlockCount{};
volatile LONG gPresentFailureCount{};
volatile LONG64 gPresentTotalTicks{};
LARGE_INTEGER gPerformanceFrequency{};
using AnimationUpdateFunction = void (*)(void*, void*);
AnimationUpdateFunction gOriginalAnimationUpdate{};
volatile LONG64 gPreviousAnimationTick{};
volatile LONG gAnimationUpdateCount{};
volatile LONG gAnimationOriginalDeltaMilli{};
volatile LONG gAnimationCorrectedDeltaMilli{};
volatile LONG gAnimationScaleMilli{1000};
volatile LONG gAnimationFrameMicroseconds{};

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

void CorrectAnimationUpdate(void* world, void* frameState)
{
    gOriginalAnimationUpdate(world, frameState);

    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    const LONG64 previous =
        InterlockedExchange64(&gPreviousAnimationTick, now.QuadPart);
    auto* delta = reinterpret_cast<float*>(
        static_cast<std::uint8_t*>(world) + kAnimationDeltaOffset);
    const float originalDelta = *delta;

    InterlockedIncrement(&gAnimationUpdateCount);
    InterlockedExchange(
        &gAnimationOriginalDeltaMilli,
        static_cast<LONG>(originalDelta * 1000.0f));

    if (previous <= 0 || gPerformanceFrequency.QuadPart <= 0 ||
        !std::isfinite(originalDelta) || originalDelta == 0.0f) {
        InterlockedExchange(
            &gAnimationCorrectedDeltaMilli,
            static_cast<LONG>(originalDelta * 1000.0f));
        return;
    }

    const double elapsedSeconds =
        static_cast<double>(now.QuadPart - previous) /
        static_cast<double>(gPerformanceFrequency.QuadPart);
    if (elapsedSeconds <= 0.0 || elapsedSeconds > 0.25) {
        InterlockedExchange(
            &gAnimationCorrectedDeltaMilli,
            static_cast<LONG>(originalDelta * 1000.0f));
        return;
    }

    const LONG activeProfile =
        *reinterpret_cast<volatile LONG*>(
            gImageBase + kActiveFrameProfileRva);
    const float referenceFps =
        activeProfile == 1 || activeProfile == 2 ? 30.0f : 60.0f;
    const float scale = std::clamp(
        static_cast<float>(elapsedSeconds) * referenceFps, 0.05f, 4.0f);
    const float correctedDelta = originalDelta * scale;
    if (!std::isfinite(correctedDelta)) {
        return;
    }

    *delta = correctedDelta;
    InterlockedExchange(
        &gAnimationCorrectedDeltaMilli,
        static_cast<LONG>(correctedDelta * 1000.0f));
    InterlockedExchange(
        &gAnimationScaleMilli,
        static_cast<LONG>(scale * 1000.0f));
    InterlockedExchange(
        &gAnimationFrameMicroseconds,
        static_cast<LONG>(elapsedSeconds * 1'000'000.0));
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

struct AnimationPatch
{
    std::uint8_t* address{};
    std::array<std::uint8_t, kAnimationUpdatePatchSize> bytes{};
};

AnimationPatch PatchAnimationUpdate(std::uint8_t* address)
{
    AnimationPatch result{};
    if (std::memcmp(address,
                    kAnimationUpdateSignature.data(),
                    kAnimationUpdateSignature.size()) != 0) {
        Log("Animation update verification failed.");
        return result;
    }

    constexpr std::size_t trampolineSize =
        kAnimationUpdatePatchSize + 12;
    auto* trampoline = static_cast<std::uint8_t*>(VirtualAlloc(
        nullptr,
        trampolineSize,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE));
    if (!trampoline) {
        Log("VirtualAlloc failed while creating the animation trampoline.");
        return result;
    }

    std::array<std::uint8_t, trampolineSize> trampolineBytes{};
    std::memcpy(trampolineBytes.data(),
                kAnimationUpdateSignature.data(),
                kAnimationUpdatePatchSize);
    trampolineBytes[kAnimationUpdatePatchSize] = 0x48;
    trampolineBytes[kAnimationUpdatePatchSize + 1] = 0xB8;
    const auto continuationAddress =
        reinterpret_cast<std::uintptr_t>(
            address + kAnimationUpdatePatchSize);
    std::memcpy(trampolineBytes.data() + kAnimationUpdatePatchSize + 2,
                &continuationAddress,
                sizeof(continuationAddress));
    trampolineBytes[kAnimationUpdatePatchSize + 10] = 0xFF;
    trampolineBytes[kAnimationUpdatePatchSize + 11] = 0xE0;
    std::memcpy(
        trampoline, trampolineBytes.data(), trampolineBytes.size());

    DWORD oldTrampolineProtection{};
    if (!VirtualProtect(trampoline,
                        trampolineSize,
                        PAGE_EXECUTE_READ,
                        &oldTrampolineProtection)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        Log("VirtualProtect failed while enabling the animation trampoline.");
        return result;
    }
    FlushInstructionCache(
        GetCurrentProcess(), trampoline, trampolineSize);

    result.address = address;
    result.bytes.fill(0x90);
    result.bytes[0] = 0x48;
    result.bytes[1] = 0xB8;
    const auto helperAddress =
        reinterpret_cast<std::uintptr_t>(&CorrectAnimationUpdate);
    std::memcpy(
        result.bytes.data() + 2, &helperAddress, sizeof(helperAddress));
    result.bytes[10] = 0xFF;
    result.bytes[11] = 0xE0;

    gOriginalAnimationUpdate =
        reinterpret_cast<AnimationUpdateFunction>(trampoline);
    DWORD oldProtection{};
    if (!VirtualProtect(address,
                        result.bytes.size(),
                        PAGE_EXECUTE_READWRITE,
                        &oldProtection)) {
        gOriginalAnimationUpdate = nullptr;
        VirtualFree(trampoline, 0, MEM_RELEASE);
        Log("VirtualProtect failed before patching the animation update.");
        return {};
    }

    std::memcpy(address, result.bytes.data(), result.bytes.size());
    FlushInstructionCache(
        GetCurrentProcess(), address, result.bytes.size());

    DWORD ignored{};
    if (!VirtualProtect(
            address, result.bytes.size(), oldProtection, &ignored)) {
        Log("Animation update was patched, but restoring page protection failed.");
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
    AnimationPatch animationPatch{};
    std::uint8_t* mainFrameLimiterAddress{};

    for (DWORD elapsed = kMonitorIntervalMs; elapsed <= kMonitorDurationMs;
         elapsed += kMonitorIntervalMs) {
        Sleep(kMonitorIntervalMs);

        if (!animationPatch.address) {
            const auto animationUpdate = FindCodePattern(
                image,
                std::span<const std::uint8_t>(kAnimationUpdateSignature));
            if (animationUpdate.count > 1) {
                Log("The animation-update signature was ambiguous.");
                return false;
            }
            if (animationUpdate.count == 1) {
                animationPatch =
                    PatchAnimationUpdate(animationUpdate.address);
                if (!animationPatch.address) {
                    return false;
                }

                std::ostringstream message;
                message << "Enabled frame-time animation correction at RVA=0x"
                        << std::hex << std::uppercase
                        << static_cast<std::size_t>(
                               animationPatch.address - image.base)
                        << '.';
                Log(message.str());
            }
        } else if (std::memcmp(animationPatch.address,
                               animationPatch.bytes.data(),
                               animationPatch.bytes.size()) != 0) {
            Log("The animation update changed unexpectedly after patching.");
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
                        << ", animation_updates=" << gAnimationUpdateCount
                        << ", animation_original_x1000="
                        << gAnimationOriginalDeltaMilli
                        << ", animation_corrected_x1000="
                        << gAnimationCorrectedDeltaMilli
                        << ", animation_scale_x1000="
                        << gAnimationScaleMilli
                        << ", animation_frame_us="
                        << gAnimationFrameMicroseconds;
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
           << ", animation_timestep="
           << (animationPatch.address ? "normalized" : "not_found")
           << '.';
    Log(result.str());
    return presentPatch.address != nullptr &&
           mainFrameLimiterAddress != nullptr &&
           animationPatch.address != nullptr;
}

DWORD WINAPI MainThread(void*)
{
    const auto pluginPath = GetModulePath(gThisModule);
    const auto pluginDirectory = pluginPath.parent_path();
    gLog.open(pluginDirectory / L"Nioh1Fix.log", std::ios::trunc);
    Log("Nioh1Fix v1.1.0");
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
