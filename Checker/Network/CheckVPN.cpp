#include <windows.h>
#include <winreg.h>
#include <winhttp.h>
#include <string>
#include <vector>
#include <algorithm>
#include <iphlpapi.h>
#pragma comment(lib, "iphlpapi.lib")

void cprint(const char*, const std::string&);
void delay_ms(int);

static bool detect_proxy_env(std::string& detail){
    const char* vars[] = { "HTTP_PROXY", "HTTPS_PROXY", "http_proxy", "https_proxy" };
    for (auto v : vars){ char* val = nullptr; size_t len = 0; _dupenv_s(&val, &len, v); if (val && len){ detail = val; free(val); return true; } }
    return false;
}

static bool detect_proxy_reg(std::string& detail){
    HKEY key; DWORD enabled = 0, type = 0, size = sizeof(DWORD);
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings", 0, KEY_READ, &key) != ERROR_SUCCESS) return false;
    bool on = false;
    if (RegQueryValueExA(key, "ProxyEnable", NULL, &type, (LPBYTE)&enabled, &size) == ERROR_SUCCESS && enabled == 1){ on = true; }
    char buf[512]; size = sizeof(buf); type = 0;
    if (RegQueryValueExA(key, "ProxyServer", NULL, &type, (LPBYTE)buf, &size) == ERROR_SUCCESS){ detail = std::string(buf, buf + size - 1); }
    RegCloseKey(key);
    return on;
}

static bool detect_vpn_adapter(){
    ULONG sz = 0;
    GetAdaptersInfo(NULL, &sz);
    if (sz == 0) return false;
    std::vector<char> buf(sz);
    PIP_ADAPTER_INFO ai = (PIP_ADAPTER_INFO)buf.data();
    if (GetAdaptersInfo(ai, &sz) != NO_ERROR) return false;
    const char* keywords[] = { "vpn", "tap", "tun", "wireguard", "anyconnect", "openvpn", "ikev2", "nordvpn", "protonvpn", "surfshark" };
    for (PIP_ADAPTER_INFO cur = ai; cur; cur = cur->Next){
        std::string desc = cur->Description ? cur->Description : "";
        std::string name = cur->AdapterName ? cur->AdapterName : "";
        std::string low = desc + " " + name;
        std::transform(low.begin(), low.end(), low.begin(), ::tolower);
        for (auto k : keywords){ if (low.find(k) != std::string::npos) return true; }
    }
    return false;
}

static void detect_installed_vpn(std::vector<std::string>& names){
    HKEY roots[] = { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER };
    const char* paths[] = {
        "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
        "SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall"
    };
    const char* patterns[] = { "vpn", "wireguard", "openvpn", "anyconnect", "nordvpn", "protonvpn", "surfshark", "expressvpn", "windscribe", "forticlient" };
    for (HKEY root : roots){
        for (auto p : paths){ HKEY k; if (RegOpenKeyExA(root, p, 0, KEY_READ, &k) != ERROR_SUCCESS) continue; DWORD idx=0; CHAR sub[256]; while (RegEnumKeyA(k, idx++, sub, sizeof(sub)) == ERROR_SUCCESS){ HKEY sk; if (RegOpenKeyExA(k, sub, 0, KEY_READ, &sk) != ERROR_SUCCESS) continue; CHAR name[256]; DWORD t=0, sz=sizeof(name); if (RegQueryValueExA(sk, "DisplayName", NULL, &t, (LPBYTE)name, &sz) == ERROR_SUCCESS){ std::string n(name); std::string ln = n; std::transform(ln.begin(), ln.end(), ln.begin(), ::tolower); for (auto pat : patterns){ if (ln.find(pat) != std::string::npos){ names.push_back(n); break; } } } RegCloseKey(sk);} RegCloseKey(k);} }
    std::sort(names.begin(), names.end()); names.erase(std::unique(names.begin(), names.end()), names.end());
}

void check_vpn(){
    cprint("Info", "Checking VPN/Proxy");
    std::string proxy_detail;
    bool proxy = detect_proxy_env(proxy_detail) || detect_proxy_reg(proxy_detail);
    if (proxy){ if (!proxy_detail.empty()) cprint("Warning", std::string("Proxy detected (") + proxy_detail + ")"); else cprint("Warning", "Proxy detected"); }
    std::vector<std::string> vpns; detect_installed_vpn(vpns);
    if (detect_vpn_adapter()) cprint("Warning", "VPN adapter detected");
    if (!vpns.empty()){ std::string list; for (size_t i=0;i<vpns.size();++i){ if (i) list += ", "; list += vpns[i]; } cprint("Warning", std::string("Installed VPN clients detected: ") + list); }
    if (!proxy && vpns.empty()) cprint("Success", "No VPN/Proxy detected");

    cprint("Info", "Checking external VPN/Proxy status");
    HINTERNET h = WinHttpOpen(L"RusticalandChecker/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (h){
        HINTERNET c = WinHttpConnect(h, L"ip-api.com", INTERNET_DEFAULT_HTTP_PORT, 0);
        if (c){
            HINTERNET r = WinHttpOpenRequest(c, L"GET", L"/json/?fields=status,message,query,isp,org,as,proxy,hosting", nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
            if (r){
                BOOL ok = WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
                if (ok) ok = WinHttpReceiveResponse(r, nullptr);
                std::string body;
                if (ok){
                    DWORD avail = 0;
                    do { WinHttpQueryDataAvailable(r, &avail); if (!avail) break; std::string chunk; chunk.resize(avail); DWORD rd=0; WinHttpReadData(r, &chunk[0], avail, &rd); body.append(chunk.data(), rd);} while (avail);
                }
                WinHttpCloseHandle(r);
                if (!body.empty()){
                    auto contains = [&](const std::string& k){ return body.find(k) != std::string::npos; };
                    auto extract = [&](const std::string& k){ size_t p = body.find("\"" + k + "\":\""); if (p == std::string::npos) return std::string(); p += k.size() + 4; size_t e = body.find("\"", p); if (e == std::string::npos) return std::string(); return body.substr(p, e - p); };
                    bool proxy_ext = contains("\"proxy\":true");
                    bool hosting_ext = contains("\"hosting\":true");
                    std::string isp = extract("isp");
                    std::string org = extract("org");
                    if (proxy_ext || hosting_ext){ cprint("Warning", std::string("External check suggests VPN/Proxy/Hosting (ISP ") + (isp.empty()?"unknown":isp) + ", Org " + (org.empty()?"unknown":org) + ")"); }
                    else { cprint("Success", std::string("External check: No VPN/Proxy detected (ISP ") + (isp.empty()?"unknown":isp) + ", Org " + (org.empty()?"unknown":org) + ")"); }
                } else { cprint("Error", "External VPN check failed"); }
            }
        }
        WinHttpCloseHandle(c);
        WinHttpCloseHandle(h);
    }
}
