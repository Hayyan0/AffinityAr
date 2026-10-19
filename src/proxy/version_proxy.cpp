#include <windows.h>
#include "../core/affinity_arabic_hook.h"

static HMODULE g_hOriginalVersion = nullptr;

static void LoadOriginalVersionDll() {
    if (g_hOriginalVersion) return;

    wchar_t sysPath[MAX_PATH];
    if (GetSystemDirectoryW(sysPath, MAX_PATH) == 0) return;
    wcscat_s(sysPath, L"\\version.dll");

    g_hOriginalVersion = LoadLibraryW(sysPath);
}

static FARPROC GetOrigProc(const char* name) {
    LoadOriginalVersionDll();
    return g_hOriginalVersion ? GetProcAddress(g_hOriginalVersion, name) : nullptr;
}

extern "C" {

BOOL WINAPI Proxy_GetFileVersionInfoA(LPCSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData) {
    typedef BOOL(WINAPI* PFN)(LPCSTR, DWORD, DWORD, LPVOID);
    static PFN pfn = (PFN)GetOrigProc("GetFileVersionInfoA");
    return pfn ? pfn(lptstrFilename, dwHandle, dwLen, lpData) : FALSE;
}

BOOL WINAPI Proxy_GetFileVersionInfoW(LPCWSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData) {
    typedef BOOL(WINAPI* PFN)(LPCWSTR, DWORD, DWORD, LPVOID);
    static PFN pfn = (PFN)GetOrigProc("GetFileVersionInfoW");
    return pfn ? pfn(lptstrFilename, dwHandle, dwLen, lpData) : FALSE;
}

BOOL WINAPI Proxy_GetFileVersionInfoByHandle(int a1, HANDLE a2, LPVOID a3, int a4) {
    typedef BOOL(WINAPI* PFN)(int, HANDLE, LPVOID, int);
    static PFN pfn = (PFN)GetOrigProc("GetFileVersionInfoByHandle");
    return pfn ? pfn(a1, a2, a3, a4) : FALSE;
}

BOOL WINAPI Proxy_GetFileVersionInfoExA(DWORD dwFlags, LPCSTR lpwstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData) {
    typedef BOOL(WINAPI* PFN)(DWORD, LPCSTR, DWORD, DWORD, LPVOID);
    static PFN pfn = (PFN)GetOrigProc("GetFileVersionInfoExA");
    return pfn ? pfn(dwFlags, lpwstrFilename, dwHandle, dwLen, lpData) : FALSE;
}

BOOL WINAPI Proxy_GetFileVersionInfoExW(DWORD dwFlags, LPCWSTR lpwstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData) {
    typedef BOOL(WINAPI* PFN)(DWORD, LPCWSTR, DWORD, DWORD, LPVOID);
    static PFN pfn = (PFN)GetOrigProc("GetFileVersionInfoExW");
    return pfn ? pfn(dwFlags, lpwstrFilename, dwHandle, dwLen, lpData) : FALSE;
}

DWORD WINAPI Proxy_GetFileVersionInfoSizeA(LPCSTR lptstrFilename, LPDWORD lpdwHandle) {
    typedef DWORD(WINAPI* PFN)(LPCSTR, LPDWORD);
    static PFN pfn = (PFN)GetOrigProc("GetFileVersionInfoSizeA");
    return pfn ? pfn(lptstrFilename, lpdwHandle) : 0;
}

DWORD WINAPI Proxy_GetFileVersionInfoSizeW(LPCWSTR lptstrFilename, LPDWORD lpdwHandle) {
    typedef DWORD(WINAPI* PFN)(LPCWSTR, LPDWORD);
    static PFN pfn = (PFN)GetOrigProc("GetFileVersionInfoSizeW");
    return pfn ? pfn(lptstrFilename, lpdwHandle) : 0;
}

DWORD WINAPI Proxy_GetFileVersionInfoSizeExA(DWORD dwFlags, LPCSTR lpwstrFilename, LPDWORD lpdwHandle) {
    typedef DWORD(WINAPI* PFN)(DWORD, LPCSTR, LPDWORD);
    static PFN pfn = (PFN)GetOrigProc("GetFileVersionInfoSizeExA");
    return pfn ? pfn(dwFlags, lpwstrFilename, lpdwHandle) : 0;
}

DWORD WINAPI Proxy_GetFileVersionInfoSizeExW(DWORD dwFlags, LPCWSTR lpwstrFilename, LPDWORD lpdwHandle) {
    typedef DWORD(WINAPI* PFN)(DWORD, LPCWSTR, LPDWORD);
    static PFN pfn = (PFN)GetOrigProc("GetFileVersionInfoSizeExW");
    return pfn ? pfn(dwFlags, lpwstrFilename, lpdwHandle) : 0;
}

DWORD WINAPI Proxy_VerFindFileA(DWORD uFlags, LPCSTR szFileName, LPCSTR szWinDir, LPCSTR szAppDir, LPSTR szCurDir, PUINT lpuCurDirLen, LPSTR szDestDir, PUINT lpuDestDirLen) {
    typedef DWORD(WINAPI* PFN)(DWORD, LPCSTR, LPCSTR, LPCSTR, LPSTR, PUINT, LPSTR, PUINT);
    static PFN pfn = (PFN)GetOrigProc("VerFindFileA");
    return pfn ? pfn(uFlags, szFileName, szWinDir, szAppDir, szCurDir, lpuCurDirLen, szDestDir, lpuDestDirLen) : 0;
}

DWORD WINAPI Proxy_VerFindFileW(DWORD uFlags, LPCWSTR szFileName, LPCWSTR szWinDir, LPCWSTR szAppDir, LPWSTR szCurDir, PUINT lpuCurDirLen, LPWSTR szDestDir, PUINT lpuDestDirLen) {
    typedef DWORD(WINAPI* PFN)(DWORD, LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, PUINT, LPWSTR, PUINT);
    static PFN pfn = (PFN)GetOrigProc("VerFindFileW");
    return pfn ? pfn(uFlags, szFileName, szWinDir, szAppDir, szCurDir, lpuCurDirLen, szDestDir, lpuDestDirLen) : 0;
}

DWORD WINAPI Proxy_VerInstallFileA(DWORD uFlags, LPCSTR szSrcFileName, LPCSTR szDestFileName, LPCSTR szSrcDir, LPCSTR szDestDir, LPCSTR szCurDir, LPSTR szTmpFile, PUINT lpuTmpFileLen) {
    typedef DWORD(WINAPI* PFN)(DWORD, LPCSTR, LPCSTR, LPCSTR, LPCSTR, LPCSTR, LPSTR, PUINT);
    static PFN pfn = (PFN)GetOrigProc("VerInstallFileA");
    return pfn ? pfn(uFlags, szSrcFileName, szDestFileName, szSrcDir, szDestDir, szCurDir, szTmpFile, lpuTmpFileLen) : 0;
}

DWORD WINAPI Proxy_VerInstallFileW(DWORD uFlags, LPCWSTR szSrcFileName, LPCWSTR szDestFileName, LPCWSTR szSrcDir, LPCWSTR szDestDir, LPCWSTR szCurDir, LPWSTR szTmpFile, PUINT lpuTmpFileLen) {
    typedef DWORD(WINAPI* PFN)(DWORD, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, PUINT);
    static PFN pfn = (PFN)GetOrigProc("VerInstallFileW");
    return pfn ? pfn(uFlags, szSrcFileName, szDestFileName, szSrcDir, szDestDir, szCurDir, szTmpFile, lpuTmpFileLen) : 0;
}

DWORD WINAPI Proxy_VerLanguageNameA(DWORD wLang, LPSTR szLang, DWORD cchLang) {
    typedef DWORD(WINAPI* PFN)(DWORD, LPSTR, DWORD);
    static PFN pfn = (PFN)GetOrigProc("VerLanguageNameA");
    return pfn ? pfn(wLang, szLang, cchLang) : 0;
}

DWORD WINAPI Proxy_VerLanguageNameW(DWORD wLang, LPWSTR szLang, DWORD cchLang) {
    typedef DWORD(WINAPI* PFN)(DWORD, LPWSTR, DWORD);
    static PFN pfn = (PFN)GetOrigProc("VerLanguageNameW");
    return pfn ? pfn(wLang, szLang, cchLang) : 0;
}

BOOL WINAPI Proxy_VerQueryValueA(LPCVOID pBlock, LPCSTR lpSubBlock, LPVOID* lplpBuffer, PUINT puLen) {
    typedef BOOL(WINAPI* PFN)(LPCVOID, LPCSTR, LPVOID*, PUINT);
    static PFN pfn = (PFN)GetOrigProc("VerQueryValueA");
    return pfn ? pfn(pBlock, lpSubBlock, lplpBuffer, puLen) : FALSE;
}

BOOL WINAPI Proxy_VerQueryValueW(LPCVOID pBlock, LPCWSTR lpSubBlock, LPVOID* lplpBuffer, PUINT puLen) {
    typedef BOOL(WINAPI* PFN)(LPCVOID, LPCWSTR, LPVOID*, PUINT);
    static PFN pfn = (PFN)GetOrigProc("VerQueryValueW");
    return pfn ? pfn(pBlock, lpSubBlock, lplpBuffer, puLen) : FALSE;
}

} 

static bool IsTargetProcess() {
    wchar_t exePath[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0) return false;

    _wcslwr_s(exePath, MAX_PATH);

    if (wcsstr(exePath, L"affinityarabic") != nullptr ||
        wcsstr(exePath, L"setup") != nullptr ||
        wcsstr(exePath, L"installer") != nullptr) {
        return false;
    }

    wchar_t* lastSlash = wcsrchr(exePath, L'\\');
    if (!lastSlash) lastSlash = wcsrchr(exePath, L'/');
    const wchar_t* fileName = lastSlash ? (lastSlash + 1) : exePath;

    return (wcscmp(fileName, L"affinity.exe") == 0);
}

static bool g_hookInitialized = false;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        LoadOriginalVersionDll();
        if (IsTargetProcess()) {
            AffinityArabic::AffinityHookManager::Initialize();
            g_hookInitialized = true;
        }
        break;
    case DLL_PROCESS_DETACH:
        if (g_hookInitialized) {
            AffinityArabic::AffinityHookManager::Shutdown();
            g_hookInitialized = false;
        }
        if (g_hOriginalVersion) {
            FreeLibrary(g_hOriginalVersion);
            g_hOriginalVersion = nullptr;
        }
        break;
    }
    return TRUE;
}
