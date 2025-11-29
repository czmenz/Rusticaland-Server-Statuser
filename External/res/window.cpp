#include <windows.h>
#include <d3d11.h>
#include <tchar.h>
#include "../Externals/ImGUI/imgui.h"
#include "../Externals/ImGUI/imgui_impl_win32.h"
#include "../Externals/ImGUI/imgui_impl_dx11.h"
#include <wincodec.h>
#include <urlmon.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <string>
#include <vector>
#include <thread>
#include <fstream>
#include <algorithm>
#include <cstring>
typedef unsigned char byte;
#ifndef PROGMEM
#define PROGMEM
#endif
#include "../Externals/fonts/Rubik-Regular.c"
#include "../Externals/fonts/Rubik-Medium.c"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

static ID3D11Device*              g_pd3dDevice = nullptr;
static ID3D11DeviceContext*       g_pd3dDeviceContext = nullptr;
static IDXGISwapChain*            g_pSwapChain = nullptr;
static ID3D11RenderTargetView*    g_mainRenderTargetView = nullptr;
static int                        g_winW = 500;
static int                        g_winH = 400;
static int                        g_winX = 100;
static int                        g_winY = 100;
static ImFont*                    g_fontRubikMedium18 = nullptr;
static ImFont*                    g_fontRubikRegular14 = nullptr;
static ImFont*                    g_fontRubikMedium15 = nullptr;
static ImFont*                    g_fontRubikRegular16 = nullptr;
static ImFont*                    g_fontRubikMedium20 = nullptr;
static ImFont*                    g_fontRubikMedium165 = nullptr;
static ImFont*                    g_fontRubikRegular15 = nullptr;
static bool                       g_showDetail = false;
static bool                       g_detailAnimActive = false;
static double                     g_detailAnimStart = 0.0;
static bool                       g_detailClosingActive = false;
static double                     g_detailCloseStart = 0.0;
static int                        g_detailMode = 0; // 0:none, 1:cs2, 2:updates
static bool                       g_updatesDragActive = false;
static float                      g_updatesDragStartY = 0.0f;
static float                      g_updatesDragScrollStart = 0.0f;
static float                      g_updatesScrollVel = 0.0f;
static double                     g_updatesLastTime = 0.0;
static float                      g_cs2BulletScroll = 0.0f;
static double                     g_cs2BulletLastTime = 0.0;
static ID3D11ShaderResourceView*  g_playIconSRV = nullptr;
static int                        g_playIconW = 0;
static int                        g_playIconH = 0;
static bool                       g_personaLoaded = false;
static std::string                g_personaName;
static std::string                g_steamId;
static ID3D11ShaderResourceView*  g_leftIconSRV = nullptr;
static int                        g_leftIconW = 0;
static int                        g_leftIconH = 0;
static ID3D11ShaderResourceView*  g_cs2IconSRV = nullptr;
static int                        g_cs2IconW = 0;
static int                        g_cs2IconH = 0;
static const char*                g_loaderVersion = "1.0.0";
struct UpdateEntry { std::string date; std::vector<std::string> lines; double ts; };
static std::vector<UpdateEntry>   g_updatesFeed;
static std::vector<UpdateEntry>   g_cs2UpdatesFeed;
static bool                       g_updatesLoaded = false;
static bool                       g_cs2UpdatesLoaded = false;
static bool                       g_versionMismatch = false;
static bool                       g_configFetchStarted = false;
static bool                       g_versionMsgShown = false;
static HWND                       g_hwnd = NULL;

static void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer = nullptr;
    if (g_pSwapChain && SUCCEEDED(g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer))))
    {
        if (g_pd3dDevice)
            g_pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL, &g_mainRenderTargetView);
        if (pBackBuffer) pBackBuffer->Release();
    }
}

static void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

static bool LoadTextureFromFileWIC(ID3D11Device* device, const wchar_t* filename, ID3D11ShaderResourceView** out_srv, int* out_w, int* out_h)
{
    if (!device || !filename || !out_srv) return false;
    *out_srv = nullptr;
    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;

    IWICImagingFactory* factory = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr) || !factory) return false;

    IWICBitmapDecoder* decoder = nullptr;
    hr = factory->CreateDecoderFromFilename(filename, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr) || !decoder) { factory->Release(); return false; }

    IWICBitmapFrameDecode* frame = nullptr;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr) || !frame) { decoder->Release(); factory->Release(); return false; }

    IWICFormatConverter* converter = nullptr;
    hr = factory->CreateFormatConverter(&converter);
    if (FAILED(hr) || !converter) { frame->Release(); decoder->Release(); factory->Release(); return false; }

    hr = converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) { converter->Release(); frame->Release(); decoder->Release(); factory->Release(); return false; }

    UINT width = 0, height = 0;
    converter->GetSize(&width, &height);
    const UINT stride = width * 4;
    const UINT buffer_size = stride * height;

    std::vector<unsigned char> buffer(buffer_size);
    hr = converter->CopyPixels(nullptr, stride, buffer_size, buffer.data());
    if (FAILED(hr)) { converter->Release(); frame->Release(); decoder->Release(); factory->Release(); return false; }

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA init_data = {};
    init_data.pSysMem = buffer.data();
    init_data.SysMemPitch = stride;

    ID3D11Texture2D* tex = nullptr;
    hr = device->CreateTexture2D(&desc, &init_data, &tex);
    if (FAILED(hr) || !tex) { converter->Release(); frame->Release(); decoder->Release(); factory->Release(); return false; }

    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
    srv_desc.Format = desc.Format;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MipLevels = 1;

    ID3D11ShaderResourceView* srv = nullptr;
    hr = device->CreateShaderResourceView(tex, &srv_desc, &srv);
    tex->Release();
    if (FAILED(hr) || !srv) { converter->Release(); frame->Release(); decoder->Release(); factory->Release(); return false; }

    if (out_w) *out_w = (int)width;
    if (out_h) *out_h = (int)height;
    *out_srv = srv;

    converter->Release();
    frame->Release();
    decoder->Release();
    factory->Release();
    return true;
}

