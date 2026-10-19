#pragma once
#include <windows.h>

namespace AffinityArabic {

class AffinityHookManager {
public:
    static bool Initialize();
    static void Shutdown();

private:
    static DWORD WINAPI HookWorkerThread(LPVOID lpParam);
    static bool InstallHooks();
};

}
