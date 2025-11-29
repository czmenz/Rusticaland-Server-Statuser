#include <windows.h>
#include <string>
#include <vector>
bool firewall_read();
void cprint(const char*, const std::string&);
static bool write_file(const std::wstring& path, const std::wstring& content){ HANDLE h=CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr); if(h==INVALID_HANDLE_VALUE) return false; DWORD w=0; BOOL ok=WriteFile(h, content.c_str(), (DWORD)(content.size()*sizeof(wchar_t)), &w, nullptr); CloseHandle(h); return ok==TRUE; }
static std::wstring temp_ps1(const wchar_t* name){ wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH,tmp); std::wstring p=tmp; if(!p.empty() && p.back()!=L'\\') p.push_back(L'\\'); p+=name; return p; }
bool firewall_read(){
    cprint("Info","Checking Firewall rules for Rusticaland");
    std::wstring ps =
        L"$ErrorActionPreference='SilentlyContinue';"
        L"$ports=@(28022,28023,28098,28099,28053,28019,28015,28016,28066,28067,80,443);"
        L"$ips=(Resolve-DnsName rusticaland.net -Type A | %{$_.IPAddress});"
        L"$rules=Get-NetFirewallRule -PolicyStore ActiveStore | Where-Object {$_.Action -eq 'Block'};"
        L"foreach($r in $rules){"
        L" $pf=$r|Get-NetFirewallPortFilter; $af=$r|Get-NetFirewallAddressFilter; $app=$r|Get-NetFirewallApplicationFilter; $m=$false;"
        L" if($pf){ if((($pf.Protocol -eq 'UDP') -or ($pf.Protocol -eq 'TCP')) -and (($ports -contains $pf.RemotePort) -or ($ports -contains $pf.LocalPort))){ $m=$true } }"
        L" if($af){ if($af.RemoteAddress){ foreach($ip in $ips){ if($af.RemoteAddress -like ('*'+$ip+'*')){ $m=$true } } } }"
        L" if($app){ if($app.Program -like '*RustClient.exe*'){ $m=$true } }"
        L" if($m){ Write-Output ('RULE='+$r.DisplayName) }"
        L"}";
    std::wstring p = temp_ps1(L"rf_read.ps1");
    if(!write_file(p, ps)){ cprint("Error","Could not create firewall read script"); return false; }
    std::wstring cmd = L"powershell -NoProfile -ExecutionPolicy Bypass -File \"" + p + L"\"";
    FILE* f = _wpopen(cmd.c_str(), L"rt"); if(!f){ cprint("Error","Could not execute firewall read"); return false; }
    wchar_t line[1024]; bool any=false; while(fgetws(line,1024,f)){ std::wstring wline(line); if(wline.find(L"RULE=")==0){ std::string name(wline.begin()+5, wline.end()); while(!name.empty() && (name.back()=='\r' || name.back()=='\n')) name.pop_back(); cprint("Warning", std::string("Firewall block rule: ")+name); any=true; } }
    _pclose(f);
    if(!any) cprint("Success","No blocking firewall rules detected for Rusticaland");
    return any;
}
