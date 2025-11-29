#include <windows.h>
#include <string>
#include <vector>
#include <chrono>
#include <thread>

static std::vector<std::string> LOG_LINES;
static std::vector<std::string> TIMELINE_LINES;

static const char* RESET = "\x1b[0m";
static const char* BLUE = "\x1b[34m";
static const char* RED = "\x1b[31m";
static const char* GREEN = "\x1b[32m";
static const char* ORANGE = "\x1b[38;5;208m";

void set_title(const wchar_t* title) {
    SetConsoleTitleW(title);
}

void enable_vt_mode() {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    GetConsoleMode(h, &mode);
    SetConsoleMode(h, mode | 0x0004);
}

void set_fixed_console(int cols, int lines, bool lock) {
    std::wstring cmd = L"/C mode con: cols=" + std::to_wstring(cols) + L" lines=" + std::to_wstring(lines);
    ShellExecuteW(nullptr, L"open", L"cmd.exe", cmd.c_str(), nullptr, SW_HIDE);
    if (lock) {
        HWND hwnd = GetConsoleWindow();
        if (hwnd) {
            LONG style = GetWindowLongW(hwnd, GWL_STYLE);
            style &= ~WS_MAXIMIZEBOX;
            style &= ~WS_THICKFRAME;
            SetWindowLongW(hwnd, GWL_STYLE, style);
            SetWindowPos(hwnd, 0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        }
    }
}

static std::string strip_ansi(const std::string& s) {
    std::string r; r.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\x1b') {
            while (i < s.size() && s[i] != 'm') i++;
            continue;
        }
        r.push_back(s[i]);
    }
    return r;
}

void cprint(const char* level, const std::string& message) {
    const char* color = BLUE;
    if (std::string(level) == "Success") color = GREEN;
    else if (std::string(level) == "Error") color = RED;
    else if (std::string(level) == "Warning") color = ORANGE;
    printf("%s%s%s - %s\n", color, level, RESET, message.c_str());
    std::string clean = strip_ansi(message);
    LOG_LINES.push_back(std::string(level) + " - " + clean);
    SYSTEMTIME st{}; GetLocalTime(&st);
    char tbuf[16]; snprintf(tbuf, sizeof(tbuf), "[%02d:%02d] ", st.wHour, st.wMinute);
    TIMELINE_LINES.push_back(std::string(tbuf) + clean);
}

void delay_ms(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

bool copy_logs_to_clipboard() {
    std::string text = "```\n";
    for (auto& l : LOG_LINES) { text += l; text += "\n"; }
    text += "```";
    if (!OpenClipboard(nullptr)) return false;
    EmptyClipboard();
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
    if (!h) { CloseClipboard(); return false; }
    char* p = (char*)GlobalLock(h);
    memcpy(p, text.c_str(), text.size() + 1);
    GlobalUnlock(h);
    SetClipboardData(CF_TEXT, h);
    CloseClipboard();
    return true;
}

// Expose snapshot of logs for GUI rendering
void get_logs(std::vector<std::string>& out) {
    out = LOG_LINES;
}

static std::wstring ws_from_utf8(const std::string& s){ return std::wstring(s.begin(), s.end()); }

void print_clean_timeline_to_console(){
    for (auto& l : TIMELINE_LINES){ printf("%s\n", l.c_str()); }
}

static bool get_temp_dir(std::wstring& out){
    wchar_t buf[MAX_PATH]{};
    DWORD n = GetTempPathW(MAX_PATH, buf);
    if (n == 0 || buf[0] == L'\0'){
        wchar_t env[MAX_PATH]{};
        DWORD m = GetEnvironmentVariableW(L"TEMP", env, MAX_PATH);
        if (m == 0 || env[0] == L'\0'){
            wchar_t win[MAX_PATH]{}; UINT w = GetWindowsDirectoryW(win, MAX_PATH);
            if (w == 0 || win[0] == L'\0') return false;
            out = std::wstring(win) + L"\\Temp\\";
            return true;
        }
        out = std::wstring(env);
    } else {
        out = std::wstring(buf);
    }
    if (!out.empty() && out.back() != L'\\') out.push_back(L'\\');
    return true;
}

bool write_clean_timeline(std::wstring& out_folder, std::wstring& out_file){
    std::wstring temp;
    if (!get_temp_dir(temp)) return false;
    SYSTEMTIME st{}; GetLocalTime(&st);
    unsigned int seed = (unsigned int)(GetTickCount64() ^ (unsigned long)GetCurrentProcessId()); srand(seed);
    wchar_t randstr[17]; for(int i=0;i<16;i++){ int v = rand() & 0xF; randstr[i] = (wchar_t)(v<10 ? (L'0'+v) : (L'A'+(v-10))); } randstr[16]=L'\0';
    wchar_t folder[MAX_PATH]; swprintf(folder, MAX_PATH, L"%ls%ls", temp.c_str(), randstr);
    CreateDirectoryW(folder, nullptr);
    wchar_t fname[MAX_PATH]; swprintf(fname, MAX_PATH, L"%ls\\Server-Check-%04d-%02d-%02d-%02d-%02d-%02d.rust", folder, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    HANDLE h = CreateFileW(fname, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    std::string content;
    for (auto& l : TIMELINE_LINES){ content += l; content += "\n"; }
    DWORD written = 0; WriteFile(h, content.c_str(), (DWORD)content.size(), &written, nullptr); CloseHandle(h);
    out_folder = folder; out_file = fname;
    return true;
}

void open_folder(const std::wstring& folder){ ShellExecuteW(nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL); }
void open_folder_select(const std::wstring& file){
    std::wstring args = L"/select,\"" + file + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}

bool write_clean_timeline_and_open(){ std::wstring f, p; if (!write_clean_timeline(f, p)) return false; open_folder(f); return true; }
