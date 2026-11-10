// StripBay Remote - Netz im Plugin (siehe RemoteNet.h)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WINSOCK_DEPRECATED_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS   // WSAAsyncSelect / gethostname: bewusst (Meldungen im Fensterthread)
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif

#include "RemoteNet.h"
#include "NetIf.h"

#include <algorithm>
#include <cstring>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace
{
    const UINT WM_SB_TCP = WM_APP + 0x5B1;
    const UINT WM_SB_UDP = WM_APP + 0x5B2;
    const UINT WM_SB_CLOSE = WM_APP + 0x5B3;
    const UINT_PTR kTickTimer = 1;
    const wchar_t* kNetClass = L"StripBayRemoteNet";

    long long NowMs() { return (long long)GetTickCount64(); }

    LRESULT CALLBACK NetProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        if (msg == WM_NCCREATE)
        {
            CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
            return TRUE;
        }
        RemoteServerNet* self = reinterpret_cast<RemoteServerNet*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (self)
        {
            if (msg == WM_SB_TCP || msg == WM_SB_UDP)
            {
                self->OnSocket((uintptr_t)wp, (long)WSAGETSELECTEVENT(lp), (int)WSAGETSELECTERROR(lp));
                return 0;
            }
            if (msg == WM_SB_CLOSE)
            {
                self->Drop(-(int)wp);   // negativ = jetzt wirklich schliessen
                return 0;
            }
            if (msg == WM_TIMER && wp == kTickTimer)
            {
                self->OnTimer();
                return 0;
            }
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    std::string AddrOf(const sockaddr_in& a)
    {
        char buf[64] = {};
        inet_ntop(AF_INET, (void*)&a.sin_addr, buf, sizeof(buf));
        return buf;
    }
}

RemoteServerNet::~RemoteServerNet()
{
    Stop();
}

bool RemoteServerNet::Start(int port, remote::Hub* hub, std::string& err)
{
    Stop();
    m_hub = hub;
    m_port = port;
    WSADATA wd;
    if (WSAStartup(MAKEWORD(2, 2), &wd) != 0) { err = "Winsock not available"; return false; }
    m_wsa = true;

    HINSTANCE inst = (HINSTANCE)&__ImageBase;
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = NetProc;
    wc.hInstance = inst;
    wc.lpszClassName = kNetClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) { err = "window class"; Stop(); return false; }
    HWND wnd = CreateWindowExW(0, kNetClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, inst, this);
    if (!wnd) { err = "message window"; Stop(); return false; }
    m_wnd = wnd;

    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ls == INVALID_SOCKET) { err = "socket"; Stop(); return false; }
    BOOL excl = TRUE;
    setsockopt(ls, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&excl, sizeof(excl));
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons((u_short)port);
    if (bind(ls, (sockaddr*)&a, sizeof(a)) != 0)
    {
        err = "port " + std::to_string(port) + " is in use";
        closesocket(ls);
        Stop();
        return false;
    }
    if (listen(ls, 4) != 0) { err = "listen"; closesocket(ls); Stop(); return false; }
    WSAAsyncSelect(ls, wnd, WM_SB_TCP, FD_ACCEPT);
    m_listen = (uintptr_t)ls;

    // Suche im Netz: StripBay Remote ruft "STRIPBAY?" als Broadcast, wir antworten mit Port und Namen
    SOCKET us = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (us != INVALID_SOCKET)
    {
        sockaddr_in ua = {};
        ua.sin_family = AF_INET;
        ua.sin_addr.s_addr = htonl(INADDR_ANY);
        ua.sin_port = htons((u_short)remote::kDiscoveryPort);
        if (bind(us, (sockaddr*)&ua, sizeof(ua)) == 0)
        {
            WSAAsyncSelect(us, wnd, WM_SB_UDP, FD_READ);
            m_udp = (uintptr_t)us;
        }
        else closesocket(us);
    }

    SetTimer(wnd, kTickTimer, 2000, nullptr);
    m_running = true;
    return true;
}

void RemoteServerNet::Stop()
{
    std::vector<int> ids;
    for (auto& kv : m_conns) ids.push_back(kv.first);
    for (int id : ids) CloseConn(id, true);
    if (m_listen != ~(uintptr_t)0) { closesocket((SOCKET)m_listen); m_listen = ~(uintptr_t)0; }
    if (m_udp != ~(uintptr_t)0) { closesocket((SOCKET)m_udp); m_udp = ~(uintptr_t)0; }
    if (m_wnd)
    {
        KillTimer((HWND)m_wnd, kTickTimer);
        DestroyWindow((HWND)m_wnd);
        m_wnd = nullptr;
        UnregisterClassW(kNetClass, (HINSTANCE)&__ImageBase);
    }
    if (m_wsa) { WSACleanup(); m_wsa = false; }
    m_running = false;
}

