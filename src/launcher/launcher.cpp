#include <windows.h>
#include <string>
#include <vector>

typedef const wchar_t* (*FnProcessArabicText)(const wchar_t* input);
typedef const wchar_t* (*FnUnprocessArabicText)(const wchar_t* input);

static FnProcessArabicText g_ProcessArabic = nullptr;
static FnUnprocessArabicText g_UnprocessArabic = nullptr;

static bool HasArabic(const std::wstring& s) {
    for (wchar_t c : s) {
        if ((c >= 0x0600 && c <= 0x06FF) ||
            (c >= 0x0750 && c <= 0x077F) ||
            (c >= 0x08A0 && c <= 0x08FF) ||
            (c >= 0xFB50 && c <= 0xFDFF) ||
            (c >= 0xFE70 && c <= 0xFEFF)) {
            return true;
        }
    }
    return false;
}

static bool HasPresentationForms(const std::wstring& s) {
    for (wchar_t c : s) {
        if ((c >= 0xFB50 && c <= 0xFDFF) || (c >= 0xFE70 && c <= 0xFEFF)) {
            return true;
        }
    }
    return false;
}

static std::wstring GetClipboardText() {
    if (!OpenClipboard(NULL)) return L"";
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    std::wstring result;
    if (hData) {
        const wchar_t* pText = (const wchar_t*)GlobalLock(hData);
        if (pText) {
            result = pText;
            GlobalUnlock(hData);
        }
    }
    CloseClipboard();
    return result;
}

static bool SetClipboardText(const std::wstring& text) {
    if (!OpenClipboard(NULL)) return false;
    EmptyClipboard();
    size_t bytes = (text.length() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hMem) {
        void* pMem = GlobalLock(hMem);
        if (pMem) {
            memcpy(pMem, text.c_str(), bytes);
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        }
    }
    CloseClipboard();
    return true;
}

static void HandleHotkeyTransform() {
    keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
    keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0);
    keybd_event(VK_SHIFT, 0, KEYEVENTF_KEYUP, 0);
    Sleep(20);

    keybd_event(VK_CONTROL, 0, 0, 0);
    keybd_event('C', 0, 0, 0);
    keybd_event('C', 0, KEYEVENTF_KEYUP, 0);
    keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
    Sleep(60);

    std::wstring selected = GetClipboardText();
    if (selected.empty() || !HasArabic(selected)) {
        MessageBeep(MB_ICONWARNING);
        return;
    }

    std::wstring transformed;
    if (HasPresentationForms(selected) && g_UnprocessArabic) {
        transformed = g_UnprocessArabic(selected.c_str());
    } else if (g_ProcessArabic) {
        transformed = g_ProcessArabic(selected.c_str());
    }

    if (!transformed.empty()) {
        SetClipboardText(transformed);
        Sleep(20);
        keybd_event(VK_CONTROL, 0, 0, 0);
        keybd_event('V', 0, 0, 0);
        keybd_event('V', 0, KEYEVENTF_KEYUP, 0);
        keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
        MessageBeep(MB_OK);
    }
}

