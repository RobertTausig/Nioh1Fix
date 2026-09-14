#include "diagnostic_hooks.hpp"

#include <algorithm>
#include <cstring>

namespace nioh1fix::runtime {
inline constexpr std::size_t kHookAllocationSize = 4096;
using StubBytes = std::array<std::uint8_t, 384>;

static bool Append(StubBytes& bytes, std::size_t& size,
                   std::span<const std::uint8_t> value) {
    if (size + value.size() > bytes.size()) return false;
    std::memcpy(bytes.data() + size, value.data(), value.size());
    size += value.size();
    return true;
}

static bool AppendRelative(StubBytes& bytes, std::size_t& size,
                           std::uint8_t* stub,
                           std::span<const std::uint8_t> opcode,
                           const void* target) {
    const auto end = size + opcode.size() + sizeof(std::int32_t);
    if (end > bytes.size() || !IsReachable(stub + end, target) ||
        !Append(bytes, size, opcode)) return false;
    const auto displacement = static_cast<std::int32_t>(
        reinterpret_cast<std::intptr_t>(target) -
        reinterpret_cast<std::intptr_t>(stub + end));
    return Append(bytes, size, {reinterpret_cast<const std::uint8_t*>(
        &displacement), sizeof(displacement)});
}

static bool AppendXmm(StubBytes& bytes, std::size_t& size,
                      std::uint8_t reg, std::uint8_t offset, bool restore) {
    if (reg > 5) return false;
    const std::array<std::uint8_t, 6> instruction{
        0xF3,0x0F,static_cast<std::uint8_t>(restore ? 0x6F : 0x7F),
        static_cast<std::uint8_t>(0x44 + reg * 8),0x24,offset};
    return Append(bytes, size, instruction);
}

PatchStatus InstallCallbackDiagnostic(const PeImage& image,
    const HookSpec& spec, std::uint8_t* block, HookState& state,
    HookResources& resources, const CallbackDiagnosticOptions& options) {
    if (!block || !options.callback || spec.blockSize < 5 ||
        spec.blockSize > state.patch.original.size() ||
        spec.stubOffset > kHookAllocationSize - StubBytes{}.size() ||
        options.preservedXmmCount > options.preservedXmm.size() ||
        !IsImageRange(image, block, spec.blockSize, IMAGE_SCN_MEM_EXECUTE) ||
        !EnsureHookResources(image, resources)) return PatchStatus::unavailable;
    for (std::size_t i = 0; i < options.preservedXmmCount; ++i)
        if (options.preservedXmm[i] > 5) return PatchStatus::unavailable;
    auto* stub = resources.code + spec.stubOffset;
    StubBytes bytes{};
    std::size_t size = spec.blockSize;
    std::memcpy(bytes.data(), block, size);
    const std::array<std::uint8_t, 7> reserve{
        0x48,0x81,0xEC,static_cast<std::uint8_t>(
            options.stackAlignedAfterBlock ? 0xA0 : 0xA8),0,0,0};
    const std::array<std::uint8_t, 7> release{
        0x48,0x81,0xC4,reserve[3],0,0,0};
    constexpr std::array<std::uint8_t, 44> saveRegisters{
        0x48,0x89,0x84,0x24,0x80,0,0,0, 0x48,0x89,0x4C,0x24,0x20,
        0x48,0x89,0x54,0x24,0x28, 0x4C,0x89,0x44,0x24,0x30,
        0x4C,0x89,0x4C,0x24,0x38, 0x4C,0x89,0x94,0x24,0x88,0,0,0,
        0x4C,0x89,0x9C,0x24,0x90,0,0,0};
    constexpr std::array<std::uint8_t, 44> restoreRegisters{
        0x48,0x8B,0x4C,0x24,0x20, 0x48,0x8B,0x54,0x24,0x28,
        0x4C,0x8B,0x44,0x24,0x30, 0x4C,0x8B,0x4C,0x24,0x38,
        0x4C,0x8B,0x94,0x24,0x88,0,0,0, 0x4C,0x8B,0x9C,0x24,0x90,0,0,0,
        0x48,0x8B,0x84,0x24,0x80,0,0,0};
    constexpr std::array<std::uint8_t, 10> saveFlags{
        0x9C,0x58,0x48,0x89,0x84,0x24,0x98,0,0,0};
    constexpr std::array<std::uint8_t, 10> restoreFlags{
        0x48,0x8B,0x84,0x24,0x98,0,0,0,0x50,0x9D};
    std::array<std::uint8_t, 12> callback{
        0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xD0};
    const auto callbackAddress = reinterpret_cast<std::uintptr_t>(options.callback);
    std::memcpy(callback.data() + 2, &callbackAddress, sizeof(callbackAddress));
    if (!Append(bytes, size, reserve) || !Append(bytes, size, saveRegisters) ||
        !Append(bytes, size, saveFlags)) return PatchStatus::unavailable;
    for (std::size_t i = 0; i < options.preservedXmmCount; ++i)
        if (!AppendXmm(bytes, size, options.preservedXmm[i],
                       static_cast<std::uint8_t>(0x40 + i * 0x10), false))
            return PatchStatus::unavailable;
    if (!Append(bytes, size, callback)) return PatchStatus::unavailable;
    for (std::size_t i = options.preservedXmmCount; i-- > 0;)
        if (!AppendXmm(bytes, size, options.preservedXmm[i],
                       static_cast<std::uint8_t>(0x40 + i * 0x10), true))
            return PatchStatus::unavailable;
    constexpr std::array<std::uint8_t, 3> increment{0xF0,0xFF,0x05};
    constexpr std::array<std::uint8_t, 1> jump{0xE9};
    if (!Append(bytes, size, restoreFlags) ||
        !Append(bytes, size, restoreRegisters) || !Append(bytes, size, release) ||
        (spec.counterIndex >= 0 && !AppendRelative(bytes, size, stub, increment,
            resources.data + spec.counterIndex * sizeof(LONG))) ||
        !AppendRelative(bytes, size, stub, jump, block + spec.blockSize) ||
        !WriteExecutable(stub, {bytes.data(), size})) {
        if (options.failure) Log(options.failure);
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
    if (options.success) Log(options.success);
    return PatchStatus::installed;
}
} // namespace nioh1fix::runtime
