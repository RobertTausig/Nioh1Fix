#include "runtime.hpp"

#include <algorithm>
#include <cwctype>
#include <cstring>

namespace nioh1fix::runtime {
void Log(const std::string& message) {
    if (g.log.is_open()) { g.log << message << '\n'; g.log.flush(); }
    OutputDebugStringA((std::string("Nioh1Fix: ") + message + "\n").c_str());
}

std::filesystem::path ModulePath(HMODULE module) {
    std::wstring buffer(32768, L'\0');
    const DWORD size = GetModuleFileNameW(module, buffer.data(), buffer.size());
    if (!size || size >= buffer.size()) return {};
    buffer.resize(size);
    return buffer;
}
bool EqualsIgnoreCase(const std::wstring& left, const std::wstring& right) {
    return left.size() == right.size() &&
           std::equal(left.begin(), left.end(), right.begin(),
                      [](wchar_t a, wchar_t b) { return towlower(a) == towlower(b); });
}

bool ReadIniBool(const std::filesystem::path& path, const wchar_t* section,
                 const wchar_t* key, bool fallback) {
    wchar_t value[32]{};
    GetPrivateProfileStringW(section, key, fallback ? L"true" : L"false",
                             value, std::size(value), path.c_str());
    std::wstring text(value);
    auto visible = [](wchar_t c) { return !iswspace(c); };
    text.erase(text.begin(), std::find_if(text.begin(), text.end(), visible));
    text.erase(std::find_if(text.rbegin(), text.rend(), visible).base(), text.end());
    std::transform(text.begin(), text.end(), text.begin(), towlower);
    if (text == L"true" || text == L"yes" || text == L"1" || text == L"on")
        return true;
    if (text == L"false" || text == L"no" || text == L"0" || text == L"off")
        return false;
    return fallback;
}

PeImage ReadPeImage(HMODULE module) {
    auto* base = reinterpret_cast<std::uint8_t*>(module);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) return {};
    auto* headers = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (headers->Signature != IMAGE_NT_SIGNATURE ||
        headers->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        headers->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return {};
    return {base, headers};
}

static SearchResult FindInSections(const PeImage& image,
                                   BytePattern pattern,
                                   DWORD characteristics) {
    SearchResult result{};
    auto* section = IMAGE_FIRST_SECTION(image.headers);
    const auto imageSize = image.headers->OptionalHeader.SizeOfImage;
    for (WORD i = 0; i < image.headers->FileHeader.NumberOfSections; ++i, ++section) {
        if ((section->Characteristics & characteristics) != characteristics) continue;
        const std::size_t rva = section->VirtualAddress;
        const std::size_t size = section->Misc.VirtualSize;
        if (rva >= imageSize || size > imageSize - rva) continue;
        const auto found = FindPattern({image.base + rva, size}, pattern);
        if (found.status == MatchStatus::unique)
            result.address = image.base + rva + found.offset;
        result.count += found.count;
        if (result.count > 1) return result;
    }
    return result;
}

SearchResult FindCode(const PeImage& image, BytePattern pattern) {
    return FindInSections(image, pattern, IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE);
}

bool IsImageRange(const PeImage& image, const void* address,
                  std::size_t size, DWORD characteristics) {
    const auto target = reinterpret_cast<std::uintptr_t>(address);
    auto* section = IMAGE_FIRST_SECTION(image.headers);
    for (WORD i = 0; i < image.headers->FileHeader.NumberOfSections; ++i, ++section) {
        if ((section->Characteristics & characteristics) != characteristics) continue;
        const auto begin = reinterpret_cast<std::uintptr_t>(image.base) +
                           section->VirtualAddress;
        const auto end = begin + section->Misc.VirtualSize;
        if (target >= begin && target <= end && size <= end - target) return true;
    }
    return false;
}

std::uint8_t* DecodeRelative(std::uint8_t* instruction,
                             std::size_t displacementOffset,
                             std::size_t instructionSize) {
    std::int32_t displacement{};
    std::memcpy(&displacement, instruction + displacementOffset, sizeof(displacement));
    return instruction + instructionSize + displacement;
}

bool PatchCode(std::uint8_t* address, std::span<const std::uint8_t> expected,
               std::span<const std::uint8_t> replacement, const char* name) {
    if (expected.size() != replacement.size() ||
        std::memcmp(address, expected.data(), expected.size())) {
        Log(std::string(name) + " verification failed."); return false;
    }
    DWORD protection{};
    if (!VirtualProtect(address, replacement.size(), PAGE_EXECUTE_READWRITE,
                        &protection)) {
        Log(std::string("VirtualProtect failed before patching ") + name + '.');
        return false;
    }
    std::memcpy(address, replacement.data(), replacement.size());
    FlushInstructionCache(GetCurrentProcess(), address, replacement.size());
    DWORD ignored{};
    if (VirtualProtect(address, replacement.size(), protection, &ignored)) return true;
    Log(std::string(name) + " was patched, but restoring page protection failed.");
    return false;
}

bool WriteExecutable(std::uint8_t* address, std::span<const std::uint8_t> bytes) {
    DWORD ignored{};
    if (!VirtualProtect(address, bytes.size(), PAGE_EXECUTE_READWRITE, &ignored)) return false;
    std::memcpy(address, bytes.data(), bytes.size());
    FlushInstructionCache(GetCurrentProcess(), address, bytes.size());
    return VirtualProtect(address, bytes.size(), PAGE_EXECUTE_READ, &ignored) != FALSE;
}
bool IsReachable(const void* end, const void* destination) {
    const auto distance = reinterpret_cast<std::intptr_t>(destination) -
                          reinterpret_cast<std::intptr_t>(end);
    return distance >= INT32_MIN && distance <= INT32_MAX;
}
bool IsApplied(const PatchRecord& patch) {
    return patch.address && patch.size &&
           !std::memcmp(patch.address, patch.applied.data(), patch.size);
}
bool SetFrameProfiles(std::uint8_t* table, bool enable) {
    const auto state = InspectGameplayProfiles(
        {table, kFrameProfileSignature.size()}, float(kInternalTargetFps));
    if ((enable && state != ProfileState::original) ||
        (!enable && state != ProfileState::patched)) return false;
    constexpr SIZE_T span = kGameplayFpsOffsets.back() + sizeof(float);
    DWORD protection{};
    if (!VirtualProtect(table, span, PAGE_READWRITE, &protection)) return false;
    const float value = enable ? float(kInternalTargetFps) : 60.0F;
    for (auto offset : kGameplayFpsOffsets)
        std::memcpy(table + offset, &value, sizeof(value));
    DWORD ignored{};
    return VirtualProtect(table, span, protection, &ignored) != FALSE;
}
} // namespace nioh1fix::runtime
