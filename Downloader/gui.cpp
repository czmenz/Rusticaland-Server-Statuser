#include <windows.h>
#include <winhttp.h>
#include <d3d9.h>
#include <dwmapi.h>
#include <string>
#include <vector>
#include <cmath>
#include <cwchar>
#include <shlobj.h>
#include <knownfolders.h>
#include <wintrust.h>
#include <Softpub.h>

#include "gui.hpp"
#include "../External/ImGUI/imgui.h"
#include "../External/ImGUI/imgui_impl_win32.h"
#include "../External/ImGUI/imgui_impl_dx9.h"

// Optional: Verify Authenticode signature of a file
static bool verify_signature(const std::wstring& path);
// Path for final checker exe in %TEMP%
static std::wstring temp_checker() {
    wchar_t tmp[MAX_PATH]{};
    std::wstring path;
    DWORD n = GetTempPathW(MAX_PATH, tmp);
    if (n == 0 || tmp[0] == L'\0'){
        wchar_t env[MAX_PATH]{}; DWORD m = GetEnvironmentVariableW(L"TEMP", env, MAX_PATH);
        if (m == 0 || env[0] == L'\0'){
            wchar_t win[MAX_PATH]{}; UINT w = GetWindowsDirectoryW(win, MAX_PATH);
            path = std::wstring(win) + L"\\Temp\\";
        } else { path = std::wstring(env); }
    } else { path = std::wstring(tmp); }
    if (!path.empty() && path.back() != L'\\') {
        path.push_back(L'\\');
    }
    return path + L"Rusticaland-Checker.exe";
}
// Temporary path for download payload latest.exe in %TEMP%
static std::wstring temp_latest() {
    wchar_t tmp[MAX_PATH]{};
    std::wstring path;
    DWORD n = GetTempPathW(MAX_PATH, tmp);
    if (n == 0 || tmp[0] == L'\0'){
        wchar_t env[MAX_PATH]{}; DWORD m = GetEnvironmentVariableW(L"TEMP", env, MAX_PATH);
        if (m == 0 || env[0] == L'\0'){
            wchar_t win[MAX_PATH]{}; UINT w = GetWindowsDirectoryW(win, MAX_PATH);
            path = std::wstring(win) + L"\\Temp\\";
        } else { path = std::wstring(env); }
    } else { path = std::wstring(tmp); }
    if (!path.empty() && path.back() != L'\\') {
        path.push_back(L'\\');
    }
    return path + L"latest.exe";
}

// Minimal URL parser (scheme://host[:port]/path)
static bool parse_url(
    const std::wstring& url,
    std::wstring& host,
    std::wstring& path,
    bool& https,
    INTERNET_PORT& port
) {
    https = false;
    port = INTERNET_DEFAULT_HTTP_PORT;
    host.clear();
    path = L"/";

    size_t start = 0;
    if (url.rfind(L"http://", 0) == 0) {
        start = 7;
        https = false;
        port = INTERNET_DEFAULT_HTTP_PORT;
    } else if (url.rfind(L"https://", 0) == 0) {
        start = 8;
        https = true;
        port = INTERNET_DEFAULT_HTTPS_PORT;
    } else {
        return false;
    }

    size_t slash = url.find(L'/', start);
    std::wstring hostport = (slash == std::wstring::npos)
        ? url.substr(start)
        : url.substr(start, slash - start);
    path = (slash == std::wstring::npos) ? L"/" : url.substr(slash);

    size_t colon = hostport.find(L':');
    if (colon != std::wstring::npos) {
        host = hostport.substr(0, colon);
        try {
            port = (INTERNET_PORT)std::stoi(hostport.substr(colon + 1));
        } catch (...) {
            return false;
        }
    } else {
        host = hostport;
    }
    return !host.empty();
}

