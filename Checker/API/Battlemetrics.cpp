#include <windows.h>
#include <winhttp.h>
#include <string>
#include <cstring>
#include <cctype>
#include <cstdlib>
#pragma comment(lib, "winhttp.lib")

void cprint(const char*, const std::string&);

static std::wstring to_w(const std::string& s){return std::wstring(s.begin(), s.end());}

std::string BM_TOKEN = "BATTLEMETRICS-API";

static bool bm_fetch(const std::wstring& path, std::string& body){
    HINTERNET h = WinHttpOpen(L"RusticalandChecker/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!h) return false;
    HINTERNET c = WinHttpConnect(h, L"api.battlemetrics.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!c){ WinHttpCloseHandle(h); return false; }
    HINTERNET r = WinHttpOpenRequest(c, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!r){ WinHttpCloseHandle(c); WinHttpCloseHandle(h); return false; }
    BOOL ok = WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (ok) ok = WinHttpReceiveResponse(r, nullptr);
    if (ok){ DWORD avail=0; do{ WinHttpQueryDataAvailable(r,&avail); if(!avail) break; std::string chunk; chunk.resize(avail); DWORD rd=0; WinHttpReadData(r,&chunk[0],avail,&rd); body.append(chunk.data(), rd);} while(avail); }
    WinHttpCloseHandle(r); WinHttpCloseHandle(c); WinHttpCloseHandle(h);
    return !body.empty();
}

static size_t bm_next_attr_pos(const std::string& body, size_t from){
    return body.find("\"attributes\":{", from);
}

static size_t bm_attr_end(const std::string& body, size_t start){
    if (start == std::string::npos) return std::string::npos;
    size_t rel = body.find("},\"relationships\"", start);
    size_t next = body.find("\"attributes\":{", start+1);
    size_t end = std::min(rel == std::string::npos ? body.size() : rel, next == std::string::npos ? body.size() : next);
    return end;
}

static int bm_extract_int(const std::string& body, size_t start, size_t end, const char* key){
    std::string k = std::string("\"") + key + "\":";
    size_t p = body.find(k, start);
    if (p == std::string::npos || p >= end) return 0;
    p += k.size();
    while (p < end && (body[p] == ' ' || body[p] == '"')) p++;
    size_t e = p;
    while (e < end && isdigit((unsigned char)body[e])) e++;
    return atoi(body.substr(p, e-p).c_str());
}

static std::string bm_extract_str(const std::string& body, size_t start, size_t end, const char* key){
    std::string k = std::string("\"") + key + "\":\"";
    size_t p = body.find(k, start);
    if (p == std::string::npos || p >= end) return std::string();
    p += k.size();
    size_t e = body.find("\"", p);
    if (e == std::string::npos || e > end) return std::string();
    return body.substr(p, e - p);
}

static int parse_desired_port(const std::string& query){
    size_t colon = query.rfind(':');
    if (colon == std::string::npos) return 0;
    return atoi(query.substr(colon+1).c_str());
}

void battlemetrics_status(const std::string& label, const std::string& query){
    std::string body;
    std::wstring path = L"/servers?filter[game]=rust&filter[search]=" + to_w(query);
    if (!bm_fetch(path, body)) { cprint("Error", "Could not check " + label + " status"); return; }
    int desired = parse_desired_port(query);
    size_t pos = bm_next_attr_pos(body, body.find("\"data\":"));
    while (pos != std::string::npos){
        size_t end = bm_attr_end(body, pos);
        int port = bm_extract_int(body, pos, end, "port");
        std::string status = bm_extract_str(body, pos, end, "status");
        std::string state = bm_extract_str(body, pos, end, "state");
        if (desired == 0 || port == desired){
            if (status == "online" || state == "online") { cprint("Success", label + " Online"); return; }
            if (status == "offline" || state == "offline") { cprint("Warning", label + " Offline"); return; }
            break;
        }
        pos = bm_next_attr_pos(body, end);
    }
    if (body.find("\"status\":\"online\"") != std::string::npos || body.find("\"state\":\"online\"") != std::string::npos) { cprint("Success", label + " Online"); return; }
    if (body.find("\"offline\"") != std::string::npos) { cprint("Warning", label + " Offline"); return; }
    cprint("Info", label + " status unknown");
}

int battlemetrics_query_port(const std::string& label, const std::string& query){
    std::string body;
    std::wstring path = L"/servers?filter[game]=rust&filter[search]=" + to_w(query);
    if (!bm_fetch(path, body)) return 0;
    int desired = parse_desired_port(query);
    int fallback = 0;
    size_t pos = bm_next_attr_pos(body, body.find("\"data\":"));
    while (pos != std::string::npos){
        size_t end = bm_attr_end(body, pos);
        int pq = bm_extract_int(body, pos, end, "portQuery");
        int p = bm_extract_int(body, pos, end, "port");
        if (fallback == 0) fallback = pq > 0 ? pq : (p > 0 ? p : 0);
        if (desired == 0 || p == desired){
            if (pq > 0) return pq;
            if (p > 0) return p;
        }
        pos = bm_next_attr_pos(body, end);
    }
    return fallback;
}

bool battlemetrics_is_online(const std::string& label, const std::string& query){
    std::string body;
    std::wstring path = L"/servers?filter[game]=rust&filter[search]=" + to_w(query);
    if (!bm_fetch(path, body)) return false;
    int desired = parse_desired_port(query);
    size_t pos = bm_next_attr_pos(body, body.find("\"data\":"));
    while (pos != std::string::npos){
        size_t end = bm_attr_end(body, pos);
        int p = bm_extract_int(body, pos, end, "port");
        std::string status = bm_extract_str(body, pos, end, "status");
        std::string state = bm_extract_str(body, pos, end, "state");
        if (desired == 0 || p == desired){
            if (status == "online" || state == "online") return true;
            return false;
        }
        pos = bm_next_attr_pos(body, end);
    }
    return body.find("\"status\":\"online\"") != std::string::npos || body.find("\"state\":\"online\"") != std::string::npos;
}
