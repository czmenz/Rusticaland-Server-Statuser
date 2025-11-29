#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <vector>
#include <chrono>
#include <numeric>
#include <cstring>
#pragma comment(lib, "ws2_32.lib")

void cprint(const char*, const std::string&);
void delay_ms(int);
void battlemetrics_status(const std::string&, const std::string&);
int battlemetrics_query_port(const std::string&, const std::string&);

// Single A2S ping with challenge handshake; returns latency ms or -1 on error
static int a2s_query(const char* host, int port, int timeout_ms){
    // Initialize Winsock once
    static bool wsa_init = false;
    WSADATA w{};
    if (!wsa_init){
        if (WSAStartup(MAKEWORD(2,2), &w)==0) wsa_init=true;
    }

    // UDP socket and timeouts
    SOCKET s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == INVALID_SOCKET) return -1;
    DWORD to = timeout_ms;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&to, sizeof(to));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&to, sizeof(to));

    // Resolve host to IPv4
    addrinfo hints{}; hints.ai_family = AF_INET; hints.ai_socktype = SOCK_DGRAM; hints.ai_protocol = IPPROTO_UDP; 
    addrinfo* res=nullptr;
    if (getaddrinfo(host, nullptr, &hints, &res) != 0 || !res){
        closesocket(s);
        cprint("Error", std::string("DNS resolve failed for ")+host);
        return -1;
    }
    sockaddr_in addr{}; 
    addr.sin_family = AF_INET; 
    addr.sin_port = htons((u_short)port); 
    addr.sin_addr = ((sockaddr_in*)res->ai_addr)->sin_addr; 
    freeaddrinfo(res);

    // Debug target IP:port
    char ipbuf[64]{}; 
    inet_ntop(AF_INET, &addr.sin_addr, ipbuf, sizeof(ipbuf));
    cprint("Info", std::string("Debug - sending A2S to ")+ipbuf+":"+std::to_string(port));

    // A2S_INFO payload + 0-terminated string
    static const unsigned char payload[] = {
        0xFF,0xFF,0xFF,0xFF,
        0x54,
        'S','o','u','r','c','e',' ','E','n','g','i','n','e',' ','Q','u','e','r','y',
        0x00
    };
    const int payload_len = (int)sizeof(payload);

    // Send, possibly handle challenge, and measure time
    auto t0 = std::chrono::high_resolution_clock::now();
    sendto(s, (const char*)payload, payload_len, 0, (sockaddr*)&addr, sizeof(addr));

    char buf[4096]; 
    sockaddr_in from{}; 
    int fromlen = sizeof(from); 
    int len = recvfrom(s, buf, sizeof(buf), 0, (sockaddr*)&from, &fromlen);

    // Challenge token (0x41) -> resend with token appended
    if (len >= 5 &&
        (unsigned char)buf[0]==0xFF && (unsigned char)buf[1]==0xFF &&
        (unsigned char)buf[2]==0xFF && (unsigned char)buf[3]==0xFF &&
        (unsigned char)buf[4]==0x41){
        unsigned char payload2[64]; 
        memcpy(payload2, payload, payload_len); 
        memcpy(payload2 + payload_len, buf+5, 4);
        sendto(s, (const char*)payload2, payload_len + 4, 0, (sockaddr*)&addr, sizeof(addr));
        len = recvfrom(s, buf, sizeof(buf), 0, (sockaddr*)&from, &fromlen);
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    if (len<=0){ 
        int err=WSAGetLastError(); 
        closesocket(s); 
        cprint("Info", std::string("Debug - recv failed (WSA ")+std::to_string(err)+")"); 
        return -1; 
    }
    closesocket(s);
    return (int)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
}

// Format min/avg/max for sample set
static std::string stats(const std::vector<int>& v){ 
    int mn=1000000000, mx=0, avg=0; 
    for(int x: v){ 
        if(x<mn) mn=x; 
        if(x>mx) mx=x; 
        avg+=x;
    } 
    if(!v.empty()) avg/= (int)v.size(); 
    return std::string("(min: ")+std::to_string(mn)+" ms | average: "+std::to_string(avg)+" ms | max: "+std::to_string(mx)+" ms)"; 
}

// Run multiple attempts on a single port and collect latencies
static std::vector<int> a2s_query_multi(const char* host, int port, int count){
    std::vector<int> values;
    for (int i=0;i<count;i++){
        int ms = a2s_query(host, port, 2000);
        if (ms>=0){ 
            cprint("Info", std::string("Debug - attempt ")+std::to_string(i+1)+" on "+host+":"+std::to_string(port)+" = "+std::to_string(ms)+" ms"); 
            values.push_back(ms); 
        }
    }
    return values;
}

// Flow: try game port, then optional query port; also report BM status
bool check_server_flow(const char* host, int port_game, int port_query, const char* label){
    cprint("Info", std::string("Checking ")+label+" Response");
    std::string bmQuery = std::string(host) + ":" + std::to_string(port_game);

    std::vector<int> samples = a2s_query_multi(host, port_game, 5);
    if (!samples.empty()){ 
        cprint("Success", std::string(label)+" "+stats(samples)); 
        battlemetrics_status(label, bmQuery); 
        return true; 
    }

    if (port_query > 0){
        std::vector<int> samples2 = a2s_query_multi(host, port_query, 5);
        if (!samples2.empty()){ 
            cprint("Success", std::string(label)+" "+stats(samples2)); 
            battlemetrics_status(label, bmQuery); 
            return true; 
        }
    }

    battlemetrics_status(label, bmQuery);
    cprint("Error", std::string("Could not connect to ")+label);
    return false;
}