static bool DownloadTextToString(const wchar_t* url, std::string& out)
{
    wchar_t tempPath[MAX_PATH]; GetTempPathW(MAX_PATH, tempPath);
    wchar_t filePath[MAX_PATH]; wsprintfW(filePath, L"%s%cnet_%u.json", tempPath, L'\\', (unsigned)GetTickCount());
    HRESULT hr = URLDownloadToFileW(NULL, url, filePath, 0, NULL);
    if (FAILED(hr)) return false;
    std::ifstream ifs; ifs.open(std::wstring(filePath));
    if (!ifs.is_open()) { DeleteFileW(filePath); return false; }
    std::string s((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    ifs.close();
    DeleteFileW(filePath);
    out = s;
    return true;
}

static int ParseInt(const char* s, int n)
{
    int v = 0; for (int i=0;i<n;i++) { char c = s[i]; if (c<'0'||c>'9') break; v = v*10 + (c-'0'); }
    return v;
}

static double ParseDateTs(const std::string& s)
{
    if (s.size() < 16) return 0.0;
    int dd = ParseInt(s.c_str()+0,2);
    int mm = ParseInt(s.c_str()+3,2);
    int yyyy = ParseInt(s.c_str()+6,4);
    int HH = ParseInt(s.c_str()+11,2);
    int MM = ParseInt(s.c_str()+14,2);
    return (double)(yyyy*1000000 + mm*10000 + dd*100 + HH*1 + MM*0.01);
}

static bool StartsWith(const std::string& s, const char* p)
{
    size_t n = strlen(p); if (s.size() < n) return false; return memcmp(s.c_str(), p, n) == 0;
}

static void LoadRemoteConfig()
{
    std::string vs; if (DownloadTextToString(L"http://89.203.249.22:8080/loader/version.json", vs))
    {
        size_t k = vs.find("\"version\"");
        if (k != std::string::npos)
        {
            size_t colon = vs.find(':', k);
            if (colon != std::string::npos)
            {
                size_t v1 = vs.find('"', colon);
                if (v1 != std::string::npos)
                {
                    size_t v2 = vs.find('"', v1+1);
                    if (v2 != std::string::npos)
                    {
                        std::string ver = vs.substr(v1+1, v2-(v1+1));
                        if (ver != std::string(g_loaderVersion))
                            g_versionMismatch = true;
                    }
                }
            }
        }
    }
    std::string cs; if (DownloadTextToString(L"http://89.203.249.22:8080/loader/changelog.json", cs))
    {
        std::vector<UpdateEntry> items;
        size_t i = 0; while (true)
        {
            size_t b = cs.find("{", i); if (b == std::string::npos) break;
            size_t e = cs.find("}", b+1); if (e == std::string::npos) break;
            std::string obj = cs.substr(b, e-b+1);
            size_t pd = obj.find("\"date\"");
            if (pd != std::string::npos)
            {
                size_t colon = obj.find(':', pd);
                if (colon != std::string::npos)
                {
                    size_t v1 = obj.find('"', colon);
                    if (v1 != std::string::npos)
                    {
                        size_t v2 = obj.find('"', v1+1);
                        if (v2 != std::string::npos)
                        {
                            std::string date = obj.substr(v1+1, v2-(v1+1));
                            UpdateEntry ue; ue.date = date; ue.ts = ParseDateTs(date);
                            size_t pc = 0; int idx = 1;
                            while (true)
                            {
                                std::string key = std::string("\"change") + std::to_string(idx) + "\"";
                                size_t pk = obj.find(key, pc);
                                if (pk == std::string::npos) break;
                                size_t colon2 = obj.find(':', pk);
                                if (colon2 == std::string::npos) break;
                                size_t c1 = obj.find('"', colon2);
                                if (c1 == std::string::npos) break;
                                size_t c2 = obj.find('"', c1+1);
                                if (c2 == std::string::npos) break;
                    std::string val = obj.substr(c1+1, c2-(c1+1));
                    ue.lines.push_back(std::string("-\xC2\xA0") + val);
                    pc = c2+1; idx++;
                }
                            items.push_back(ue);
                        }
                    }
                }
            }
            i = e+1;
        }
        std::sort(items.begin(), items.end(), [](const UpdateEntry& a, const UpdateEntry& b){ return a.ts > b.ts; });
        g_updatesFeed = items; g_updatesLoaded = true;
    }

    std::string cs2; bool cs2_ok = DownloadTextToString(L"http://89.203.249.22:8080/loader/games/cs2.json", cs2);
    if (cs2_ok)
    {
        std::vector<UpdateEntry> items;
        size_t i = 0; while (true)
        {
            size_t b = cs2.find("{", i); if (b == std::string::npos) break;
            size_t e = cs2.find("}", b+1); if (e == std::string::npos) break;
            std::string obj = cs2.substr(b, e-b+1);
            size_t pd = obj.find("\"date\"");
            if (pd != std::string::npos)
            {
                size_t colon = obj.find(':', pd);
                if (colon != std::string::npos)
                {
                    size_t v1 = obj.find('"', colon);
                    if (v1 != std::string::npos)
                    {
                        size_t v2 = obj.find('"', v1+1);
                        if (v2 != std::string::npos)
                        {
                            std::string date = obj.substr(v1+1, v2-(v1+1));
                            UpdateEntry ue; ue.date = date; ue.ts = ParseDateTs(date);
                            size_t pc = 0; int idx = 1;
                            while (true)
                            {
                                std::string key = std::string("\"change") + std::to_string(idx) + "\"";
                                size_t pk = obj.find(key, pc);
                                if (pk == std::string::npos) break;
                                size_t colon2 = obj.find(':', pk);
                                if (colon2 == std::string::npos) break;
                                size_t c1 = obj.find('"', colon2);
                                if (c1 == std::string::npos) break;
                                size_t c2 = obj.find('"', c1+1);
                                if (c2 == std::string::npos) break;
                                std::string val = obj.substr(c1+1, c2-(c1+1));
                                ue.lines.push_back(std::string("-\xC2\xA0") + val);
                                pc = c2+1; idx++;
                            }
                            items.push_back(ue);
                        }
                    }
                }
            }
            i = e+1;
        }
        std::sort(items.begin(), items.end(), [](const UpdateEntry& a, const UpdateEntry& b){ return a.ts > b.ts; });
        g_cs2UpdatesFeed = items; g_cs2UpdatesLoaded = !items.empty();
    }
    if (!g_cs2UpdatesLoaded && g_updatesLoaded)
    {
        g_cs2UpdatesFeed = g_updatesFeed;
        g_cs2UpdatesLoaded = true;
    }
}

static bool CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    if (D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext) != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

static void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return 1;
    switch (msg)
    {
    case WM_NCHITTEST:
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hWnd, &pt);
        int header_h = 60;
        int btn_min_x1 = 431, btn_min_x2 = 431 + 24;
        int btn_close_x1 = 461, btn_close_x2 = 461 + 24;
        int btn_y1 = 22, btn_y2 = 22 + 24;
        if (pt.y >= 0 && pt.y < header_h) {
            if (!((pt.x >= btn_min_x1 && pt.x <= btn_min_x2 && pt.y >= btn_y1 && pt.y <= btn_y2) ||
                  (pt.x >= btn_close_x1 && pt.x <= btn_close_x2 && pt.y >= btn_y1 && pt.y <= btn_y2)))
                return HTCAPTION;
        }
        break;
    }
    case WM_SIZE:
        if (g_pd3dDevice != NULL && wParam != SIZE_MINIMIZED)
        {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU)
            return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int)
{
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L, hInstance, NULL, NULL, NULL, NULL, _T("LoaderWindow"), NULL };
    RegisterClassEx(&wc);
    HWND hwnd = CreateWindowEx(WS_EX_LAYERED, wc.lpszClassName, _T("Loader"), WS_POPUP, 100, 100, g_winW, g_winH, NULL, NULL, wc.hInstance, NULL);
    if (!hwnd) return 0;
    SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
    {
        int sw = GetSystemMetrics(SM_CXSCREEN);
        int sh = GetSystemMetrics(SM_CYSCREEN);
        g_winX = (sw - g_winW) / 2;
        g_winY = (sh - g_winH) / 2;
        SetWindowPos(hwnd, HWND_TOPMOST, g_winX, g_winY, g_winW, g_winH, SWP_SHOWWINDOW | SWP_NOACTIVATE);
    }
    {
        MARGINS m = { -1 };
        DwmExtendFrameIntoClientArea(hwnd, &m);
    }

    if (!CreateDeviceD3D(hwnd)) { CleanupDeviceD3D(); UnregisterClass(wc.lpszClassName, wc.hInstance); return 0; }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.Colors[ImGuiCol_WindowBg].w = 0.0f;
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    {
        ImFontConfig cfg;
        cfg.FontDataOwnedByAtlas = false;
        g_fontRubikMedium18 = io.Fonts->AddFontFromMemoryTTF((void*)Rubik_Medium, (int)sizeof(Rubik_Medium), 18.0f, &cfg);
        g_fontRubikRegular14 = io.Fonts->AddFontFromMemoryTTF((void*)Rubik_Regular, (int)sizeof(Rubik_Regular), 14.0f, &cfg);
        g_fontRubikMedium15 = io.Fonts->AddFontFromMemoryTTF((void*)Rubik_Medium, (int)sizeof(Rubik_Medium), 15.0f, &cfg);
        g_fontRubikRegular16 = io.Fonts->AddFontFromMemoryTTF((void*)Rubik_Regular, (int)sizeof(Rubik_Regular), 16.0f, &cfg);
        g_fontRubikMedium20 = io.Fonts->AddFontFromMemoryTTF((void*)Rubik_Medium, (int)sizeof(Rubik_Medium), 20.0f, &cfg);
        g_fontRubikMedium165 = io.Fonts->AddFontFromMemoryTTF((void*)Rubik_Medium, (int)sizeof(Rubik_Medium), 16.5f, &cfg);
        g_fontRubikRegular15 = io.Fonts->AddFontFromMemoryTTF((void*)Rubik_Regular, (int)sizeof(Rubik_Regular), 15.0f, &cfg);
    }

    g_hwnd = hwnd;

    MSG msg; ZeroMemory(&msg, sizeof(msg));
    while (msg.message != WM_QUIT)
    {
        if (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            continue;
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2((float)g_winW, (float)g_winH), ImGuiCond_Always);
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
        ImGui::Begin("Loader", nullptr, flags);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 win_pos = ImGui::GetWindowPos();
        ImU32 col_bg = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f/255.0f, 15.0f/255.0f, 53.0f/255.0f, 1.0f));

        static double sIntroStart = 0.0;
        if (sIntroStart == 0.0) {
            sIntroStart = ImGui::GetTime();
            if (!g_configFetchStarted) {
                g_configFetchStarted = true;
                std::thread([](){ LoadRemoteConfig(); }).detach();
            }
        }
        if (g_versionMismatch && !g_versionMsgShown)
        {
            g_versionMsgShown = true;
            MessageBoxW(g_hwnd, L"Your launcher is out of date. Please update.", L"Version Mismatch", MB_ICONERROR);
            PostQuitMessage(0);
        }
        double eIntro = ImGui::GetTime() - sIntroStart;
        if (eIntro < 3.0)
        {
            ImVec2 c(win_pos.x + g_winW * 0.5f, win_pos.y + g_winH * 0.5f);
            ImVec2 pmin(c.x - 62.5f, c.y - 62.5f);
            ImVec2 pmax(c.x + 62.5f, c.y + 62.5f);
            ImU32 col_intro = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f/255.0f, 15.0f/255.0f, 53.0f/255.0f, 1.0f));
            dl->AddRectFilled(pmin, pmax, col_intro, 8.0f);
            ImU32 col_w = ImGui::ColorConvertFloat4ToU32(ImVec4(1,1,1,1));
            float r = 18.0f;
            float a = (float)ImGui::GetTime() * 6.0f;
            dl->PathClear();
            dl->PathArcTo(c, r, a, a + 3.1415926f * 0.9f, 32);
            dl->PathStroke(col_w, false, 3.0f);
            ImGui::End();
            ImGui::Render();
            const float clear_color_with_alpha[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
            g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            g_pSwapChain->Present(1, 0);
            continue;
        }
        else if (eIntro < 3.15)
        {
            float t = (float)((eIntro - 3.0) / 0.15f); if (t < 0.0f) t = 0.0f; if (t > 1.0f) t = 1.0f;
            float introW = 125.0f + (float)(g_winW - 125) * t;
            float introH = 125.0f + (float)(g_winH - 125) * t;
            ImVec2 c(win_pos.x + g_winW * 0.5f, win_pos.y + g_winH * 0.5f);
            ImVec2 pmin(c.x - introW * 0.5f, c.y - introH * 0.5f);
            ImVec2 pmax(c.x + introW * 0.5f, c.y + introH * 0.5f);
            ImU32 col_intro = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f/255.0f, 15.0f/255.0f, 53.0f/255.0f, 1.0f));
            dl->AddRectFilled(pmin, pmax, col_intro, 8.0f);
            ImGui::End();
            ImGui::Render();
            const float clear_color_with_alpha2[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
            g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha2);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            g_pSwapChain->Present(1, 0);
            continue;
        }
        float fadeAlpha = 1.0f;
        if (eIntro < 3.45) { fadeAlpha = (float)((eIntro - 3.15) / 0.3f); if (fadeAlpha < 0.0f) fadeAlpha = 0.0f; if (fadeAlpha > 1.0f) fadeAlpha = 1.0f; }

        if (!g_showDetail)
            dl->AddRectFilled(win_pos, ImVec2(win_pos.x + g_winW, win_pos.y + g_winH), col_bg, 10.0f);
        dl->AddRectFilled(win_pos, ImVec2(win_pos.x + g_winW, win_pos.y + g_winH), col_bg, 8.0f);

        ImGui::PushFont(g_fontRubikMedium20);
        {
            float x = win_pos.x + 140.0f; float y = win_pos.y + 21.0f;
            dl->AddText(ImVec2(x, y), ImGui::ColorConvertFloat4ToU32(ImVec4(1,1,1,fadeAlpha)), "Versions");
        }
        ImGui::PopFont();
        
        ImGui::PushFont(g_fontRubikRegular16);
        {
            float x = win_pos.x + 140.0f; float y = win_pos.y + 53.0f;
            dl->AddText(ImVec2(x, y), ImGui::ColorConvertFloat4ToU32(ImVec4(183.0f/255.0f, 183.0f/255.0f, 183.0f/255.0f, fadeAlpha)), "Available versions");
        }
        ImGui::PopFont();
        

        

        

        

        ImU32 col_box = ImGui::ColorConvertFloat4ToU32(ImVec4(3.0f/255.0f, 13.0f/255.0f, 42.0f/255.0f, 1.0f));
        ImVec2 box_min = ImVec2(win_pos.x + 168.0f, win_pos.y + 94.0f);
        ImVec2 box_max = ImVec2(win_pos.x + 168.0f + 293.0f, win_pos.y + 94.0f + 74.0f);
        dl->AddRectFilled(box_min, box_max, col_box, 8.0f);
        dl->AddRect(box_min, box_max, ImGui::ColorConvertFloat4ToU32(ImVec4(4.0f/255.0f, 26.0f/255.0f, 80.0f/255.0f, fadeAlpha)), 8.0f, 0, 1.5f);
        if (!g_showDetail)
        {
            bool box_hovered = ImGui::IsMouseHoveringRect(box_min, box_max, true);
            bool box_clicked = box_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
            if (box_clicked)
            {
                g_detailMode = 1;
                g_showDetail = true;
                g_detailAnimActive = true;
                g_detailAnimStart = ImGui::GetTime();
            }
        }
        ImGui::PushFont(g_fontRubikMedium165);
        float cs2_x = win_pos.x + 165.0f; float cs2_y = win_pos.y + 103.0f; float cs2_w = 141.0f; float cs2_h = 18.0f;
        ImVec2 cs2_sz = ImGui::CalcTextSize("Counter-Strike 2");
        float cs2_tx = cs2_x + (cs2_w - cs2_sz.x) * 0.5f; float cs2_ty = cs2_y + (cs2_h - cs2_sz.y) * 0.5f;
        dl->AddText(ImVec2(cs2_tx + 3.0f, cs2_ty), ImGui::ColorConvertFloat4ToU32(ImVec4(1,1,1,fadeAlpha)), "Counter-Strike 2");
        ImGui::PopFont();

        

        if (!g_showDetail)
        {
            float ux = cs2_tx + 13.0f;
            float uy = cs2_ty + cs2_sz.y + 4.0f;
            ImGui::PushFont(g_fontRubikRegular15);
            std::string headTxt = "Latest Update: Loading...";
            if (g_cs2UpdatesLoaded && !g_cs2UpdatesFeed.empty()) headTxt = std::string("Latest Update: ") + g_cs2UpdatesFeed[0].date;
            ImVec2 up_sz = ImGui::CalcTextSize(headTxt.c_str());
            ImGui::PopFont();
            float lines_h = (g_cs2UpdatesLoaded && !g_cs2UpdatesFeed.empty()) ? 36.0f : 0.0f;
            ImVec2 up_min(ux - 3.0f, uy - 3.0f);
            ImVec2 up_max(ux + up_sz.x + 3.0f, uy + up_sz.y + 3.0f + lines_h);
            bool up_hovered = ImGui::IsMouseHoveringRect(up_min, up_max, true);
            bool up_clicked = up_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
            if (up_clicked)
            {
                g_detailMode = 1;
                g_showDetail = true;
                g_detailAnimActive = true;
                g_detailAnimStart = ImGui::GetTime();
            }
        }

        ImGui::PushFont(g_fontRubikRegular15);
        {
            float x = cs2_tx + 13.0f;
            float y = cs2_ty + cs2_sz.y + 4.0f;
            ImU32 col = ImGui::ColorConvertFloat4ToU32(ImVec4(183.0f/255.0f, 183.0f/255.0f, 183.0f/255.0f, fadeAlpha));
            if (g_cs2UpdatesLoaded && !g_cs2UpdatesFeed.empty())
            {
                std::string head = std::string("Latest Update: ") + g_cs2UpdatesFeed[0].date;
                dl->AddText(ImVec2(x, y), col, head.c_str());
                float area_w = 270.0f;
                const std::string& bullet = g_cs2UpdatesFeed[0].lines.empty() ? std::string("") : g_cs2UpdatesFeed[0].lines[0];
                ImVec2 bsz = ImGui::CalcTextSize(bullet.c_str());
                ImVec2 amin(x, y + 18.0f);
                ImVec2 amax(x + area_w, amin.y + bsz.y);
                bool hovered = ImGui::IsMouseHoveringRect(amin, amax, true);
                double now = ImGui::GetTime();
                float dtm = (g_cs2BulletLastTime == 0.0) ? 0.0f : (float)(now - g_cs2BulletLastTime);
                g_cs2BulletLastTime = now;
                if (hovered && bsz.x > area_w)
                {
                    g_cs2BulletScroll += 80.0f * dtm;
                    float max_off = bsz.x - area_w;
                    if (g_cs2BulletScroll > max_off) g_cs2BulletScroll = max_off;
                }
                else
                {
                    g_cs2BulletScroll *= 0.85f;
                    if (g_cs2BulletScroll < 1.0f) g_cs2BulletScroll = 0.0f;
                }
                ImDrawList* d2 = ImGui::GetWindowDrawList();
                d2->PushClipRect(amin, amax, true);
                d2->AddText(ImVec2(x - g_cs2BulletScroll, amin.y), col, bullet.c_str());
                d2->PopClipRect();
            }
            else
            {
                dl->AddText(ImVec2(x, y), col, "Latest Update: Loading...");
            }
        }
        ImGui::PopFont();

        

        
        // Bottom-right button to open Launcher Updates detail (always visible; disabled in detail)
        {
            float bw = 150.0f, bh = 32.0f;
            ImVec2 upd_min(win_pos.x + g_winW - 15.0f - bw, win_pos.y + g_winH - 15.0f - bh);
            ImVec2 upd_max(upd_min.x + bw, upd_min.y + bh);
            ImGui::SetCursorScreenPos(upd_min);
            ImGui::InvisibleButton("btn_open_updates", ImVec2(bw, bh));
            bool uhover = ImGui::IsItemHovered();
            bool uclick = !g_showDetail && ImGui::IsItemClicked();
            ImVec4 base = ImVec4(0.0f, 64.0f/255.0f, 237.0f/255.0f, 1.0f);
            ImVec4 hov = ImVec4(20.0f/255.0f, 84.0f/255.0f, 1.0f, 1.0f);
            ImU32 col_btn = ImGui::ColorConvertFloat4ToU32(uhover ? hov : base);
            dl->AddRectFilled(upd_min, upd_max, col_btn, 8.0f);
            ImGui::PushFont(g_fontRubikMedium165);
            ImVec2 usz = ImGui::CalcTextSize("Launcher Updates");
            ImVec2 upos(upd_min.x + (bw - usz.x) * 0.5f, upd_min.y + (bh - usz.y) * 0.5f);
            dl->AddText(upos, ImGui::ColorConvertFloat4ToU32(ImVec4(1,1,1,1)), "Launcher Updates");
            ImGui::PopFont();
            if (uclick) { g_detailMode = 2; g_showDetail = true; g_detailAnimActive = true; g_detailAnimStart = ImGui::GetTime(); }
        }

        ImVec2 list_min = ImVec2(win_pos.x + 168.0f, win_pos.y + 94.0f + 74.0f + 12.0f);
        float list_w = 293.0f;
        float list_h = 74.0f * 2.0f + 12.0f * 1.0f;
        ImGui::SetCursorScreenPos(list_min);
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 6.0f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0,0,0,0.15f));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, ImVec4(1,1,1,0.25f));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, ImVec4(1,1,1,0.35f));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, ImVec4(1,1,1,0.45f));
        ImGui::BeginChild("game_list_scroll", ImVec2(list_w, list_h), false, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysVerticalScrollbar);

        ImVec2 base = ImGui::GetCursorScreenPos();
        ImDrawList* d2 = ImGui::GetWindowDrawList();
        float yoff = 0.0f;
        for (int i=0;i<5;i++)
        {
            ImU32 col_boxN = ImGui::ColorConvertFloat4ToU32(ImVec4(3.0f/255.0f, 13.0f/255.0f, 42.0f/255.0f, 1.0f));
            ImVec2 bmin(base.x, base.y + yoff);
            ImVec2 bmax(bmin.x + 293.0f, bmin.y + 74.0f);
            d2->AddRectFilled(bmin, bmax, col_boxN, 8.0f);
            d2->AddRect(bmin, bmax, ImGui::ColorConvertFloat4ToU32(ImVec4(4.0f/255.0f, 26.0f/255.0f, 80.0f/255.0f, fadeAlpha)), 8.0f, 0, 1.5f);
            ImGui::PushFont(g_fontRubikMedium165);
            float gx = win_pos.x + 165.0f; float gy = bmin.y + 9.0f; float gw = 141.0f; float gh = 18.0f;
            ImVec2 gsz = ImGui::CalcTextSize("Coming soon");
            float gtx = gx + (gw - gsz.x) * 0.5f; float gty = gy + (gh - gsz.y) * 0.5f;
            {
                ImU32 blurColTitleN = ImGui::ColorConvertFloat4ToU32(ImVec4(1,1,1,0.05f * fadeAlpha));
                for (int bx=-2; bx<=2; ++bx)
                    for (int by=-2; by<=2; ++by)
                        d2->AddText(ImVec2(cs2_tx + 3.0f + bx, gty + by), blurColTitleN, "Coming soon");
            }
            ImGui::PopFont();
            ImGui::PushFont(g_fontRubikRegular15);
            {
                float xN = cs2_tx + 13.0f;
                float yN = gty + gsz.y + 4.0f;
                ImU32 blurColN1 = ImGui::ColorConvertFloat4ToU32(ImVec4(183.0f/255.0f, 183.0f/255.0f, 183.0f/255.0f, 0.08f * fadeAlpha));
                for (int bx=-2; bx<=2; ++bx)
                    for (int by=-2; by<=2; ++by)
                        d2->AddText(ImVec2(xN + bx, yN + by), blurColN1, "Latest Update: Coming soon");
                ImU32 blurColN2 = ImGui::ColorConvertFloat4ToU32(ImVec4(183.0f/255.0f, 183.0f/255.0f, 183.0f/255.0f, 0.08f * fadeAlpha));
                for (int bx=-2; bx<=2; ++bx)
                    for (int by=-2; by<=2; ++by)
                        d2->AddText(ImVec2(xN + bx, yN + 18.0f + by), blurColN2, "-\xC2" "\xA0" "Coming soon");
            }
            ImGui::PopFont();
            yoff += 74.0f + 12.0f;
        }

        ImGui::EndChild();
        ImGui::PopStyleColor(5);
        ImGui::PopStyleVar();

        if (!g_cs2IconSRV)
        {
            wchar_t tempPath[MAX_PATH]; GetTempPathW(MAX_PATH, tempPath);
            wchar_t iconPath[MAX_PATH]; wsprintfW(iconPath, L"%s%cs2_icon.png", tempPath, L'\\');
            URLDownloadToFileW(NULL, L"https://cdn2.steamgriddb.com/icon/e1bd06c3f8089e7552aa0552cb387c92/32/512x512.png", iconPath, 0, NULL);
            ID3D11ShaderResourceView* srv = nullptr; int iw=0, ih=0;
            LoadTextureFromFileWIC(g_pd3dDevice, iconPath, &srv, &iw, &ih);
            if (srv) { g_cs2IconSRV = srv; g_cs2IconW = iw; g_cs2IconH = ih; }
        }
        if (g_cs2IconSRV)
        {
            ImVec2 pmin(win_pos.x + 430.0f, win_pos.y + 101.0f);
            ImVec2 pmax(win_pos.x + 430.0f + 24.0f, win_pos.y + 101.0f + 24.0f);
            ImU32 tint = ImGui::ColorConvertFloat4ToU32(ImVec4(1,1,1,fadeAlpha));
            dl->AddImageRounded(ImTextureRef((void*)g_cs2IconSRV), pmin, pmax, ImVec2(0,0), ImVec2(1,1), tint, 4.0f);
            if (!g_showDetail)
            {
                ImGui::SetCursorScreenPos(pmin);
                ImGui::InvisibleButton("hit_logo", ImVec2(24,24));
                if (ImGui::IsItemClicked()) { g_detailMode = 1; g_showDetail = true; g_detailAnimActive = true; g_detailAnimStart = ImGui::GetTime(); }
            }
        }

        if (!g_playIconSRV)
        {
            wchar_t tempPath[MAX_PATH]; GetTempPathW(MAX_PATH, tempPath);
            wchar_t iconPath[MAX_PATH]; wsprintfW(iconPath, L"%s%cplay_icon.png", tempPath, L'\\');
            URLDownloadToFileW(NULL, L"https://img.icons8.com/FFFFFF/sf-regular-filled/2x/play.png", iconPath, 0, NULL);
            ID3D11ShaderResourceView* srv = nullptr; int iw=0, ih=0;
            LoadTextureFromFileWIC(g_pd3dDevice, iconPath, &srv, &iw, &ih);
            if (srv) { g_playIconSRV = srv; g_playIconW = iw; g_playIconH = ih; }
        }

        if (!g_showDetail)
        {
            ImVec2 pmin(win_pos.x + 431.0f, win_pos.y + 22.0f);
            ImGui::SetCursorScreenPos(pmin);
            ImGui::InvisibleButton("btn_min", ImVec2(24,24));
            bool hovered = ImGui::IsItemHovered();
            bool clicked = ImGui::IsItemClicked();
            if (hovered)
                dl->AddRectFilled(pmin, ImVec2(pmin.x + 24.0f, pmin.y + 24.0f), ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f/255.0f, 15.0f/255.0f, 53.0f/255.0f, 0.35f * fadeAlpha)), 4.0f);
            ImGui::PushFont(g_fontRubikRegular16);
            ImVec2 tsz = ImGui::CalcTextSize("-");
            ImVec2 tpos(pmin.x + (24 - tsz.x)*0.5f, pmin.y + (24 - tsz.y)*0.5f);
            dl->AddText(tpos, ImGui::ColorConvertFloat4ToU32(ImVec4(1,1,1,fadeAlpha)), "-");
            ImGui::PopFont();
            if (clicked) ShowWindow(hwnd, SW_MINIMIZE);
        }
        if (!g_showDetail)
        {
            ImVec2 pmin(win_pos.x + 461.0f, win_pos.y + 22.0f);
            ImGui::SetCursorScreenPos(pmin);
            ImGui::InvisibleButton("btn_close", ImVec2(24,24));
            bool hovered = ImGui::IsItemHovered();
            bool clicked = ImGui::IsItemClicked();
            if (hovered)
                dl->AddRectFilled(pmin, ImVec2(pmin.x + 24.0f, pmin.y + 24.0f), ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f/255.0f, 15.0f/255.0f, 53.0f/255.0f, 0.35f * fadeAlpha)), 4.0f);
            ImGui::PushFont(g_fontRubikRegular16);
            ImVec2 tsz = ImGui::CalcTextSize("X");
            ImVec2 tpos(pmin.x + (24 - tsz.x)*0.5f, pmin.y + (24 - tsz.y)*0.5f);
            dl->AddText(tpos, ImGui::ColorConvertFloat4ToU32(ImVec4(1,1,1,fadeAlpha)), "X");
            ImGui::PopFont();
            if (clicked) PostQuitMessage(0);
        }

        if (!g_personaLoaded)
        {
            HKEY hKey;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
            {
                wchar_t buf[512]; DWORD type=0,size=sizeof(buf);
                if (RegGetValueW(hKey, nullptr, L"InstallPath", RRF_RT_REG_SZ, &type, buf, &size) == ERROR_SUCCESS)
                {
                    std::wstring install = buf;
                    std::wstring vdf = install + L"\\config\\loginusers.vdf";
                    FILE* f=nullptr; _wfopen_s(&f, vdf.c_str(), L"rb");
                    if (f)
                    {
                        fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
                        std::string data; data.resize(len);
                        fread(&data[0], 1, len, f); fclose(f);
                        size_t pos = 0;
                        while (true)
                        {
                            size_t idStart = data.find('"', pos); if (idStart == std::string::npos) break;
                            size_t idEnd = data.find('"', idStart+1); if (idEnd == std::string::npos) break;
                            std::string id = data.substr(idStart+1, idEnd-idStart-1);
                            size_t blockStart = data.find('{', idEnd); if (blockStart == std::string::npos) { pos = idEnd+1; continue; }
                            size_t blockEnd = data.find('}', blockStart); if (blockEnd == std::string::npos) { pos = idEnd+1; continue; }
                            std::string block = data.substr(blockStart, blockEnd-blockStart);
                            size_t mr = block.find("\"MostRecent\"\t\t\"1\"");
                            if (mr != std::string::npos)
                            {
                                size_t pnKey = block.find("\"PersonaName\"");
                                if (pnKey != std::string::npos)
                                {
                                    size_t valStart = block.find('"', pnKey + 13);
                                    if (valStart != std::string::npos)
                                    {
                                        size_t valEnd = block.find('"', valStart+1);
                                        if (valEnd != std::string::npos)
                                            g_personaName = block.substr(valStart+1, valEnd-valStart-1);
                                    }
                                }
                                g_steamId = id;
                                std::wstring avatar = install + L"\\config\\avatarcache\\" + std::wstring(g_steamId.begin(), g_steamId.end()) + L".png";
                                ID3D11ShaderResourceView* srv = nullptr; int aw=0, ah=0;
                                LoadTextureFromFileWIC(g_pd3dDevice, avatar.c_str(), &srv, &aw, &ah);
                                if (srv) { g_leftIconSRV = srv; g_leftIconW = aw; g_leftIconH = ah; }
                                g_personaLoaded = true;
                                break;
                            }
                            pos = blockEnd+1;
                        }
                    }
                }
                RegCloseKey(hKey);
            }
        }
        {
            float pad = 13.0f;
            ImVec2 icon_min(win_pos.x + pad + 10.0f, win_pos.y + g_winH - pad - 35.0f - 10.0f);
            ImVec2 icon_max(icon_min.x + 35.0f, icon_min.y + 35.0f);
            if (g_leftIconSRV)
            {
                ImU32 tint = ImGui::ColorConvertFloat4ToU32(ImVec4(1, 1, 1, fadeAlpha));
                dl->AddImageRounded(ImTextureRef((void*)g_leftIconSRV), icon_min, icon_max, ImVec2(0,0), ImVec2(1,1), tint, 8.0f);
            }
            else
            {
                ImU32 col_img_bg = ImGui::ColorConvertFloat4ToU32(ImVec4(25.0f/255.0f, 41.0f/255.0f, 93.0f/255.0f, 0.25f * fadeAlpha));
                dl->AddRectFilled(icon_min, icon_max, col_img_bg, 8.0f);
            }
            const char* disp_name = g_personaName.empty() ? "Counter-Strike 2" : g_personaName.c_str();
            ImGui::PushFont(g_fontRubikMedium15);
            dl->AddText(ImVec2(icon_max.x + 13.0f, icon_min.y + 2.0f), ImGui::ColorConvertFloat4ToU32(ImVec4(1,1,1,fadeAlpha)), disp_name);
            ImGui::PopFont();
            const char* adminIds[] = {"76561199009736359","76561198871932430","76561198779290609"};
            const char* friendIds[] = {"76561198312851420","76561199237075452"};
            const char* role = "User";
            bool isAdmin = false;
            for (int i=0;i<3;i++) { if (g_steamId == adminIds[i]) { role = "Administrator"; isAdmin = true; break; } }
            if (!isAdmin) { for (int i=0;i<2;i++) { if (g_steamId == friendIds[i]) { role = "Friend"; break; } } }
            ImGui::PushFont(g_fontRubikRegular14);
            dl->AddText(ImVec2(icon_max.x + 13.0f, icon_min.y + 20.0f), ImGui::ColorConvertFloat4ToU32(ImVec4(77.0f/255.0f, 133.0f/255.0f, 239.0f/255.0f, fadeAlpha)), role);
            ImGui::PopFont();
        }

        if (!g_showDetail)
        {
            ImVec2 box_min(win_pos.x + 168.0f, win_pos.y + 94.0f);
            ImVec2 box_size(293.0f, 74.0f);
            ImGui::SetCursorScreenPos(box_min);
            ImGui::InvisibleButton("hit_box", box_size);
            if (ImGui::IsItemClicked()) { g_detailMode = 1; g_showDetail = true; g_detailAnimActive = true; g_detailAnimStart = ImGui::GetTime(); }
        }
        if (!g_showDetail)
        {
            ImVec2 t_min(ImVec2(cs2_tx, cs2_ty));
            ImVec2 t_size(ImVec2(cs2_sz.x + 6.0f, cs2_sz.y + 6.0f));
            ImGui::SetCursorScreenPos(t_min);
            ImGui::InvisibleButton("hit_title", t_size);
            if (ImGui::IsItemClicked()) { g_detailMode = 1; g_showDetail = true; g_detailAnimActive = true; g_detailAnimStart = ImGui::GetTime(); }
        }
        if (!g_showDetail)
        {
            float ux = cs2_tx + 13.0f; float uy = cs2_ty + cs2_sz.y + 4.0f;
            ImVec2 ts = ImGui::CalcTextSize("Latest Update: 17.11.2025");
            ImVec2 u_size(ts.x + 6.0f, ts.y + 6.0f);
            ImGui::SetCursorScreenPos(ImVec2(ux, uy));
            ImGui::InvisibleButton("hit_update", u_size);
            if (ImGui::IsItemClicked()) { g_showDetail = true; g_detailAnimActive = true; g_detailAnimStart = ImGui::GetTime(); }
        }

        if (g_showDetail)
        {
            if (g_detailClosingActive)
            {
                double dtc = ImGui::GetTime() - g_detailCloseStart;
                if (dtc < 0.15)
                {
                    float t = (float)(dtc / 0.15f); if (t < 0.0f) t = 0.0f; if (t > 1.0f) t = 1.0f;
                    float a_bg = 0.35f * (1.0f - t);
                    ImU32 col_bg_fade = ImGui::ColorConvertFloat4ToU32(ImVec4(0,0,0,a_bg));
                    dl->AddRectFilled(win_pos, ImVec2(win_pos.x + g_winW, win_pos.y + g_winH), col_bg_fade, 0.0f);
                    ImVec2 c(win_pos.x + g_winW * 0.5f, win_pos.y + g_winH * 0.5f);
                    float w = 470.0f - (470.0f - 125.0f) * t;
                    float h = 370.0f - (370.0f - 125.0f) * t;
                    ImVec2 pmin(c.x - w * 0.5f, c.y - h * 0.5f);
                    ImVec2 pmax(c.x + w * 0.5f, c.y + h * 0.5f);
                    ImU32 col_panel_fade = ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 9.0f/255.0f, 33.0f/255.0f, 1.0f - t));
                    dl->AddRectFilled(pmin, pmax, col_panel_fade, 8.0f);
                    ImGui::End();
                    ImGui::Render();
                    const float clear_color_with_alpha3[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                    g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
                    g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha3);
                    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
                    g_pSwapChain->Present(1, 0);
                    continue;
                }
                else
                {
                    g_detailClosingActive = false;
                    g_showDetail = false;
                    ImGui::End();
                    ImGui::Render();
                    const float clear_color_with_alpha4[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                    g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
                    g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha4);
                    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
                    g_pSwapChain->Present(1, 0);
                    continue;
                }
            }
            dl->AddRectFilled(win_pos, ImVec2(win_pos.x + g_winW, win_pos.y + g_winH), ImGui::ColorConvertFloat4ToU32(ImVec4(0,0,0,0.35f)), 0.0f);
            ImU32 col_panel = ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 9.0f/255.0f, 33.0f/255.0f, 1.0f));
            ImVec2 panel_target_min(win_pos.x + 15.0f, win_pos.y + 15.0f);
            ImVec2 panel_target_max(win_pos.x + 15.0f + 470.0f, win_pos.y + 15.0f + 370.0f);
            if (g_detailAnimActive)
            {
                double dt = ImGui::GetTime() - g_detailAnimStart;
                if (dt < 0.15)
                {
                    float t = (float)(dt / 0.15);
                    if (t < 0.0f) t = 0.0f; if (t > 1.0f) t = 1.0f;
                    ImVec2 c(win_pos.x + g_winW * 0.5f, win_pos.y + g_winH * 0.5f);
                    float w = 125.0f + (470.0f - 125.0f) * t;
                    float h = 125.0f + (370.0f - 125.0f) * t;
                    ImVec2 pmin(c.x - w * 0.5f, c.y - h * 0.5f);
                    ImVec2 pmax(c.x + w * 0.5f, c.y + h * 0.5f);
                    dl->AddRectFilled(pmin, pmax, col_panel, 8.0f);
                    ImGui::End();
                    ImGui::Render();
                    const float clear_color_with_alpha3[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                    g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
                    g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha3);
                    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
                    g_pSwapChain->Present(1, 0);
                    continue;
                }
                else
                {
                    g_detailAnimActive = false;
                }
            }
            dl->AddRectFilled(panel_target_min, panel_target_max, col_panel, 8.0f);
            
            dl->AddLine(ImVec2(win_pos.x + 15.0f, win_pos.y + 315.0f), ImVec2(win_pos.x + 15.0f + 470.0f, win_pos.y + 315.0f), ImGui::ColorConvertFloat4ToU32(ImVec4(4.0f/255.0f, 26.0f/255.0f, 80.0f/255.0f, 1.0f)), 1.5f);
            dl->AddLine(ImVec2(win_pos.x + 15.0f, win_pos.y + 86.0f), ImVec2(win_pos.x + 15.0f + 470.0f, win_pos.y + 86.0f), ImGui::ColorConvertFloat4ToU32(ImVec4(4.0f/255.0f, 26.0f/255.0f, 80.0f/255.0f, 1.0f)), 1.5f);

            float header_top = win_pos.y + 15.0f;
            float header_bottom = win_pos.y + 86.0f;
            float header_mid = header_top + (header_bottom - header_top) * 0.5f;
            float btn_size = 40.0f;
            ImVec2 close_min(win_pos.x + 451.0f, header_mid - btn_size * 0.5f);
            ImGui::PushFont(g_fontRubikMedium18);
            ImVec2 tsz = ImGui::CalcTextSize("X");
            ImVec2 tpos(close_min.x + (btn_size - tsz.x)*0.5f, close_min.y + (btn_size - tsz.y)*0.5f);
            ImVec2 glyph_min = tpos;
            ImVec2 glyph_max = ImVec2(tpos.x + tsz.x, tpos.y + tsz.y);
            bool cclick = ImGui::IsMouseHoveringRect(glyph_min, glyph_max, true) && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
            dl->AddText(tpos, ImGui::ColorConvertFloat4ToU32(ImVec4(1,1,1,1)), "X");
            ImGui::PopFont();
            if (cclick) { g_detailClosingActive = true; g_detailAnimActive = false; g_detailCloseStart = ImGui::GetTime(); }

            if (g_detailMode == 1)
            {
                ImVec2 logo_min(win_pos.x + 33.0f, win_pos.y + 32.0f);
                ImVec2 logo_max(logo_min.x + 40.0f, logo_min.y + 40.0f);
                if (g_cs2IconSRV)
                    dl->AddImageRounded(ImTextureRef((void*)g_cs2IconSRV), logo_min, logo_max, ImVec2(0,0), ImVec2(1,1), ImGui::ColorConvertFloat4ToU32(ImVec4(1,1,1,1)), 8.0f);
                ImGui::PushFont(g_fontRubikMedium20);
                float x = win_pos.x + 84.0f; float y = win_pos.y + 42.0f; float w = 334.0f; float h = 19.0f;
                ImVec2 sz = ImGui::CalcTextSize("Counter-Strike 2");
                float ty = y + (h - sz.y) * 0.5f;
                dl->AddText(ImVec2(x, ty), ImGui::ColorConvertFloat4ToU32(ImVec4(1,1,1,1)), "Counter-Strike 2");
                ImGui::PopFont();

                float content_x = panel_target_min.x + 33.0f;
                float content_y = panel_target_min.y + 100.0f;
                float content_w = panel_target_max.x - panel_target_min.x - 66.0f;
                float bottom_line_y = win_pos.y + 315.0f;
                float content_h = bottom_line_y - content_y;
                ImU32 lineCol = ImGui::ColorConvertFloat4ToU32(ImVec4(4.0f/255.0f, 26.0f/255.0f, 80.0f/255.0f, 1.0f));
                ImU32 textCol = ImGui::ColorConvertFloat4ToU32(ImVec4(1,1,1,1));

                ImGui::SetCursorScreenPos(ImVec2(content_x, content_y));
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 9.0f/255.0f, 33.0f/255.0f, 1.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 0.0f);
                ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0,0,0,0));
                ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, ImVec4(1.0f,1.0f,1.0f,0.0f));
                ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, ImVec4(1.0f,1.0f,1.0f,0.0f));
                ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, ImVec4(1.0f,1.0f,1.0f,0.0f));
                ImGui::BeginChild("cs2_updates_detail", ImVec2(content_w, content_h), false, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysVerticalScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

                {
                    ImGuiIO& io = ImGui::GetIO();
                    double now = ImGui::GetTime();
                    float dt = (g_updatesLastTime == 0.0) ? 0.0f : (float)(now - g_updatesLastTime);
                    g_updatesLastTime = now;
                    float currY = ImGui::GetScrollY();
                    float maxY = ImGui::GetScrollMaxY();

                    bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_None);
                    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    {
                        g_updatesDragActive = true;
                        g_updatesDragStartY = ImGui::GetMousePos().y;
                        g_updatesDragScrollStart = currY;
                        g_updatesScrollVel = 0.0f;
                    }
                    if (g_updatesDragActive && ImGui::IsMouseDown(ImGuiMouseButton_Left))
                    {
                        float dy = ImGui::GetMousePos().y - g_updatesDragStartY;
                        float targetY = g_updatesDragScrollStart - dy;
                        if (targetY < 0.0f) targetY = 0.0f;
                        if (targetY > maxY) targetY = maxY;
                        g_updatesScrollVel = (currY - targetY);
                        ImGui::SetScrollY(targetY);
                    }
                    if (g_updatesDragActive && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                    {
                        g_updatesDragActive = false;
                    }

                    if (!g_updatesDragActive)
                    {
                        g_updatesScrollVel += -io.MouseWheel * 1200.0f;
                        float targetY = currY + g_updatesScrollVel * dt;
                        if (targetY < 0.0f) { targetY = 0.0f; g_updatesScrollVel *= 0.6f; }
                        if (targetY > maxY) { targetY = maxY; g_updatesScrollVel *= 0.6f; }
                        float newY = currY + (targetY - currY) * 0.45f;
                        ImGui::SetScrollY(newY);
                        float damp = 0.94f;
                        g_updatesScrollVel *= damp;
                        if (fabsf(g_updatesScrollVel) < 0.03f) g_updatesScrollVel = 0.0f;
                    }
                }

                if (!g_cs2UpdatesLoaded) { ImGui::Text("Loading CS2 updates..."); }
                else
                {
                    for (size_t i=0;i<g_cs2UpdatesFeed.size();i++)
                    {
                        const UpdateEntry& ue = g_cs2UpdatesFeed[i];
                        ImGui::PushFont(g_fontRubikRegular16);
                        ImGui::Text("%s", ue.date.c_str());
                        ImGui::PopFont();
                        ImGui::Dummy(ImVec2(0,10));
                        ImVec2 cur = ImGui::GetCursorScreenPos();
                        ImDrawList* d2 = ImGui::GetWindowDrawList();
                        d2->AddLine(cur, ImVec2(cur.x + content_w, cur.y), lineCol, 1.5f);
                        ImGui::Dummy(ImVec2(0,6));
                        ImGui::PushFont(g_fontRubikRegular15);
                        ImGui::PushTextWrapPos(content_x + content_w);
                        for (size_t k=0;k<ue.lines.size();k++) ImGui::Text("%s", ue.lines[k].c_str());
                        ImGui::PopTextWrapPos();
                        ImGui::PopFont();
                        ImGui::Dummy(ImVec2(0,30));
                    }
                }

                ImGui::EndChild();
                ImGui::PopStyleColor(5);
                ImGui::PopStyleVar();

                ImVec2 load_min(win_pos.x + 358.0f, win_pos.y + 328.0f);
                ImVec2 load_max(load_min.x + 105.0f, load_min.y + 36.0f);
                ImGui::SetCursorScreenPos(load_min);
                ImGui::InvisibleButton("detail_load_btn", ImVec2(105,36));
                bool lhover = ImGui::IsItemHovered();
                bool lclick = ImGui::IsItemClicked();
                ImVec4 base = ImVec4(0.0f, 64.0f/255.0f, 237.0f/255.0f, 1.0f);
                ImVec4 hov = ImVec4(20.0f/255.0f, 84.0f/255.0f, 1.0f, 1.0f);
                ImU32 col_btn = ImGui::ColorConvertFloat4ToU32(lhover ? hov : base);
                dl->AddRectFilled(load_min, load_max, col_btn, 8.0f);
                if (g_playIconSRV)
                {
                    ImVec2 pmin2(win_pos.x + 378.0f, win_pos.y + 336.0f);
                    ImVec2 pmax2(pmin2.x + 20.0f, pmin2.y + 20.0f);
                    dl->AddImageRounded(ImTextureRef((void*)g_playIconSRV), pmin2, pmax2, ImVec2(0,0), ImVec2(1,1), ImGui::ColorConvertFloat4ToU32(ImVec4(1,1,1,1)), 4.0f);
                }
                else
                {
                    ImU32 col_tri = ImGui::ColorConvertFloat4ToU32(ImVec4(1,1,1,1));
                    ImVec2 c = ImVec2(load_min.x + 378.0f - 358.0f + load_min.x, load_min.y + 336.0f - 328.0f + load_min.y);
                    ImVec2 a = ImVec2(load_min.x + 382.0f, load_min.y + 338.0f);
                    ImVec2 b = ImVec2(load_min.x + 382.0f, load_min.y + 354.0f);
                    ImVec2 d = ImVec2(load_min.x + 394.0f, load_min.y + 346.0f);
                    dl->AddTriangleFilled(a, b, d, col_tri);
                }
                ImGui::PushFont(g_fontRubikMedium165);
                float lx = win_pos.x + 385.0f; float ly = win_pos.y + 328.0f; float lw = 78.0f; float lh = 36.0f;
                ImVec2 lsz = ImGui::CalcTextSize("Load");
                float ltx = lx + (lw - lsz.x) * 0.5f; float lty = ly + (lh - lsz.y) * 0.5f;
                dl->AddText(ImVec2(ltx, lty), ImGui::ColorConvertFloat4ToU32(ImVec4(1,1,1,1)), "Load");
                ImGui::PopFont();
            }
            else if (g_detailMode == 2)
            {
                ImGui::PushFont(g_fontRubikMedium20);
                float hx = win_pos.x + 84.0f; float hy = win_pos.y + 42.0f; float hw = 334.0f; float hh = 19.0f;
                ImVec2 hsz = ImGui::CalcTextSize("Launcher Updates");
                float hty = hy + (hh - hsz.y) * 0.5f;
                dl->AddText(ImVec2(hx, hty), ImGui::ColorConvertFloat4ToU32(ImVec4(1,1,1,1)), "Launcher Updates");
                ImGui::PopFont();

                float content_x = panel_target_min.x + 33.0f;
                float content_y = panel_target_min.y + 100.0f;
                float content_w = panel_target_max.x - panel_target_min.x - 66.0f;
                float bottom_line_y = win_pos.y + 315.0f;
                float content_h = bottom_line_y - content_y;
                ImU32 lineCol = ImGui::ColorConvertFloat4ToU32(ImVec4(4.0f/255.0f, 26.0f/255.0f, 80.0f/255.0f, 1.0f));
                ImU32 textCol = ImGui::ColorConvertFloat4ToU32(ImVec4(1,1,1,1));

                ImGui::SetCursorScreenPos(ImVec2(content_x, content_y));
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 9.0f/255.0f, 33.0f/255.0f, 1.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 0.0f);
                ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0,0,0,0));
                ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, ImVec4(1.0f,1.0f,1.0f,0.0f));
                ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, ImVec4(1.0f,1.0f,1.0f,0.0f));
                ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, ImVec4(1.0f,1.0f,1.0f,0.0f));
                ImGui::BeginChild("updates_detail", ImVec2(content_w, content_h), false, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysVerticalScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

                {
                    ImGuiIO& io = ImGui::GetIO();
                    double now = ImGui::GetTime();
                    float dt = (g_updatesLastTime == 0.0) ? 0.0f : (float)(now - g_updatesLastTime);
                    g_updatesLastTime = now;
                    float currY = ImGui::GetScrollY();
                    float maxY = ImGui::GetScrollMaxY();

                    bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_None);
                    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    {
                        g_updatesDragActive = true;
                        g_updatesDragStartY = ImGui::GetMousePos().y;
                        g_updatesDragScrollStart = currY;
                        g_updatesScrollVel = 0.0f;
                    }
                    if (g_updatesDragActive && ImGui::IsMouseDown(ImGuiMouseButton_Left))
                    {
                        float dy = ImGui::GetMousePos().y - g_updatesDragStartY;
                        float targetY = g_updatesDragScrollStart - dy;
                        if (targetY < 0.0f) targetY = 0.0f;
                        if (targetY > maxY) targetY = maxY;
                        g_updatesScrollVel = (currY - targetY);
                        ImGui::SetScrollY(targetY);
                    }
                    if (g_updatesDragActive && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                    {
                        g_updatesDragActive = false;
                    }

                    if (!g_updatesDragActive)
                    {
                        g_updatesScrollVel += -io.MouseWheel * 1200.0f;
                        float targetY = currY + g_updatesScrollVel * dt;
                        if (targetY < 0.0f) { targetY = 0.0f; g_updatesScrollVel *= 0.6f; }
                        if (targetY > maxY) { targetY = maxY; g_updatesScrollVel *= 0.6f; }
                        float newY = currY + (targetY - currY) * 0.45f;
                        ImGui::SetScrollY(newY);
                        float damp = 0.94f;
                        g_updatesScrollVel *= damp;
                        if (fabsf(g_updatesScrollVel) < 0.03f) g_updatesScrollVel = 0.0f;
                    }
                }

                if (!g_updatesLoaded) { }
                else
                {
                    for (size_t i=0;i<g_updatesFeed.size();i++)
                    {
                        const UpdateEntry& ue = g_updatesFeed[i];
                        ImGui::PushFont(g_fontRubikRegular16);
                        ImGui::Text("%s", ue.date.c_str());
                        ImGui::PopFont();
                        ImGui::Dummy(ImVec2(0,10));
                        ImVec2 cur = ImGui::GetCursorScreenPos();
                        ImDrawList* d2 = ImGui::GetWindowDrawList();
                        d2->AddLine(cur, ImVec2(cur.x + content_w, cur.y), lineCol, 1.5f);
                        ImGui::Dummy(ImVec2(0,6));
                        ImGui::PushFont(g_fontRubikRegular15);
                        ImGui::PushTextWrapPos(content_x + content_w);
                        for (size_t k=0;k<ue.lines.size();k++) ImGui::Text("%s", ue.lines[k].c_str());
                        ImGui::PopTextWrapPos();
                        ImGui::PopFont();
                        ImGui::Dummy(ImVec2(0,30));
                    }
                }

                ImGui::EndChild();
                ImGui::PopStyleColor(5);
                ImGui::PopStyleVar();
            }
        }
        ImGui::End();

        ImGui::Render();
        const float clear_color_with_alpha[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    UnregisterClass(wc.lpszClassName, wc.hInstance);
    CoUninitialize();
    return 0;
}