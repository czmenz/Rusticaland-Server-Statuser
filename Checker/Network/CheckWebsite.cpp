#include <windows.h>
#include <winhttp.h>
#include <string>
#include <vector>
#include <chrono>
#include <numeric>
#pragma comment(lib, "winhttp.lib")

void cprint(const char*, const std::string&);
void delay_ms(int);

static int http_ping(const wchar_t* host, const wchar_t* path){
    auto t0 = std::chrono::high_resolution_clock::now();
    HINTERNET h = WinHttpOpen(L"RusticalandChecker/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!h) return -1;
    HINTERNET c = WinHttpConnect(h, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!c) { WinHttpCloseHandle(h); return -1; }
    HINTERNET r = WinHttpOpenRequest(c, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!r) { WinHttpCloseHandle(c); WinHttpCloseHandle(h); return -1; }
    BOOL ok = WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (ok) ok = WinHttpReceiveResponse(r, nullptr);
    WinHttpCloseHandle(r); WinHttpCloseHandle(c); WinHttpCloseHandle(h);
    if (!ok) return -1;
    auto t1 = std::chrono::high_resolution_clock::now();
    return (int)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
}

void check_website(){
    cprint("Info", "Checking Rusticaland Website Response");
    std::vector<int> samples;
    for (int i=0;i<5;i++){ int ms = http_ping(L"rusticaland.net", L"/"); if (ms>=0) samples.push_back(ms); delay_ms(100); }
    if (!samples.empty()){ int avg = (int)std::accumulate(samples.begin(), samples.end(), 0) / (int)samples.size(); cprint("Success", std::string("Rusticaland Website (response to rusticaland website ") + std::to_string(avg) + " ms)"); }
    else cprint("Error", "Could not connect to rusticaland website");
}
