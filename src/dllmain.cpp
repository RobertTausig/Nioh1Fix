#include "frame_profile.hpp"

#include <windows.h>

#include <algorithm>
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

HMODULE gThisModule{};
std::ofstream gLog;

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

DWORD WINAPI MainThread(void*)
{
    const auto pluginPath = GetModulePath(gThisModule);
    const auto pluginDirectory = pluginPath.parent_path();
    gLog.open(pluginDirectory / L"Nioh1Fix.log", std::ios::trunc);
    Log("Nioh1Fix v0.1.0");

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

    if (!ApplyFrameratePatch(table, targetFps)) {
        Log("Failed to apply the framerate patch.");
        return 0;
    }

    std::ostringstream success;
    success << "Patched both 60 FPS gameplay profiles to " << targetFps
            << " FPS; 30 FPS profiles were left unchanged.";
    Log(success.str());
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
