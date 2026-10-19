#include <windows.h>
#include "affinity_arabic_hook.h"

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        AffinityArabic::AffinityHookManager::Initialize();
        break;
    case DLL_PROCESS_DETACH:
        AffinityArabic::AffinityHookManager::Shutdown();
        break;
    }
    return TRUE;
}