// Streaming HTTP GET to file
static bool download(
    const std::wstring& host,
    const std::wstring& path,
    INTERNET_PORT port,
    bool https,
    const std::wstring& out
) {
    HINTERNET h = WinHttpOpen(
        L"RusticalandDownloader/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );
    if (!h) return false;

    HINTERNET c = WinHttpConnect(h, host.c_str(), port, 0);
    if (!c) { WinHttpCloseHandle(h); return false; }

    DWORD flags = https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET r = WinHttpOpenRequest(
        c,
        L"GET",
        path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags
    );
    if (!r) { WinHttpCloseHandle(c); WinHttpCloseHandle(h); return false; }

    BOOL ok = WinHttpSendRequest(
        r,
        WINHTTP_NO_ADDITIONAL_HEADERS,
        0,
        WINHTTP_NO_REQUEST_DATA,
        0,
        0,
        0
    );
    if (ok) ok = WinHttpReceiveResponse(r, nullptr);
    if (!ok) { WinHttpCloseHandle(r); WinHttpCloseHandle(c); WinHttpCloseHandle(h); return false; }

    HANDLE f = CreateFileW(
        out.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_TEMPORARY,
        nullptr
    );
    if (f == INVALID_HANDLE_VALUE) { WinHttpCloseHandle(r); WinHttpCloseHandle(c); WinHttpCloseHandle(h); return false; }

    DWORD avail = 0;
    BOOL q = FALSE;
    do {
        q = WinHttpQueryDataAvailable(r, &avail);
        if (!q || !avail) break;

        std::vector<char> buf(avail);
        DWORD rd = 0;
        WinHttpReadData(r, buf.data(), avail, &rd);

        DWORD wr = 0;
        WriteFile(f, buf.data(), rd, &wr, nullptr);
    } while (avail);

    CloseHandle(f);
    WinHttpCloseHandle(r);
    WinHttpCloseHandle(c);
    WinHttpCloseHandle(h);
    return true;
}

// D3D9 globals
static LPDIRECT3D9 g_pD3D = nullptr;
static LPDIRECT3DDEVICE9 g_pd3dDevice = nullptr;
static D3DPRESENT_PARAMETERS g_d3dpp = {};
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

// Minimal window proc: route to ImGui, block context menu, handle destroy
static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return 1;
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    if (msg == WM_NCRBUTTONUP || msg == WM_CONTEXTMENU) return 0;
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// Create D3D9 device with alpha blending enabled
static bool CreateDeviceD3D(HWND hWnd) {
    g_pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    if (!g_pD3D) return false;

    ZeroMemory(&g_d3dpp, sizeof(g_d3dpp));
    g_d3dpp.Windowed = TRUE;
    g_d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    g_d3dpp.BackBufferFormat = D3DFMT_A8R8G8B8;
    g_d3dpp.EnableAutoDepthStencil = TRUE;
    g_d3dpp.AutoDepthStencilFormat = D3DFMT_D16;
    g_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;

    if (g_pD3D->CreateDevice(
            D3DADAPTER_DEFAULT,
            D3DDEVTYPE_HAL,
            hWnd,
            D3DCREATE_HARDWARE_VERTEXPROCESSING,
            &g_d3dpp,
            &g_pd3dDevice) < 0) {
        return false;
    }
    g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    g_pd3dDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    g_pd3dDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    return true;
}

// Destroy D3D resources
static void CleanupDeviceD3D() {
    if (g_pd3dDevice) {
        g_pd3dDevice->Release();
        g_pd3dDevice = nullptr;
    }
    if (g_pD3D) {
        g_pD3D->Release();
        g_pD3D = nullptr;
    }
}

// Handle device lost/reset
static void ResetDevice() {
    ImGui_ImplDX9_InvalidateDeviceObjects();
    if (g_pd3dDevice->Reset(&g_d3dpp) == D3DERR_INVALIDCALL) return;
    ImGui_ImplDX9_CreateDeviceObjects();
}

static void DrawSpinner(const ImVec2& center, float radius, float thickness, ImU32 color, float speed) {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    float t = (float)(ImGui::GetTime() * speed);
    const float PI = 3.1415926535f;
    float start = std::fmod(t, 1.0f) * 2.0f * PI;
    float end = start + PI * 1.5f;
    const int segments = 64;
    dl->PathClear();
    for (int i = 0; i < segments; ++i) {
        float a = start + (end - start) * (float(i) / float(segments));
        ImVec2 p(center.x + std::cos(a) * radius, center.y + std::sin(a) * radius);
        dl->PathLineTo(p);
    }
    dl->PathStroke(color, 0, thickness);
}

// Entry point for downloader GUI
int gui_run_downloader(HINSTANCE hInst, const wchar_t* url, int start_deg) {
    // Register window class
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"RusticalandDownloaderWnd";
    RegisterClassExW(&wc);

    // Centered size/position
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int winW = 420;
    int winH = 260;
    int winX = (sw - winW) / 2;
    int winY = (sh - winH) / 2;

    // Layered popup for transparency
    HWND hWnd = CreateWindowExW(
        WS_EX_LAYERED,
        wc.lpszClassName,
        L"Rusticaland",
        WS_POPUP,
        winX,
        winY,
        winW,
        winH,
        nullptr,
        nullptr,
        wc.hInstance,
        nullptr
    );
    SetLayeredWindowAttributes(hWnd, 0, 255, LWA_ALPHA);
    {
        MARGINS m = { -1 };
        DwmExtendFrameIntoClientArea(hWnd, &m);
    }

    if (!CreateDeviceD3D(hWnd)) {
        CleanupDeviceD3D();
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }
    ShowWindow(hWnd, SW_SHOWDEFAULT);
    SetWindowPos(hWnd, nullptr, winX, winY, winW, winH, SWP_NOZORDER | SWP_NOSIZE);
    UpdateWindow(hWnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui_ImplWin32_Init(hWnd);
    ImGui_ImplDX9_Init(g_pd3dDevice);

    // Prepare download parameters
    std::wstring host, path;
    bool https = false;
    INTERNET_PORT port = 0;
    std::wstring out = temp_checker();
    std::wstring latest = temp_latest();
    bool okParse = parse_url(std::wstring(url), host, path, https, port);

    // Background download thread
    HANDLE hThread = nullptr;
    struct DLArgs { std::wstring host, path, save; INTERNET_PORT port; bool https; } args;

    // UI state
    bool done = false;
    bool success = false;
    float start_phase = (float)start_deg * 3.14159265f / 180.0f;
    double t_start = ImGui::GetTime();
    bool expanded = false;
    if (okParse) {
        args.host = host; args.path = path; args.save = latest; args.port = port; args.https = https;
        hThread = CreateThread(nullptr, 0, [](LPVOID p)->DWORD {
            auto* a = (DLArgs*)p;
            return download(a->host, a->path, a->port, a->https, a->save) ? 1 : 0;
        }, &args, 0, nullptr);
    }

    // Message loop + render
    MSG msg{};
    while (msg.message != WM_QUIT) {
        if (PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            continue;
        }

        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Full-window overlay with box + spinner
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(420, 260), ImGuiCond_Always);
        ImGui::Begin(
            "##loader",
            nullptr,
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoScrollbar
        );
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 win_pos = ImGui::GetWindowPos();
        ImU32 col_box = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f/255.0f, 15.0f/255.0f, 53.0f/255.0f, 1.0f));

        ImVec2 c(win_pos.x + 210.0f, win_pos.y + 130.0f);
        double t_now = ImGui::GetTime();
        if (!expanded && (t_now - t_start) >= 3.0) expanded = true;
        float box_half = 62.5f;

        ImVec2 pmin(c.x - box_half, c.y - box_half);
        ImVec2 pmax(c.x + box_half, c.y + box_half);
        dl->AddRectFilled(pmin, pmax, col_box, 8.0f);

        float a = (float)t_now * 6.0f + start_phase;
        ImU32 white = IM_COL32(255, 255, 255, 255);
        dl->PathClear();
        dl->PathArcTo(c, 18.0f, a, a + 3.1415926f * 0.9f, 32);
        dl->PathStroke(white, false, 3.0f);
        ImGui::End();

        ImGui::EndFrame();
        g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
        g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        g_pd3dDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
        g_pd3dDevice->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_RGBA(0, 0, 0, 0), 1.0f, 0);
        if (g_pd3dDevice->BeginScene() == D3D_OK) {
            ImGui::Render();
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
            g_pd3dDevice->EndScene();
        }
        HRESULT present = g_pd3dDevice->Present(nullptr, nullptr, nullptr, nullptr);
        if (present == D3DERR_DEVICELOST && g_pd3dDevice->TestCooperativeLevel() == D3DERR_DEVICENOTRESET) ResetDevice();

        if (!done) {
            if (hThread) {
                DWORD code = 0;
                if (WaitForSingleObject(hThread, 0) == WAIT_OBJECT_0) {
                    GetExitCodeThread(hThread, &code);
                    success = (code == 1);
                    done = true;
                    CloseHandle(hThread);
                }
            } else {
                done = true;
                success = false;
            }
        }
        if (done && ((ImGui::GetTime() - t_start) >= 3.0)) {
            break;
        }
    }

    if (!okParse || !success) {
        ImGui_ImplDX9_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        CleanupDeviceD3D();
        DestroyWindow(hWnd);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }
    STARTUPINFOW si{}; si.cb = sizeof(si); PROCESS_INFORMATION pi{};
    // Move latest.exe to final name; fallback to copy if needed
    if (GetFileAttributesW(latest.c_str()) != INVALID_FILE_ATTRIBUTES) {
        MoveFileExW(latest.c_str(), out.c_str(), MOVEFILE_REPLACE_EXISTING);
        if (GetFileAttributesW(out.c_str()) == INVALID_FILE_ATTRIBUTES) {
            CopyFileW(latest.c_str(), out.c_str(), FALSE);
            DeleteFileW(latest.c_str());
        }
    }
    int deg = 0; if (hThread==nullptr) deg = 0; else { double ang_t = ImGui::GetTime(); double a = ang_t*6.0; double degf = fmod(a*180.0/3.14159265, 360.0); deg = (int)degf; }
    // Launch Checker externally (no CMD), pass angle argument
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.hwnd = NULL;
    sei.lpVerb = L"open";
    sei.lpFile = out.c_str();
    std::wstring params = std::to_wstring(deg);
    sei.lpParameters = params.c_str();
    sei.lpDirectory = NULL;
    sei.nShow = SW_SHOWDEFAULT;
    if (!ShellExecuteExW(&sei) || !sei.hProcess) {
        ImGui_ImplDX9_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        CleanupDeviceD3D();
        DestroyWindow(hWnd);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        DeleteFileW(out.c_str());
        DeleteFileW(latest.c_str());
        return 1;
    }
    WaitForInputIdle(sei.hProcess, 5000);
    Sleep(300);
    ImGui_ImplDX9_Shutdown(); ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext();
    CleanupDeviceD3D(); DestroyWindow(hWnd); UnregisterClassW(wc.lpszClassName, wc.hInstance);
    
    // Background cleanup of the executable after Checker exits
    wchar_t ps[1024];
    swprintf(
        ps,
        1024,
        L"powershell -NoProfile -WindowStyle Hidden -Command \"Wait-Process -Id %lu; Start-Sleep -Milliseconds 200; Remove-Item -Force '%s'\"",
        GetProcessId(sei.hProcess),
        out.c_str()
    );
    std::wstring cmd = L"/C ";
    cmd += ps;
    STARTUPINFOW si2{}; si2.cb = sizeof(si2); PROCESS_INFORMATION pi2{};
    ShellExecuteW(nullptr, L"open", L"cmd.exe", cmd.c_str(), nullptr, SW_HIDE);
    if (pi2.hThread) CloseHandle(pi2.hThread);
    if (pi2.hProcess) CloseHandle(pi2.hProcess);
    CloseHandle(sei.hProcess);
    return 0;
}
static bool verify_signature(const std::wstring& path) {
    WINTRUST_FILE_INFO file_info{};
    file_info.cbStruct = sizeof(file_info);
    file_info.pcwszFilePath = path.c_str();
    GUID policy = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    WINTRUST_DATA trust_data{};
    trust_data.cbStruct = sizeof(trust_data);
    trust_data.dwUIChoice = WTD_UI_NONE;
    trust_data.fdwRevocationChecks = WTD_REVOKE_NONE;
    trust_data.dwUnionChoice = WTD_CHOICE_FILE;
    trust_data.pFile = &file_info;
    trust_data.dwStateAction = WTD_STATEACTION_VERIFY;
    trust_data.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;
    LONG status = WinVerifyTrust(NULL, &policy, &trust_data);
    trust_data.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(NULL, &policy, &trust_data);
    return status == ERROR_SUCCESS;
}
