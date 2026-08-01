#include "runtime.hpp"

#include <bit>
#include <cstring>
#include <sstream>

namespace nioh1fix::runtime {
static std::uint8_t* AllocateNear(const PeImage& image, SIZE_T size) {
    SYSTEM_INFO info{}; GetSystemInfo(&info);
    const auto granularity = std::uintptr_t(info.dwAllocationGranularity);
    const SIZE_T allocation = (size + info.dwPageSize - 1) & ~(info.dwPageSize - 1);
    const auto imageEnd = std::uintptr_t(image.base) +
                          image.headers->OptionalHeader.SizeOfImage;
    std::uintptr_t cursor = (imageEnd + granularity - 1) & ~(granularity - 1);
    const auto limit = std::uintptr_t(image.base) + 0x60000000ULL;
    while (cursor < limit) {
        MEMORY_BASIC_INFORMATION region{};
        if (!VirtualQuery(reinterpret_cast<void*>(cursor), &region, sizeof(region))) break;
        const auto base = std::uintptr_t(region.BaseAddress);
        const auto end = base + region.RegionSize;
        if (region.State == MEM_FREE) {
            const auto candidate = (base + granularity - 1) & ~(granularity - 1);
            if (candidate >= cursor && candidate < end && allocation <= end - candidate)
                if (auto* memory = static_cast<std::uint8_t*>(VirtualAlloc(
                        reinterpret_cast<void*>(candidate), allocation,
                        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE))) return memory;
        }
        if (end <= cursor) break;
        cursor = end;
    }
    return nullptr;
}

static std::array<std::uint8_t, 12> AbsoluteJump(const void* destination) {
    std::array<std::uint8_t, 12> bytes{0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};
    const auto target = reinterpret_cast<std::uintptr_t>(destination);
    std::memcpy(bytes.data() + 2, &target, sizeof(target));
    return bytes;
}

bool EnsureHookResources(const PeImage& image, HookResources& resources) {
    if (resources.code) return true;
    auto* code = AllocateNear(image, 4096);
    auto* data = AllocateNear(image, 4096);
    if (!code || !data) {
        if (code) VirtualFree(code, 0, MEM_RELEASE);
        if (data) VirtualFree(data, 0, MEM_RELEASE);
        Log("Could not reserve memory near nioh.exe for optional timing hooks.");
        return false;
    }
    resources = {code, data};
    const auto motion = AbsoluteJump(reinterpret_cast<const void*>(&GetNormalizedMotionDelta));
    const auto input = AbsoluteJump(reinterpret_cast<const void*>(&NormalizedInputUpdate));
    std::memcpy(code, motion.data(), motion.size());
    std::memcpy(code + 16, input.data(), input.size());
    auto* values = reinterpret_cast<LONG*>(data);
    values[0] = std::bit_cast<LONG>(ReadTimingScale());
    for (std::size_t i = 1; i < 8; ++i) values[i] = 0;
    DWORD ignored{};
    if (!VirtualProtect(code, 4096, PAGE_EXECUTE_READ, &ignored)) {
        VirtualFree(code, 0, MEM_RELEASE); VirtualFree(data, 0, MEM_RELEASE);
        resources = {}; Log("Could not make the optional timing relays executable.");
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), code, 32);
    g.timingData = reinterpret_cast<volatile LONG*>(data);
    return true;
}

static bool AppendRip(std::array<std::uint8_t, 64>& bytes, std::size_t& size,
                      std::span<const std::uint8_t> opcode,
                      std::uint8_t* stub, const void* target) {
    const std::size_t end = size + opcode.size() + sizeof(std::int32_t);
    if (end > bytes.size() || !IsReachable(stub + end, target)) return false;
    std::memcpy(bytes.data() + size, opcode.data(), opcode.size());
    const auto displacement = static_cast<std::int32_t>(
        reinterpret_cast<std::intptr_t>(target) -
        reinterpret_cast<std::intptr_t>(stub + end));
    std::memcpy(bytes.data() + size + opcode.size(), &displacement,
                sizeof(displacement));
    size = end; return true;
}

static bool AppendMultiply(std::array<std::uint8_t, 64>& bytes,
                           std::size_t& size, std::uint8_t* stub,
                           std::uint8_t reg, const void* scale) {
    if (reg < 8) {
        const std::array<std::uint8_t, 4> opcode{0xF3,0x0F,0x59,
            static_cast<std::uint8_t>(0x05 + reg * 8)};
        return AppendRip(bytes, size, opcode, stub, scale);
    }
    const std::array<std::uint8_t, 5> opcode{0xF3,0x44,0x0F,0x59,
        static_cast<std::uint8_t>(0x05 + (reg - 8) * 8)};
    return AppendRip(bytes, size, opcode, stub, scale);
}

PatchStatus InstallBlockHook(const PeImage& image, const HookSpec& spec,
                             std::uint8_t* block,
                             HookState& state, HookResources& resources) {
    if (!EnsureHookResources(image, resources)) return PatchStatus::unavailable;
    auto* stub = resources.code + spec.stubOffset;
    std::array<std::uint8_t, 64> stubBytes{};
    std::size_t size = spec.blockSize;
    std::memcpy(stubBytes.data(), block, size);
    for (std::size_t i = 0; i < spec.xmmCount; ++i)
        if (!AppendMultiply(stubBytes, size, stub, spec.xmm[i], resources.data)) {
            Log(std::string("The ") + spec.name + " stub could not reach its scale.");
            return PatchStatus::unavailable;
        }
    if (spec.counterIndex >= 0) {
        constexpr std::array<std::uint8_t, 3> increment{0xF0,0xFF,0x05};
        if (!AppendRip(stubBytes, size, increment, stub,
                       resources.data + spec.counterIndex * sizeof(LONG))) {
            Log(std::string("The ") + spec.name + " stub could not reach its counter.");
            return PatchStatus::unavailable;
        }
    }
    constexpr std::array<std::uint8_t, 1> jump{0xE9};
    if (!AppendRip(stubBytes, size, jump, stub, block + spec.blockSize) ||
        !WriteExecutable(stub, {stubBytes.data(), size})) {
        Log(std::string("Could not complete the ") + spec.name + " timing stub.");
        return PatchStatus::unavailable;
    }
    state.patch = {}; state.patch.address = block; state.patch.size = spec.blockSize;
    std::memcpy(state.patch.original.data(), block, spec.blockSize);
    std::fill_n(state.patch.applied.data(), spec.blockSize, 0x90);
    state.patch.applied[0] = 0xE9;
    if (!IsReachable(block + 5, stub)) return PatchStatus::unavailable;
    const auto displacement = static_cast<std::int32_t>(
        reinterpret_cast<std::intptr_t>(stub) -
        reinterpret_cast<std::intptr_t>(block + 5));
    std::memcpy(state.patch.applied.data() + 1, &displacement, sizeof(displacement));
    if (!PatchCode(block, {state.patch.original.data(), spec.blockSize},
                   {state.patch.applied.data(), spec.blockSize}, spec.name))
        return PatchStatus::unavailable;
    Log(spec.success); return PatchStatus::installed;
}
} // namespace nioh1fix::runtime
