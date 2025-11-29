#include <windows.h>
#include <d3d9.h>
#include <dwmapi.h>
#include <vector>
#include <string>
#include <cmath>
#include "gui.hpp"
#include "../../External/ImGUI/imgui.h"
#include "../../External/ImGUI/imgui_impl_win32.h"
#include "../../External/ImGUI/imgui_impl_dx9.h"
#ifndef PROGMEM
#define PROGMEM
#endif
#include "../../External/fonts/Rubik-Regular.c"
#include "../../External/fonts/Rubik-Medium.c"
#include "../../External/fonts/Rubik-SemiBold.c"
static LPDIRECT3D9 g_pD3D = nullptr;
static LPDIRECT3DDEVICE9 g_pd3dDevice = nullptr;
static D3DPRESENT_PARAMETERS g_d3dpp = {};
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
static ImFont* g_fontRubikMedium20 = nullptr;
static ImFont* g_fontRubikRegular16 = nullptr;
static ImFont* g_fontRubikRegular15 = nullptr;
static ImFont* g_fontRubikSemiBold24 = nullptr;
static ImFont* g_fontRubikSemiBold18 = nullptr;
void get_logs(std::vector<std::string>&);
static HWND g_hWnd = nullptr;
static int g_phase = -1;
static std::wstring g_phase_resp;
static std::wstring g_cur_server;
static int g_server_idx = 0;
static int g_server_total = 0;
static std::vector<int> g_server_durs;
static bool g_completed = false;
static float g_base_phase_secs[7] = {2.0f,2.0f,2.0f,2.0f,0.0f,2.0f,2.0f};
static float g_base_server_sec = 20.0f;
static ULONGLONG g_last_switch = 0;
static bool g_started = false;
static bool g_exit = false;
static bool s_dragging = false;
static POINT s_dragStartMouse{0,0};
static POINT s_dragStartWin{0,0};
void gui_set_phase(int idx, const wchar_t* resp){
    g_phase = idx;
    g_phase_resp = resp ? resp : L"";
    g_last_switch = GetTickCount64();
}
void gui_mark_start(){ g_started = true; }
bool gui_is_started(){ return g_started; }
bool gui_should_exit(){ return g_exit; }
void gui_request_exit(){ g_exit = true; }
void gui_set_current_server(const wchar_t* name){ g_cur_server = name ? name : L""; }
void gui_set_server_index(int idx, int total){ g_server_idx = idx; g_server_total = total; }
void gui_add_server_duration_ms(int ms){ if (ms>0) g_server_durs.push_back(ms); }
void gui_mark_completed(){ g_completed = true; }
static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam){
    if(ImGui_ImplWin32_WndProcHandler(hWnd,msg,wParam,lParam)) return 1;
    if(msg==WM_CLOSE){ DestroyWindow(hWnd); return 0; }
    if(msg==WM_DESTROY){ PostQuitMessage(0); return 0; }
    if(msg==WM_NCRBUTTONUP || msg==WM_CONTEXTMENU) return 0;
    if(msg==WM_SYSCOMMAND){ if(wParam==SC_CONTEXTHELP) return 0; }
    if(msg==WM_NCHITTEST) return HTCLIENT;
    return DefWindowProcW(hWnd,msg,wParam,lParam);
}
static bool CreateDeviceD3D(HWND hWnd){
    g_pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    if(!g_pD3D) return false;
    ZeroMemory(&g_d3dpp,sizeof(g_d3dpp));
    g_d3dpp.Windowed=TRUE;
    g_d3dpp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    g_d3dpp.BackBufferFormat=D3DFMT_A8R8G8B8;
    g_d3dpp.EnableAutoDepthStencil=TRUE;
    g_d3dpp.AutoDepthStencilFormat=D3DFMT_D16;
    g_d3dpp.PresentationInterval=D3DPRESENT_INTERVAL_ONE;
    if(g_pD3D->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,hWnd,D3DCREATE_HARDWARE_VERTEXPROCESSING,&g_d3dpp,&g_pd3dDevice)<0) return false;
    g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);
    g_pd3dDevice->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);
    g_pd3dDevice->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
    return true;
}
static void CleanupDeviceD3D(){
    if(g_pd3dDevice){ g_pd3dDevice->Release(); g_pd3dDevice=nullptr; }
    if(g_pD3D){ g_pD3D->Release(); g_pD3D=nullptr; }
}
static void ResetDevice(){
    ImGui_ImplDX9_InvalidateDeviceObjects();
    if(g_pd3dDevice->Reset(&g_d3dpp)==D3DERR_INVALIDCALL) return;
    ImGui_ImplDX9_CreateDeviceObjects();
}
int checker_gui_loading(int start_deg){
    WNDCLASSEXW wc{ sizeof(WNDCLASSEXW), CS_CLASSDC, WndProc, 0, 0, GetModuleHandleW(NULL), nullptr, nullptr, nullptr, nullptr, L"RusticalandCheckerLoaderWnd", nullptr };
    RegisterClassExW(&wc);
    int sw=GetSystemMetrics(SM_CXSCREEN), sh=GetSystemMetrics(SM_CYSCREEN);
    int winW=500, winH=400;
    int winX=(sw-winW)/2, winY=(sh-winH)/2;
    HWND hWnd=CreateWindowExW(WS_EX_LAYERED|WS_EX_APPWINDOW, wc.lpszClassName, L"Rusticaland Checker", WS_POPUP, winX, winY, winW, winH, nullptr, nullptr, wc.hInstance, nullptr);
    g_hWnd = hWnd;
    SetLayeredWindowAttributes(hWnd,0,255,LWA_ALPHA);
    { MARGINS m={-1}; DwmExtendFrameIntoClientArea(hWnd,&m);} 
    if(!CreateDeviceD3D(hWnd)){ CleanupDeviceD3D(); UnregisterClassW(wc.lpszClassName,wc.hInstance); return 1; }
    ShowWindow(hWnd, SW_SHOWDEFAULT);
    UpdateWindow(hWnd);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui::GetIO().IniFilename=nullptr;
    ImGuiStyle& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_Text] = ImVec4(1.0f,1.0f,1.0f,1.0f);
    ImGuiIO& io = ImGui::GetIO();
    ImGui_ImplWin32_Init(hWnd);
    ImGui_ImplDX9_Init(g_pd3dDevice);
    {
        ImFontConfig cfg; cfg.FontDataOwnedByAtlas = false;
        g_fontRubikMedium20 = io.Fonts->AddFontFromMemoryTTF((void*)Rubik_Medium, (int)sizeof(Rubik_Medium), 20.0f, &cfg);
        g_fontRubikRegular16 = io.Fonts->AddFontFromMemoryTTF((void*)Rubik_Regular, (int)sizeof(Rubik_Regular), 16.0f, &cfg);
        g_fontRubikRegular15 = io.Fonts->AddFontFromMemoryTTF((void*)Rubik_Regular, (int)sizeof(Rubik_Regular), 15.0f, &cfg);
        g_fontRubikSemiBold24 = io.Fonts->AddFontFromMemoryTTF((void*)Rubik_SemiBold, (int)sizeof(Rubik_SemiBold), 24.0f, &cfg);
        g_fontRubikSemiBold18 = io.Fonts->AddFontFromMemoryTTF((void*)Rubik_SemiBold, (int)sizeof(Rubik_SemiBold), 18.0f, &cfg);
    }
    float start_phase = (float)start_deg * 3.14159265f / 180.0f; 
    double t_start = ImGui::GetTime(); 
    MSG msg{}; 
    while(msg.message!=WM_QUIT && !g_exit){ 
        if(PeekMessageW(&msg,nullptr,0U,0U,PM_REMOVE)){ TranslateMessage(&msg); DispatchMessageW(&msg); continue; }
        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0,0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(500,400), ImGuiCond_Always);
        ImGui::Begin("##loader", nullptr, ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoBackground|ImGuiWindowFlags_NoScrollbar);
        ImDrawList* dl=ImGui::GetWindowDrawList();
        ImVec2 win_pos=ImGui::GetWindowPos();
        ImU32 col_box=ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f/255.0f,15.0f/255.0f,53.0f/255.0f,1.0f));
        ImU32 col_bg=col_box;
        ImVec2 center(win_pos.x+250.0f, win_pos.y+200.0f);
        double t_now=ImGui::GetTime();
        double eIntro=t_now - t_start; 
        if(eIntro < 3.0){ 
            ImVec2 pmin(center.x-62.5f, center.y-62.5f); 
            ImVec2 pmax(center.x+62.5f, center.y+62.5f); 
            dl->AddRectFilled(pmin,pmax,col_box,8.0f); 
            float a=(float)t_now*6.0f+start_phase; 
            ImU32 white=IM_COL32(255,255,255,255); 
            dl->PathClear(); 
            dl->PathArcTo(center,18.0f,a, a+3.1415926f*0.9f, 32); 
            dl->PathStroke(white,false,3.0f); 
        }
        else if (eIntro < 3.15){ float t = (float)((eIntro - 3.0) / 0.15f); if (t<0.0f) t=0.0f; if (t>1.0f) t=1.0f; float introW = 125.0f + (500.0f - 125.0f) * t; float introH = 125.0f + (400.0f - 125.0f) * t; ImVec2 pmin(center.x - introW*0.5f, center.y - introH*0.5f); ImVec2 pmax(center.x + introW*0.5f, center.y + introH*0.5f); dl->AddRectFilled(pmin,pmax,col_box,8.0f); }
        else { 
            dl->AddRectFilled(win_pos, ImVec2(win_pos.x + 500.0f, win_pos.y + 400.0f), col_bg, 10.0f); 
            ImVec2 panel_center(win_pos.x + 250.0f, win_pos.y + 200.0f);
            ImU32 col_text  = IM_COL32(255,255,255,255);

            // Top-right X to exit
            ImVec2 xLocal(500.0f - 28.0f, 10.0f);
            ImGui::SetCursorPos(ImVec2(0,0));
            if (ImGui::InvisibleButton("##dragbar", ImVec2(500.0f - 36.0f, 26.0f))){ /* noop */ }
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)){
                POINT pt; GetCursorPos(&pt);
                if (!s_dragging){ s_dragging = true; s_dragStartMouse = pt; RECT rc; GetWindowRect(g_hWnd, &rc); s_dragStartWin.x = rc.left; s_dragStartWin.y = rc.top; }
                int nx = s_dragStartWin.x + (pt.x - s_dragStartMouse.x);
                int ny = s_dragStartWin.y + (pt.y - s_dragStartMouse.y);
                SetWindowPos(g_hWnd, nullptr, nx, ny, 0, 0, SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
            } else { s_dragging = false; }
            ImGui::SetCursorPos(xLocal);
            if (ImGui::InvisibleButton("##close", ImVec2(18,18))) { gui_request_exit(); if (g_hWnd) DestroyWindow(g_hWnd); PostQuitMessage(0); }
            ImU32 white = IM_COL32(255,255,255,255);
            dl->AddText(ImVec2(win_pos.x + xLocal.x + 4.0f, win_pos.y + xLocal.y - 1.0f), white, "X");

            // Direct text replacement without fade

            if (!g_started){
                const char* title = "Rusticaland Checker";
                const char* subtitle = "Diagnostics";
                if (g_fontRubikSemiBold24) ImGui::PushFont(g_fontRubikSemiBold24);
                ImVec2 s1 = ImGui::CalcTextSize(title);
                dl->AddText(ImVec2(panel_center.x - s1.x*0.5f, panel_center.y - 130.0f), col_text, title);
                if (g_fontRubikSemiBold24) ImGui::PopFont();
                if (g_fontRubikSemiBold18) ImGui::PushFont(g_fontRubikSemiBold18);
                ImVec2 s2 = ImGui::CalcTextSize(subtitle);
                dl->AddText(ImVec2(panel_center.x - s2.x*0.5f, panel_center.y - 100.0f), col_text, subtitle);
                if (g_fontRubikSemiBold18) ImGui::PopFont();
                ImVec2 btnSz(160, 40);
                ImVec2 btnPosLocal(panel_center.x - btnSz.x*0.5f - win_pos.x, panel_center.y + 20.0f - win_pos.y);
                ImGui::SetCursorPos(btnPosLocal);
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(10.0f/255.0f,33.0f/255.0f,64.0f/255.0f,1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(12.0f/255.0f,44.0f/255.0f,85.0f/255.0f,1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(13.0f/255.0f,55.0f/255.0f,105.0f/255.0f,1.0f));
                if (ImGui::Button("Check", btnSz)) { gui_mark_start(); gui_set_phase(0, L"..."); }
                ImGui::PopStyleColor(3);
                ImGui::PopStyleVar();
            } else {
                const char* steps[] = {
                    "Checking Connection to Network",
                    "Checking Rusticaland Website Response",
                    "Checking VPN/Proxy",
                    "Checking VPN/Proxy Externally",
                    "Checking Server",
                    "Checking Firewall",
                    "ForceAllowing Rusticaland Servers"
                };
                int phase = g_phase;
                if (phase < 0) phase = 0;
                if (phase > 6) phase = 6;
                std::string titleS = steps[phase];
                if (phase == 4){
                    if (!g_cur_server.empty()){
                        std::string n(g_cur_server.begin(), g_cur_server.end());
                        titleS = std::string("Checking Server: ") + n;
                    } else {
                        titleS = "Checking Server: Monthly, OneGrid, Vanilla, Sandbox, Modded";
                    }
                }
                const char* title = titleS.c_str();
                if (g_completed) title = "Completed";
                if (g_fontRubikSemiBold24) ImGui::PushFont(g_fontRubikSemiBold24);
                ImVec2 szTitle = ImGui::CalcTextSize(title);
                ImVec2 posTitle(panel_center.x - szTitle.x * 0.5f, panel_center.y - 130.0f);
                dl->AddText(posTitle, col_text, title);
                if (g_fontRubikSemiBold24) ImGui::PopFont();
                if (g_fontRubikSemiBold18) ImGui::PushFont(g_fontRubikSemiBold18);
                std::wstring r = g_phase_resp;
                if (g_completed) r = L"Please send this file to a staff in the ticket."; else if (r.empty()) r = L"...";
                std::string resp(r.begin(), r.end());
                ImVec2 szResp = ImGui::CalcTextSize(resp.c_str());
                ImVec2 posResp(panel_center.x - szResp.x * 0.5f, panel_center.y - 100.0f);
                dl->AddText(posResp, col_text, resp.c_str());
                if (g_fontRubikSemiBold18) ImGui::PopFont();

                // Bottom loading circle + percent and ETA
                ImVec2 circleCenter(panel_center.x, panel_center.y + 60.0f);
                float radius = 18.0f;
                ImU32 white = IM_COL32(255,255,255,255);
                if (!g_completed){
                    float elapsed_cur = (float)((GetTickCount64() - g_last_switch) / 1000.0f);
                    int total_servers = (g_server_total>0?g_server_total:5);
                    float avg_server = g_base_server_sec;
                    if (!g_server_durs.empty()){ float sum=0.0f; for(int ms: g_server_durs) sum += (float)ms/1000.0f; avg_server = sum / (float)g_server_durs.size(); if (avg_server < 5.0f) avg_server = 5.0f; }
                    float total_est = g_base_phase_secs[0] + g_base_phase_secs[1] + g_base_phase_secs[2] + g_base_phase_secs[3] + g_base_phase_secs[5] + g_base_phase_secs[6] + avg_server * total_servers;
                    float elapsed_est = 0.0f;
                    switch (phase){
                        case 0:
                            elapsed_est = std::min(elapsed_cur, g_base_phase_secs[0]);
                            break;
                        case 1:
                            elapsed_est = g_base_phase_secs[0] + std::min(elapsed_cur, g_base_phase_secs[1]);
                            break;
                        case 2:
                            elapsed_est = g_base_phase_secs[0] + g_base_phase_secs[1] + std::min(elapsed_cur, g_base_phase_secs[2]);
                            break;
                        case 3:
                            elapsed_est = g_base_phase_secs[0] + g_base_phase_secs[1] + g_base_phase_secs[2] + std::min(elapsed_cur, g_base_phase_secs[3]);
                            break;
                        case 5:
                            elapsed_est = g_base_phase_secs[0] + g_base_phase_secs[1] + g_base_phase_secs[2] + g_base_phase_secs[3] + std::min(elapsed_cur, g_base_phase_secs[5]);
                            break;
                        case 6:
                            elapsed_est = g_base_phase_secs[0] + g_base_phase_secs[1] + g_base_phase_secs[2] + g_base_phase_secs[3] + g_base_phase_secs[5] + std::min(elapsed_cur, g_base_phase_secs[6]);
                            break;
                        case 4:
                            elapsed_est = g_base_phase_secs[0] + g_base_phase_secs[1] + g_base_phase_secs[2] + g_base_phase_secs[3] + avg_server * (float)g_server_idx + std::min(elapsed_cur, avg_server);
                            break;
                        default:
                            break;
                    }
                    float pctf = total_est>0.0f ? (elapsed_est / total_est) : 0.0f; if (pctf<0.0f) pctf=0.0f; if (pctf>1.0f) pctf=1.0f;
                    int pct = (int)(pctf * 100.0f);
                    int eta = (int)std::max(0.0f, total_est - elapsed_est);
                    float a0 = (float)ImGui::GetTime() * 2.5f;
                    dl->PathClear();
                    dl->PathArcTo(circleCenter, radius, a0, a0 + 3.1415926f*0.9f, 48);
                    dl->PathStroke(white, false, 3.0f);
                    char buf[64]; snprintf(buf, sizeof(buf), "%d%% | %d Seconds Remaining", pct, eta);
                    ImVec2 szP = ImGui::CalcTextSize(buf);
                    dl->AddText(ImVec2(circleCenter.x - szP.x * 0.5f, circleCenter.y + radius + 12.0f), white, buf);
                } else {
                    char buf[64]; snprintf(buf, sizeof(buf), "100%% | 0 Seconds Remaining");
                    ImVec2 szP = ImGui::CalcTextSize(buf);
                    dl->AddText(ImVec2(circleCenter.x - szP.x * 0.5f, circleCenter.y + 12.0f), white, buf);
                }
            }
        }
        ImGui::End(); 
        ImGui::EndFrame(); 
        g_pd3dDevice->SetRenderState(D3DRS_ZENABLE,FALSE); 
        g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE); 
        g_pd3dDevice->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE); 
        g_pd3dDevice->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,D3DCOLOR_RGBA(0,0,0,0),1.0f,0); 
        if(g_pd3dDevice->BeginScene()==D3D_OK){ ImGui::Render(); ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData()); g_pd3dDevice->EndScene(); } 
        HRESULT present=g_pd3dDevice->Present(nullptr,nullptr,nullptr,nullptr); 
        if(present==D3DERR_DEVICELOST && g_pd3dDevice->TestCooperativeLevel()==D3DERR_DEVICENOTRESET) ResetDevice(); 
    }
    ImGui_ImplDX9_Shutdown(); ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext(); CleanupDeviceD3D(); DestroyWindow(hWnd); UnregisterClassW(wc.lpszClassName, wc.hInstance); return 0; }