static std::wstring FindExistingPath(const std::vector<std::wstring>& candidates) {
    for (const auto& path : candidates) {
        if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
            return path;
        }
    }
    return L"";
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    wchar_t selfPath[MAX_PATH];
    GetModuleFileNameW(NULL, selfPath, MAX_PATH);

    std::wstring selfDir = selfPath;
    size_t lastSlash = selfDir.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) {
        selfDir = selfDir.substr(0, lastSlash);
    }

    std::vector<std::wstring> exeCandidates = {
        selfDir + L"\\Affinity.exe",
        selfDir + L"\\..\\Affinity.exe",
        selfDir + L"\\..\\..\\Affinity.exe",
        L"C:\\Program Files\\Affinity\\Affinity\\Affinity.exe",
        L"D:\\Program Files\\Affinity\\Affinity\\Affinity.exe",
        L"E:\\Program Files\\Affinity\\Affinity\\Affinity.exe"
    };

    std::wstring affinityExe = FindExistingPath(exeCandidates);
    if (affinityExe.empty()) {
        MessageBoxW(NULL, L"Affinity.exe not found! Please check installation directory.", L"Affinity Arabic Launcher", MB_ICONERROR);
        return 1;
    }

    std::wstring affinityDir = affinityExe;
    size_t exeSlash = affinityDir.find_last_of(L"\\/");
    if (exeSlash != std::wstring::npos) {
        affinityDir = affinityDir.substr(0, exeSlash);
    }

    std::vector<std::wstring> dllCandidates = {
        affinityDir + L"\\arabic_support\\bin\\affinity_arabic.dll",
        affinityDir + L"\\affinity_arabic.dll",
        selfDir + L"\\arabic_support\\bin\\affinity_arabic.dll",
        selfDir + L"\\affinity_arabic.dll",
        selfDir + L"\\bin\\affinity_arabic.dll"
    };

    std::wstring dllPath = FindExistingPath(dllCandidates);

    HMODULE hArabicDll = nullptr;
    if (!dllPath.empty()) {
        hArabicDll = LoadLibraryW(dllPath.c_str());
        if (hArabicDll) {
            g_ProcessArabic = (FnProcessArabicText)GetProcAddress(hArabicDll, "ProcessArabicText");
            g_UnprocessArabic = (FnUnprocessArabicText)GetProcAddress(hArabicDll, "UnprocessArabicText");
        }
    }

    STARTUPINFOW si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);

    std::wstring cmdLine = L"\"" + affinityExe + L"\"";
    LPCWSTR rawCmd = GetCommandLineW();
    if (rawCmd) {
        bool inQuote = false;
        const wchar_t* p = rawCmd;
        while (*p) {
            if (*p == L'"') inQuote = !inQuote;
            else if (*p == L' ' && !inQuote) {
                while (*p == L' ') p++;
                if (*p) {
                    cmdLine += L" ";
                    cmdLine += p;
                }
                break;
            }
            p++;
        }
    }

    BOOL created = CreateProcessW(
        affinityExe.c_str(),
        &cmdLine[0],
        NULL,
        NULL,
        FALSE,
        0,
        NULL,
        affinityDir.c_str(),
        &si,
        &pi
    );

    if (!created) {
        MessageBoxW(NULL, L"Failed to start Affinity process!", L"Affinity Arabic Launcher", MB_ICONERROR);
        return 1;
    }

    bool proxyAlreadyInstalled = (GetFileAttributesW((affinityDir + L"\\libstacktrace_orig.dll").c_str()) != INVALID_FILE_ATTRIBUTES);
    if (!proxyAlreadyInstalled && !dllPath.empty()) {
        Sleep(600); // Allow initialization
        size_t dllPathSize = (dllPath.length() + 1) * sizeof(wchar_t);
        void* remoteMem = VirtualAllocEx(pi.hProcess, NULL, dllPathSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (remoteMem) {
            if (WriteProcessMemory(pi.hProcess, remoteMem, dllPath.c_str(), dllPathSize, NULL)) {
                HMODULE hK32 = GetModuleHandleW(L"kernel32.dll");
                void* pLoadLib = (void*)GetProcAddress(hK32, "LoadLibraryW");
                if (pLoadLib) {
                    HANDLE hThread = CreateRemoteThread(pi.hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)pLoadLib, remoteMem, 0, NULL);
                    if (hThread) {
                        WaitForSingleObject(hThread, 5000);
                        CloseHandle(hThread);
                    }
                }
            }
            VirtualFreeEx(pi.hProcess, remoteMem, 0, MEM_RELEASE);
        }
    }

    RegisterHotKey(NULL, 1, MOD_NOREPEAT, VK_F8);
    RegisterHotKey(NULL, 2, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'A');

    MSG msg;
    while (true) {
        DWORD exitCode = 0;
        if (GetExitCodeProcess(pi.hProcess, &exitCode) && exitCode != STILL_ACTIVE) {
            break;
        }

        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                goto cleanup;
            }
            if (msg.message == WM_HOTKEY) {
                HandleHotkeyTransform();
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(50);
    }

cleanup:
    UnregisterHotKey(NULL, 1);
    UnregisterHotKey(NULL, 2);
    if (hArabicDll) {
        FreeLibrary(hArabicDll);
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}
