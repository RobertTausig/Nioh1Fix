#include "runtime.hpp"

#include <iomanip>
#include <sstream>

namespace nioh1fix::runtime {
DWORD WINAPI MainThread(void*) {
    const auto directory = ModulePath(g.module).parent_path();
    g.log.open(directory / L"Nioh1Fix.log", std::ios::trunc);
    Log("Nioh1Fix v1.6.1");
    QueryPerformanceFrequency(&g.frequency);

    const auto module = GetModuleHandleW(nullptr);
    const auto path = ModulePath(module);
    if (!EqualsIgnoreCase(path.filename().wstring(), kSupportedExe)) {
        Log("Unsupported process; expected nioh.exe. No changes were made.");
        return 0;
    }
    const auto image = ReadPeImage(module);
    if (!image.headers) {
        Log("Could not validate the executable PE headers. No changes were made.");
        return 0;
    }
    const auto timestamp = image.headers->FileHeader.TimeDateStamp;
    const auto imageSize = image.headers->OptionalHeader.SizeOfImage;
    std::ostringstream metadata;
    metadata << "Executable timestamp=0x" << std::hex << std::uppercase
             << timestamp << ", image_size=0x" << imageSize;
    Log(metadata.str());
    const auto ini = directory / L"Nioh1Fix.ini";
    if (!ReadIniBool(ini, L"Framerate", L"Enabled", true)) {
        Log("Framerate patch is disabled in Nioh1Fix.ini.");
        return 0;
    }
    Monitor(image);
    return 0;
}
} // namespace nioh1fix::runtime

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    using namespace nioh1fix::runtime;
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(module);
    g.module = module;
    if (HANDLE thread = CreateThread(nullptr, 0, MainThread, nullptr, 0, nullptr))
        CloseHandle(thread);
    return TRUE;
}
