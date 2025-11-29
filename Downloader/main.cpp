#include <windows.h>
#include <string>
#include <cwchar>
#include <winreg.h>
#include "gui.hpp"

// Download source URL (configurable)
static std::wstring DOWNLOAD_URL = L"DOWNLOAD-URL";

// Parse optional CLI argument for starting spinner angle (0..360)
static int parse_start_deg(){
    int deg = 0;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc >= 2){
        wchar_t* end = nullptr;
        long v = wcstol(argv[1], &end, 10);
        if (end && *end == L'\0') {
            deg = (int)v;
        }
    }
    if (argv) {
        LocalFree(argv);
    }
    if (deg < 0 || deg > 360) {
        deg = 0;
    }
    return deg;
}

// WinMain entry: create GUI and run
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int){
    DWORD val = 1, type = 0, sz = sizeof(DWORD);
    HKEY key;
    bool uac_on = true;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System", 0, KEY_READ, &key) == ERROR_SUCCESS){
        if (RegQueryValueExW(key, L"EnableLUA", NULL, &type, (LPBYTE)&val, &sz) == ERROR_SUCCESS){
            if (type == REG_DWORD) uac_on = (val == 1);
        }
        RegCloseKey(key);
    }
    if (!uac_on){
        MessageBoxW(NULL, L"UAC must be enabled to run Rusticaland Downloader.\nPlease enable User Account Control (EnableLUA) and restart.", L"Rusticaland Downloader", MB_ICONWARNING | MB_OK);
        return 1;
    }
    int start_deg = parse_start_deg();
    return gui_run_downloader(hInst, DOWNLOAD_URL.c_str(), start_deg);
}