void RemoteServerNet::OnSocket(uintptr_t sock, long event, int error)
{
    if (sock == m_udp)
    {
        char buf[256];
        sockaddr_in from = {};
        int fl = sizeof(from);
        int n = recvfrom((SOCKET)sock, buf, sizeof(buf) - 1, 0, (sockaddr*)&from, &fl);
        if (n > 0)
        {
            buf[n] = 0;
            if (strncmp(buf, "STRIPBAY?", 9) == 0)
            {
                std::string rep = "STRIPBAY1 " + std::to_string(m_port) + " " + (m_hub ? m_hub->name : ComputerName());
                sendto((SOCKET)sock, rep.c_str(), (int)rep.size(), 0, (sockaddr*)&from, fl);
            }
        }
        return;
    }
    if (sock == m_listen)
    {
        if (event != FD_ACCEPT || error) return;
        sockaddr_in from = {};
        int fl = sizeof(from);
        SOCKET c = accept((SOCKET)m_listen, (sockaddr*)&from, &fl);
        if (c == INVALID_SOCKET) return;
        BOOL nd = TRUE;
        setsockopt(c, IPPROTO_TCP, TCP_NODELAY, (const char*)&nd, sizeof(nd));
        WSAAsyncSelect(c, (HWND)m_wnd, WM_SB_TCP, FD_READ | FD_WRITE | FD_CLOSE);
        int id = m_nextId++;
        Conn& cn = m_conns[id];
        cn.sock = (uintptr_t)c;
        cn.addr = AddrOf(from);
        m_bySock[(uintptr_t)c] = id;
        if (m_hub) m_hub->Connected(id, cn.addr, NowMs());
        return;
    }
    auto bs = m_bySock.find(sock);
    if (bs == m_bySock.end()) return;
    int id = bs->second;
    if (event == FD_CLOSE || error) { CloseConn(id, true); return; }
    if (event == FD_WRITE) { Flush(id); return; }
    if (event == FD_READ)
    {
        char buf[16384];
        for (;;)
        {
            auto it = m_conns.find(id);
            if (it == m_conns.end()) return;
            int n = recv((SOCKET)it->second.sock, buf, sizeof(buf), 0);
            if (n > 0)
            {
                it->second.in.Feed(buf, (size_t)n);
                continue;
            }
            if (n == 0 || WSAGetLastError() != WSAEWOULDBLOCK) { CloseConn(id, true); return; }
            break;
        }
        std::string payload;
        for (;;)
        {
            auto it = m_conns.find(id);
            if (it == m_conns.end()) return;
            if (it->second.in.Bad()) { CloseConn(id, true); return; }
            if (!it->second.in.Next(payload)) break;
            if (m_hub) m_hub->Received(id, payload, NowMs());
        }
    }
}

void RemoteServerNet::OnTimer()
{
    if (m_hub) m_hub->Tick(NowMs());
}

void RemoteServerNet::Send(int client, const std::string& frame)
{
    auto it = m_conns.find(client);
    if (it == m_conns.end()) return;
    if (it->second.out.size() > (64u << 20)) { Drop(client); return; }   // Geraet liest nicht mehr
    it->second.out += frame;
    Flush(client);
}

void RemoteServerNet::Flush(int client)
{
    auto it = m_conns.find(client);
    if (it == m_conns.end()) return;
    Conn& c = it->second;
    while (!c.out.empty())
    {
        int n = send((SOCKET)c.sock, c.out.data(), (int)std::min<size_t>(c.out.size(), 1 << 20), 0);
        if (n > 0) { c.out.erase(0, (size_t)n); continue; }
        if (WSAGetLastError() == WSAEWOULDBLOCK) break;
        // Fehler: spaeter schliessen (wir koennen gerade mitten in einer Schleife des Hubs sein)
        c.out.clear();
        if (m_wnd) PostMessageW((HWND)m_wnd, WM_SB_CLOSE, (WPARAM)client, 0);
        break;
    }
}

void RemoteServerNet::Drop(int client)
{
    if (client < 0) { CloseConn(-client, true); return; }
    // nicht sofort: der Hub ruft das aus seinen eigenen Schleifen auf
    if (m_wnd) PostMessageW((HWND)m_wnd, WM_SB_CLOSE, (WPARAM)client, 0);
    else CloseConn(client, true);
}

void RemoteServerNet::CloseConn(int client, bool notify)
{
    auto it = m_conns.find(client);
    if (it == m_conns.end()) return;
    SOCKET s = (SOCKET)it->second.sock;
    if (!it->second.out.empty()) send(s, it->second.out.data(), (int)it->second.out.size(), 0);   // letzter Versuch (z. B. Ablehnung)
    shutdown(s, SD_BOTH);
    closesocket(s);
    m_bySock.erase(it->second.sock);
    m_conns.erase(it);
    if (notify && m_hub) m_hub->Closed(client);
}

std::vector<std::string> RemoteServerNet::LocalAddresses()
{
    std::vector<std::string> v;
    // Heimnetz zuerst (Adapter mit Gateway), virtuelle Adapter (Hyper-V, WSL, VPN) nur, wenn sonst nichts da ist
    std::vector<netif::Interface> ifs = netif::List();
    for (const netif::Interface& f : ifs)
        if ((f.gateway || !f.virt) && std::find(v.begin(), v.end(), f.addr) == v.end()) v.push_back(f.addr);
    if (v.empty())
        for (const netif::Interface& f : ifs)
            if (std::find(v.begin(), v.end(), f.addr) == v.end()) v.push_back(f.addr);
    if (!v.empty()) return v;

    char host[256] = {};
    if (gethostname(host, sizeof(host)) != 0) return v;
    addrinfo hints = {};
    hints.ai_family = AF_INET;
    addrinfo* res = nullptr;
    if (getaddrinfo(host, nullptr, &hints, &res) != 0) return v;
    for (addrinfo* p = res; p; p = p->ai_next)
    {
        std::string a = AddrOf(*reinterpret_cast<sockaddr_in*>(p->ai_addr));
        if (a.compare(0, 4, "127.") == 0 || a.compare(0, 8, "169.254.") == 0) continue;
        if (std::find(v.begin(), v.end(), a) == v.end()) v.push_back(a);
    }
    freeaddrinfo(res);
    return v;
}

std::string RemoteServerNet::ComputerName()
{
    wchar_t w[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD n = MAX_COMPUTERNAME_LENGTH + 1;
    if (!GetComputerNameW(w, &n)) return "PC";
    std::string s;
    for (DWORD i = 0; i < n; ++i) s += (w[i] < 128 && w[i] > 32) ? (char)w[i] : '-';
    return s.empty() ? std::string("PC") : s;
}
