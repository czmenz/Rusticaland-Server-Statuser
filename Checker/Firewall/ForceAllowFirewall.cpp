// Force-allow rules for Rusticaland: ports, host and RustClient.exe (if found)
#include <windows.h>
#include <string>
#include <vector>

void cprint(const char*, const std::string&);

static bool write_file(const std::wstring& path, const std::wstring& content){
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD w = 0; BOOL ok = WriteFile(h, content.c_str(), (DWORD)(content.size()*sizeof(wchar_t)), &w, nullptr);
    CloseHandle(h);
    return ok==TRUE;
}

static std::wstring temp_ps1(const wchar_t* name){
    wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
    std::wstring p=tmp; if(!p.empty() && p.back()!=L'\\') p.push_back(L'\\'); p+=name; return p;
}

bool force_allow_firewall(){
    cprint("Info", "ForceAllowing Rusticaland Servers");
    // Known ports (game/query + web)
    int ports[] = {28022,28023,28098,28099,28053,28019,28015,28016,28066,28067,80,443};
    // Try to find RustClient.exe in common path (best-effort)
    std::wstring rustPath = L"";
    wchar_t pf[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableW(L"ProgramFiles(x86)", pf, MAX_PATH);
    if (n > 0 && n < MAX_PATH){
        rustPath = std::wstring(pf) + L"\\Steam\\steamapps\\common\\Rust\\RustClient.exe";
        if (GetFileAttributesW(rustPath.c_str()) == INVALID_FILE_ATTRIBUTES) rustPath.clear();
    }

    std::wstring ps;
    ps += L"$ErrorActionPreference='SilentlyContinue';";
    ps += L"Import-Module NetSecurity -ErrorAction SilentlyContinue;";
    ps += L"$ports=@(";
    for (size_t i=0;i<sizeof(ports)/sizeof(ports[0]);++i){ ps += std::to_wstring(ports[i]); if (i+1<sizeof(ports)/sizeof(ports[0])) ps += L","; }
    ps += L");";
    ps += L"$hostIps=(Resolve-DnsName rusticaland.net -Type A | %{$_.IPAddress});";
    ps += L"foreach($p in $ports){";
    ps += L" if (-not (Get-NetFirewallRule -DisplayName ('Rusticaland Allow OUT UDP '+$p))) { netsh advfirewall firewall add rule name='Rusticaland Allow OUT UDP '+$p dir=out action=allow protocol=UDP remoteport=$p enable=yes | Out-Null }";
    ps += L" if (-not (Get-NetFirewallRule -DisplayName ('Rusticaland Allow OUT TCP '+$p))) { netsh advfirewall firewall add rule name='Rusticaland Allow OUT TCP '+$p dir=out action=allow protocol=TCP remoteport=$p enable=yes | Out-Null }";
    ps += L" if (-not (Get-NetFirewallRule -DisplayName ('Rusticaland Allow IN UDP '+$p))) { netsh advfirewall firewall add rule name='Rusticaland Allow IN UDP '+$p dir=in action=allow protocol=UDP localport=$p enable=yes | Out-Null }";
    ps += L" if (-not (Get-NetFirewallRule -DisplayName ('Rusticaland Allow IN TCP '+$p))) { netsh advfirewall firewall add rule name='Rusticaland Allow IN TCP '+$p dir=in action=allow protocol=TCP localport=$p enable=yes | Out-Null }";
    ps += L"}";
    ps += L"foreach($ip in $hostIps){";
    ps += L" if (-not (Get-NetFirewallRule -DisplayName ('Rusticaland Allow Host OUT '+$ip))) { netsh advfirewall firewall add rule name='Rusticaland Allow Host OUT '+$ip dir=out action=allow remoteip=$ip enable=yes | Out-Null }";
    ps += L" if (-not (Get-NetFirewallRule -DisplayName ('Rusticaland Allow Host IN '+$ip))) { netsh advfirewall firewall add rule name='Rusticaland Allow Host IN '+$ip dir=in action=allow remoteip=$ip enable=yes | Out-Null }";
    ps += L"}";
    if (!rustPath.empty()){
        ps += L"if (-not (Get-NetFirewallRule -DisplayName 'RustClient Allow')) { New-NetFirewallRule -DisplayName 'RustClient Allow' -Direction Inbound -Action Allow -Program '" + rustPath + L"' -Profile Any | Out-Null }";
        ps += L"if (-not (Get-NetFirewallRule -DisplayName 'RustClient Allow Out')) { New-NetFirewallRule -DisplayName 'RustClient Allow Out' -Direction Outbound -Action Allow -Program '" + rustPath + L"' -Profile Any | Out-Null }";
    }

    std::wstring p = temp_ps1(L"rf_force_allow.ps1");
    if (!write_file(p, ps)) { cprint("Error", "Force allow script creation failed"); return false; }

    std::wstring cmd = L"/C powershell -NoProfile -ExecutionPolicy Bypass -File '" + p + L"'";
    ShellExecuteW(nullptr, L"open", L"cmd.exe", cmd.c_str(), nullptr, SW_HIDE);
    Sleep(2000);
    cprint("Success", "Force allow applied (rules added if missing)");
    return true;
}
