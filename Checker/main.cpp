#include <windows.h>
#include <string>
#include <vector>
#include <cstdio>
#include <conio.h>
#include "GUI/gui.hpp"
#include <thread>

void set_title(const wchar_t*);
void enable_vt_mode();
void set_fixed_console(int,int,bool);
void cprint(const char*, const std::string&);
void delay_ms(int);
bool copy_logs_to_clipboard();
void check_internet();
void check_website();
void check_vpn();
bool check_server_flow(const char*, int, int, const char*);
bool firewall_read();
bool firewall_write();
bool force_allow_firewall();
extern std::string BM_TOKEN;
void get_logs(std::vector<std::string>&);
void gui_set_phase(int idx, const wchar_t* resp);
void print_clean_timeline_to_console();
bool write_clean_timeline(std::wstring& out_folder, std::wstring& out_file);
void open_folder(const std::wstring& folder);
static std::wstring w(const std::string& s){ return std::wstring(s.begin(), s.end()); }

int main(){

    
    HWND ch = GetConsoleWindow(); if (ch) ShowWindow(ch, SW_HIDE);


    enable_vt_mode();
    set_title(L"Rusticaland Server Checker");
    set_fixed_console(80,25,false);
    system("cls");
    int argc = 0; int start_deg = 0; LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc); if (argv && argc >= 2){ wchar_t* end=nullptr; long v=wcstol(argv[1], &end, 10); if (end && *end==L'\0') start_deg=(int)v; } if (argv) LocalFree(argv); if (start_deg<0 || start_deg>360) start_deg=0;
    // Run GUI concurrently to show live steps/responses
    std::thread gui_thread([&](){ checker_gui_loading(start_deg); });
    // Wait for user to click Check in GUI
    for(;;){ if (gui_is_started()) break; if (gui_should_exit()) { if (gui_thread.joinable()) gui_thread.join(); return 0; } Sleep(50); }

    // 0. Network
    gui_set_phase(0, L"...");
    if (gui_should_exit()) { if (gui_thread.joinable()) gui_thread.join(); return 0; }
    check_internet();
    { std::vector<std::string> logs; get_logs(logs); bool ok=false; for(int i=(int)logs.size()-1;i>=0;--i){ if (logs[i].find("Network is connected")!=std::string::npos){ ok=true; break; } if (logs[i].find("Network is not connected")!=std::string::npos){ ok=false; break; } } gui_set_phase(0, ok ? L"Network is connected" : L"Network is not connected"); delay_ms(2000); }

    // 1. Website
    gui_set_phase(1, L"...");
    if (gui_should_exit()) { if (gui_thread.joinable()) gui_thread.join(); return 0; }
    check_website();
    { std::vector<std::string> logs; get_logs(logs); std::wstring resp=L"Response: N/A"; for(int i=(int)logs.size()-1;i>=0;--i){ const std::string& s=logs[i]; size_t p=s.find("response to rusticaland website "); if (p!=std::string::npos){ p+=strlen("response to rusticaland website "); size_t e=s.find(" ms", p); if (e!=std::string::npos){ std::string num=s.substr(p,e-p); resp=std::wstring(L"Response: ")+std::wstring(num.begin(),num.end())+L"ms"; break; } } } gui_set_phase(1, resp.c_str()); delay_ms(2000); }

    // 2-3. VPN local/external
    gui_set_phase(2, L"...");
    if (gui_should_exit()) { if (gui_thread.joinable()) gui_thread.join(); return 0; }
    check_vpn();
    { std::vector<std::string> logs; get_logs(logs); std::wstring resp=L"No VPN/Proxy detected"; for(int i=(int)logs.size()-1;i>=0;--i){ const std::string& s=logs[i]; if (s.find("No VPN/Proxy detected")!=std::string::npos){ resp=L"No VPN/Proxy detected"; break; } if (s.find("VPN adapter detected")!=std::string::npos || s.find("Proxy detected")!=std::string::npos || s.find("Installed VPN clients detected")!=std::string::npos){ resp=L"Found VPN/Proxy adapter"; break; } } gui_set_phase(2, resp.c_str()); delay_ms(2000); }
    gui_set_phase(3, L"...");
    if (gui_should_exit()) { if (gui_thread.joinable()) gui_thread.join(); return 0; }
    { std::vector<std::string> logs; get_logs(logs); std::wstring resp=L"No VPN/Proxy detected"; for(int i=(int)logs.size()-1;i>=0;--i){ const std::string& s=logs[i]; if (s.find("External check: No VPN/Proxy detected")!=std::string::npos){ resp=L"No VPN/Proxy detected"; break; } if (s.find("External check suggests VPN/Proxy")!=std::string::npos){ resp=L"Found VPN/Proxy"; break; } } gui_set_phase(3, resp.c_str()); delay_ms(2000); }
    // 5. Firewall read/write
    bool fw_found = firewall_read();
    bool fw_removed = false;
    if (gui_should_exit()) { if (gui_thread.joinable()) gui_thread.join(); return 0; }
    if (fw_found) { fw_removed = firewall_write(); if (gui_should_exit()) { if (gui_thread.joinable()) gui_thread.join(); return 0; } delay_ms(500); firewall_read(); }
    gui_set_phase(5, L"...");
    { std::wstring resp = fw_found ? L"Fixing firewall" : L"No changes required on firewall"; gui_set_phase(5, resp.c_str()); delay_ms(2000); }
    gui_set_phase(6, L"...");
    if (gui_should_exit()) { if (gui_thread.joinable()) gui_thread.join(); return 0; }
    cprint("Info", "ForceAllowing Rusticaland Servers");
    bool fw_forced = force_allow_firewall();
    { std::wstring resp = fw_forced ? L"Allowed Successfully" : L"Could not allow"; gui_set_phase(6, resp.c_str()); delay_ms(2000); }
    struct S{ const char* host; int port_game; int port_query; const char* name; } servers[] = {
        {"rusticaland.com", 28022, 28023, "Rusticaland Vanilla"},
        {"rusticaland.com", 28098, 28099, "Rusticaland Sandbox"},
        {"rusticaland.com", 28053, 28019, "Rusticaland OneGrid"},
        {"rusticaland.com", 28015, 28016, "Rusticaland Monthly"},
        {"rusticaland.com", 28066, 28067, "Rusticaland Modded"}
    };
    // 4. Servers
    gui_set_phase(4, L"...");
    for (int i=0;i<(int)(sizeof(servers)/sizeof(servers[0]));++i){ auto& s = servers[i];
        if (gui_should_exit()) { if (gui_thread.joinable()) gui_thread.join(); return 0; }
        gui_set_server_index(i, (int)(sizeof(servers)/sizeof(servers[0])));
        gui_set_current_server(w(s.name).c_str());
        gui_set_phase(4, L"Checking...");
        ULONGLONG t0 = GetTickCount64();
        check_server_flow(s.host, s.port_game, s.port_query, s.name);
        ULONGLONG t1 = GetTickCount64();
        gui_add_server_duration_ms((int)(t1 - t0));
        { std::vector<std::string> logs; get_logs(logs); std::wstring resp=L"Could not connect"; for(int j=(int)logs.size()-1;j>=0;--j){ const std::string& line=logs[j]; if (line.find(s.name)!=std::string::npos){ size_t p=line.find("(min: "); if (p!=std::string::npos){ size_t p1=line.find(": ", p); size_t p2=line.find(" ms", p1+2); std::string mn=line.substr(p1+2, p2-(p1+2)); size_t q=line.find("average: ", p2); size_t q2=line.find(" ms", q+9); std::string av=line.substr(q+9, q2-(q+9)); size_t r=line.find("max: ", q2); size_t r2=line.find(" ms", r+5); std::string mx=line.substr(r+5, r2-(r+5)); std::wstring wm(mn.begin(),mn.end()); std::wstring wa(av.begin(),av.end()); std::wstring wx(mx.begin(),mx.end()); resp = std::wstring(L"(min: ")+wm+L"ms | avr: "+wa+L"ms | max: "+wx+L"ms)"; break; } } }
            gui_set_phase(4, resp.c_str()); }
        delay_ms(2000);
        printf("\n");
    }
    gui_set_current_server(L"");
    std::wstring out_dir, out_file;
    print_clean_timeline_to_console();
    bool saved = write_clean_timeline(out_dir, out_file);
    gui_set_phase(4, L"Completed");
    gui_mark_completed();
    if (saved){
        open_folder(out_dir);
    } else {
        cprint("Error", "Could not create log in TEMP folder");
    }
    delay_ms(1500);
    if (fw_removed) {
        cprint("Info", "Firewall changes applied, rechecking servers");
        for (auto& s : servers){ check_server_flow(s.host, s.port_game, s.port_query, s.name); delay_ms(500); printf("\n"); }
    }
    printf("\nClick X to exit\n");
    printf("Click C to copy console input\n");
    for(;;){
        if (gui_should_exit()) break;
        printf("> ");
        int ch = 0;
        for(;;){ if (gui_should_exit()) { ch = 0; break; } if (_kbhit()){ ch = _getch(); break; } Sleep(50); }
        if (gui_should_exit()) break;
        if (ch=='X' || ch=='x') break;
        if (ch=='C' || ch=='c'){
            if (copy_logs_to_clipboard()) cprint("Success", "Console output copied to clipboard for Discord"); else cprint("Error", "Could not copy to clipboard");
        } else {
            cprint("Info", "Press X to exit or C to copy");
        }
    }
    if (gui_thread.joinable()) gui_thread.join();
    return 0;
}
