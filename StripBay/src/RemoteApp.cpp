// StripBay Remote - die Streifen auf einem weiteren Windows-Geraet im selben Netz
//
// Das Programm zeigt dasselbe Fenster wie das Plugin (StripWindow, derselbe Code) und verbindet sich mit
// StripBay in EuroScope (".strips remote on"). Jede Aktion geht an das Plugin und laeuft dort durch denselben
// Code wie ein Tipp im Fenster am PC - der Abgleich mit den anderen Lotsen bleibt damit vollstaendig.
// Nur Zoom, Layout, Hell/Dunkel, Vollbild, "immer oben" und die Fensterposition gelten je Geraet.
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
#include <commctrl.h>
#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
// zeitgemaesse Schaltflaechen im Verbinden-Fenster
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

#include "NetIf.h"
#include "StripWindow.h"
#include "RemoteProto.h"
#include "Tone.h"
#include "Version.h"

#include <algorithm>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace
{
    const UINT WM_RA_SOCK = WM_APP + 0x6A1;
    const UINT WM_RA_DRAIN = WM_APP + 0x6A2;
    const UINT WM_RA_UDP = WM_APP + 0x6A3;
    const UINT_PTR T_PING = 11, T_RECONNECT = 12, T_CONNECT_TIMEOUT = 13, T_UDP_DONE = 14;
    const int ID_HOST = 101, ID_CODE = 102, ID_SEARCH = 103, ID_CONNECT = 104, ID_STATUS = 105, ID_QUIT = 106;

    long long NowMs() { return (long long)GetTickCount64(); }

    std::wstring W(const std::string& s)
    {
        if (s.empty()) return std::wstring();
        int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
        std::wstring w(n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
        return w;
    }

    std::string U(const std::wstring& w)
    {
        if (w.empty()) return std::string();
        int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string s(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
        return s;
    }

    std::string Lower(std::string s)
    {
        for (char& c : s) c = (char)tolower((unsigned char)c);
        return s;
    }

    std::string Trim(const std::string& s)
    {
        size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
        return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
    }

    std::vector<std::string> Tokens(const std::string& s)
    {
        std::vector<std::string> t;
        std::string cur;
        for (char c : s)
        {
            if (c == ' ' || c == '\t') { if (!cur.empty()) t.push_back(cur); cur.clear(); }
            else cur += c;
        }
        if (!cur.empty()) t.push_back(cur);
        return t;
    }

    std::string ComputerName()
    {
        wchar_t w[MAX_COMPUTERNAME_LENGTH + 1] = {};
        DWORD n = MAX_COMPUTERNAME_LENGTH + 1;
        if (!GetComputerNameW(w, &n)) return "Remote";
        return U(std::wstring(w, n));
    }

    // "STRIPBAY?" auf jedem Netzwerkadapter rufen (Windows schickt 255.255.255.255 nur ueber einen Adapter)
    void SendDiscovery(SOCKET s)
    {
        BOOL on = TRUE;
        setsockopt(s, SOL_SOCKET, SO_BROADCAST, (const char*)&on, sizeof(on));
        std::vector<std::string> targets = { "255.255.255.255" };
        for (const netif::Interface& f : netif::List())
            if (std::find(targets.begin(), targets.end(), f.bcast) == targets.end()) targets.push_back(f.bcast);
        const char q[] = "STRIPBAY?";
        for (const std::string& t : targets)
        {
            sockaddr_in to = {};
            to.sin_family = AF_INET;
            to.sin_port = htons((u_short)remote::kDiscoveryPort);
            if (inet_pton(AF_INET, t.c_str(), &to.sin_addr) != 1) continue;
            sendto(s, q, (int)sizeof(q) - 1, 0, (sockaddr*)&to, sizeof(to));
        }
    }

    // Antwort "STRIPBAY1 <port> <name>" -> port, name
    bool ParseDiscovery(const char* buf, int& port, std::string& name)
    {
        std::vector<std::string> t = Tokens(buf);
        if (t.size() < 2 || t[0] != "STRIPBAY1") return false;
        port = atoi(t[1].c_str());
        if (port <= 0 || port > 65535) return false;
        name.clear();
        for (size_t i = 2; i < t.size(); ++i) name += (name.empty() ? "" : " ") + t[i];
        return true;
    }

    std::string OwnAddress()
    {
        std::vector<netif::Interface> v = netif::List();
        return v.empty() ? std::string("?") : v[0].addr;
    }
}

class RemoteApp
{
public:
    int Run(const std::wstring& cmdLine);

    LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT DlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    HWND Dialog() const { return m_dlg; }

private:
    remote::Link m_link;
    StripWindow  m_win{ &m_link };
    bool         m_winCreated = false;
    HWND         m_msg = nullptr;     // Nachrichtenfenster fuer Socket und Zeitgeber
    HWND         m_dlg = nullptr;     // Verbinden-Fenster
    HFONT        m_dlgFont = nullptr;
    SOCKET       m_sock = INVALID_SOCKET;
    remote::FrameReader m_in;
    std::string  m_out;
    std::deque<std::string> m_pending;   // beim Warten auf eine Antwort angekommen
    enum State { S_IDLE, S_CONNECTING, S_HELLO, S_ONLINE } m_state = S_IDLE;
    bool         m_auto = false;        // nach einer Trennung selbst neu verbinden
    long long    m_lastRx = 0;
    std::string  m_host, m_code, m_serverName, m_serverVersion;
    int          m_port = remote::kDefaultPort;
    Board        m_board;
    bool         m_haveBoard = false;
    int          m_mirrorView = -1, m_mirrorFilter = -1;
    int          m_pendView = -1, m_pendFilter = -1;
    long long    m_pendUntil = 0;
    std::wstring m_ini;
    WindowSettings m_ws;
    SOCKET       m_udp = INVALID_SOCKET;   // Wiederfinden: PC hat eine neue IP-Adresse bekommen
    int          m_fails = 0;              // Verbindungsversuche ohne Erfolg in Folge

    void LoadIni(WindowSettings& ws);
    void SaveIni();
    void ShowDialog(const std::wstring& status);
    void SetStatus(const std::wstring& status);
    void Search();
    bool StartConnect(const std::string& hostPort, const std::string& code);
    void OnConnected(int error);
    void OnSocket(long event, int error);
    void Disconnected(const std::wstring& why, bool keepAuto);
    void CloseSocket();
    void SendFrame(const std::string& frame);
    void Flush();
    void ReadAll();
    void Drain();
    void Handle(const std::string& payload);
    bool WaitReply(uint32_t id, std::string& reply);
    bool LocalCommand(const std::string& line);
    void WindowState();
    void EnsureWindow();
    void Fail(const std::wstring& why);
    std::wstring NoAnswer() const;
    std::wstring NetHint() const;
    void Rediscover();
    void OnUdp();
    void CloseUdp();
};

static RemoteApp* g_app = nullptr;

static LRESULT CALLBACK RaMsgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    return g_app ? g_app->MsgProc(hwnd, msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

static LRESULT CALLBACK RaDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    return g_app ? g_app->DlgProc(hwnd, msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// Einstellungen: %APPDATA%\StripBay\StripBayRemote.ini
// ---------------------------------------------------------------------------
void RemoteApp::LoadIni(WindowSettings& ws)
{
    wchar_t ad[MAX_PATH] = {};
    GetEnvironmentVariableW(L"APPDATA", ad, MAX_PATH);
    std::wstring dir = std::wstring(ad) + L"\\StripBay";
    CreateDirectoryW(dir.c_str(), nullptr);
    m_ini = dir + L"\\StripBayRemote.ini";
    auto gs = [&](const wchar_t* sec, const wchar_t* key, const wchar_t* def) {
        wchar_t b[512] = {};
        GetPrivateProfileStringW(sec, key, def, b, 512, m_ini.c_str());
        return std::wstring(b);
    };
    auto gi = [&](const wchar_t* sec, const wchar_t* key, int def) { return (int)GetPrivateProfileIntW(sec, key, def, m_ini.c_str()); };
    m_host = U(gs(L"Connection", L"Host", L""));
    m_code = U(gs(L"Connection", L"Code", L""));
    m_serverName = U(gs(L"Connection", L"Name", L""));
    m_port = gi(L"Connection", L"Port", remote::kDefaultPort);
    ws.normalRect = { gi(L"Window", L"Left", 100), gi(L"Window", L"Top", 100), gi(L"Window", L"Right", 1500), gi(L"Window", L"Bottom", 860) };
    ws.zoom = gi(L"Window", L"Zoom", 100) / 100.0;
    ws.layout = gi(L"Window", L"Layout", 0);
    ws.light = gi(L"Window", L"Light", 1) != 0;
    ws.fullscreen = gi(L"Window", L"Fullscreen", 0) != 0;
    ws.topmost = gi(L"Window", L"Topmost", 0) != 0;
    ws.view = gi(L"Window", L"View", 0);
    ws.rwyFilter = gi(L"Window", L"Filter", 0);
    ws.visible = true;
}

void RemoteApp::SaveIni()
{
    if (m_ini.empty()) return;
    auto ps = [&](const wchar_t* sec, const wchar_t* key, const std::wstring& v) { WritePrivateProfileStringW(sec, key, v.c_str(), m_ini.c_str()); };
    auto pi = [&](const wchar_t* sec, const wchar_t* key, int v) { ps(sec, key, std::to_wstring(v)); };
    ps(L"Connection", L"Host", W(m_host));
    ps(L"Connection", L"Code", W(m_code));
    ps(L"Connection", L"Name", W(m_serverName));
    pi(L"Connection", L"Port", m_port);
    if (m_winCreated)
    {
        WindowSettings s = m_win.GetSettings();
        pi(L"Window", L"Left", s.normalRect.left);
        pi(L"Window", L"Top", s.normalRect.top);
        pi(L"Window", L"Right", s.normalRect.right);
        pi(L"Window", L"Bottom", s.normalRect.bottom);
        pi(L"Window", L"Zoom", (int)(s.zoom * 100.0 + 0.5));
        pi(L"Window", L"Layout", s.layout);
        pi(L"Window", L"Light", s.light ? 1 : 0);
        pi(L"Window", L"Fullscreen", s.fullscreen ? 1 : 0);
        pi(L"Window", L"Topmost", s.topmost ? 1 : 0);
        pi(L"Window", L"View", s.view);
        pi(L"Window", L"Filter", s.rwyFilter);
    }
}

// ---------------------------------------------------------------------------
// Verbinden-Fenster (erster Start, falscher Code, "Disconnect")
// ---------------------------------------------------------------------------
void RemoteApp::ShowDialog(const std::wstring& status)
{
    if (!m_dlg)
    {
        HINSTANCE inst = (HINSTANCE)&__ImageBase;
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = RaDlgProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"StripBayRemoteConnect";
        RegisterClassExW(&wc);
        HDC sdc = GetDC(nullptr);
        int dpi = GetDeviceCaps(sdc, LOGPIXELSY);
        ReleaseDC(nullptr, sdc);
        auto S = [&](int v) { return MulDiv(v, dpi, 96); };
        int w = S(500), h = S(350);
        RECT wa;
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        m_dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, wc.lpszClassName, L"StripBay Remote " STRIPBAY_VERSION_W,
                                WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, (wa.left + wa.right - w) / 2, (wa.top + wa.bottom - h) / 2, w, h,
                                nullptr, nullptr, inst, nullptr);
        m_dlgFont = CreateFontW(-S(15), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        auto mk = [&](const wchar_t* cls, const wchar_t* txt, DWORD st, int x, int y, int cw, int ch, int id) {
            HWND c = CreateWindowExW(0, cls, txt, WS_CHILD | WS_VISIBLE | st, S(x), S(y), S(cw), S(ch), m_dlg, (HMENU)(INT_PTR)id, inst, nullptr);
            SendMessageW(c, WM_SETFONT, (WPARAM)m_dlgFont, TRUE);
            return c;
        };
        mk(L"STATIC", L"PC with EuroScope (name or IP address):", 0, 20, 16, 440, 22, 0);
        HWND hc = mk(L"COMBOBOX", L"", CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_VSCROLL | WS_TABSTOP, 20, 40, 330, 200, ID_HOST);
        mk(L"BUTTON", L"Search", BS_PUSHBUTTON | WS_TABSTOP, 360, 39, 110, 30, ID_SEARCH);
        mk(L"STATIC", L"Code (EuroScope: .strips remote on, or Settings):", 0, 20, 84, 440, 22, 0);
        HWND ec = mk(L"EDIT", L"", WS_BORDER | ES_NUMBER | WS_TABSTOP, 20, 108, 150, 28, ID_CODE);
        SendMessageW(ec, EM_SETLIMITTEXT, 6, 0);
        mk(L"BUTTON", L"Connect", BS_DEFPUSHBUTTON | WS_TABSTOP, 20, 156, 150, 34, ID_CONNECT);
        mk(L"BUTTON", L"Quit", BS_PUSHBUTTON | WS_TABSTOP, 180, 156, 110, 34, ID_QUIT);
        mk(L"STATIC", L"", 0, 20, 204, 455, 96, ID_STATUS);
        std::string hp = m_host;
        if (!hp.empty() && m_port != remote::kDefaultPort) hp += ":" + std::to_string(m_port);
        SetWindowTextW(hc, W(hp).c_str());
        SetWindowTextW(ec, W(m_code).c_str());
    }
    SetStatus(status);
    ShowWindow(m_dlg, SW_SHOW);
    SetForegroundWindow(m_dlg);
    SetFocus(GetDlgItem(m_dlg, m_host.empty() ? ID_HOST : ID_CODE));
}

void RemoteApp::SetStatus(const std::wstring& status)
{
    if (m_dlg) SetWindowTextW(GetDlgItem(m_dlg, ID_STATUS), status.c_str());
}

// StripBay in EuroScope im lokalen Netz suchen (UDP-Broadcast, Antwort mit Port und Namen)
void RemoteApp::Search()
{
    SetStatus(L"Searching\x2026");
    UpdateWindow(m_dlg);
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) { SetStatus(L"Search not possible (network)."); return; }
    SendDiscovery(s);
    std::vector<std::wstring> found;
    long long until = NowMs() + 1500;
    for (;;)
    {
        long long left = until - NowMs();
        if (left <= 0) break;
        fd_set rs;
        FD_ZERO(&rs);
        FD_SET(s, &rs);
        timeval tv = { (long)(left / 1000), (long)((left % 1000) * 1000) };
        if (select(0, &rs, nullptr, nullptr, &tv) <= 0) break;
        char buf[512];
        sockaddr_in from = {};
        int fl = sizeof(from);
        int n = recvfrom(s, buf, sizeof(buf) - 1, 0, (sockaddr*)&from, &fl);
        if (n <= 0) continue;
        buf[n] = 0;
        int port = 0;
        std::string name;
        if (!ParseDiscovery(buf, port, name)) continue;
        char ip[64] = {};
        inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
        std::string hp = ip;
        if (port != remote::kDefaultPort) hp += ":" + std::to_string(port);
        std::wstring item = W(hp + "   " + name);
        if (std::find(found.begin(), found.end(), item) == found.end()) found.push_back(item);
    }
    closesocket(s);
    HWND hc = GetDlgItem(m_dlg, ID_HOST);
    SendMessageW(hc, CB_RESETCONTENT, 0, 0);
    for (const std::wstring& f : found) SendMessageW(hc, CB_ADDSTRING, 0, (LPARAM)f.c_str());
    if (!found.empty())
    {
        SendMessageW(hc, CB_SETCURSEL, 0, 0);
        SetStatus(found.size() == 1 ? L"Found. Enter the code and connect." : L"Several found - pick one, enter the code and connect.");
        SetFocus(GetDlgItem(m_dlg, ID_CODE));
    }
    else
        SetStatus(L"Nothing found (this device: " + W(OwnAddress()) + L"). In EuroScope: .strips remote on. "
                  L"Otherwise enter the IP address EuroScope shows (.strips remote status). Still nothing? Run "
                  L"REMOTE-FIREWALL.bat once on the EuroScope PC (Windows firewall).");
}

LRESULT RemoteApp::DlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_COMMAND:
        if (LOWORD(wp) == ID_SEARCH) { Search(); return 0; }
        if (LOWORD(wp) == ID_QUIT) { PostQuitMessage(0); return 0; }
        if (LOWORD(wp) == ID_CONNECT || LOWORD(wp) == IDOK)
        {
            wchar_t b[256] = {}, c[16] = {};
            GetWindowTextW(GetDlgItem(hwnd, ID_HOST), b, 256);
            GetWindowTextW(GetDlgItem(hwnd, ID_CODE), c, 16);
            std::vector<std::string> t = Tokens(U(b));   // "192.168.1.20   PC-NAME" -> erste Angabe
            std::string hp = t.empty() ? std::string() : t[0];
            std::string code = Trim(U(c));
            if (hp.empty()) { SetStatus(L"Please enter the PC name or IP address (or Search)."); return 0; }
            if (code.size() != 6) { SetStatus(L"The code has 6 digits (EuroScope: .strips remote status)."); return 0; }
            StartConnect(hp, code);
            return 0;
        }
        break;
    case WM_CLOSE:
        if (m_state == S_ONLINE || m_auto) { ShowWindow(hwnd, SW_HIDE); return 0; }
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// Verbindung
// ---------------------------------------------------------------------------
bool RemoteApp::StartConnect(const std::string& hostPort, const std::string& code)
{
    CloseSocket();
    std::string host = hostPort;
    int port = remote::kDefaultPort;
    size_t colon = host.rfind(':');
    if (colon != std::string::npos)
    {
        port = atoi(host.c_str() + colon + 1);
        host = host.substr(0, colon);
        if (port <= 0 || port > 65535) port = remote::kDefaultPort;
    }
    m_host = host;
    m_port = port;
    m_code = code;
    addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res)
    {
        Fail(L"PC \"" + W(host) + L"\" not found in the network. Enter its IP address instead (EuroScope: .strips remote status).");
        return false;
    }
    m_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_sock == INVALID_SOCKET) { freeaddrinfo(res); return false; }
    BOOL nd = TRUE;
    setsockopt(m_sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&nd, sizeof(nd));
    WSAAsyncSelect(m_sock, m_msg, WM_RA_SOCK, FD_CONNECT | FD_READ | FD_WRITE | FD_CLOSE);
    int rc = connect(m_sock, res->ai_addr, (int)res->ai_addrlen);
    freeaddrinfo(res);
    if (rc != 0 && WSAGetLastError() != WSAEWOULDBLOCK)
    {
        CloseSocket();
        Fail(L"Connection to " + W(host) + L" not possible from this device (network)." + NetHint());
        return false;
    }
    m_state = S_CONNECTING;
    SetTimer(m_msg, T_CONNECT_TIMEOUT, 5000, nullptr);
    if (m_dlg && IsWindowVisible(m_dlg)) SetStatus(L"Connecting to " + W(host) + L"\x2026");
    return true;
}

void RemoteApp::OnConnected(int error)
{
    KillTimer(m_msg, T_CONNECT_TIMEOUT);
    if (error)
    {
        CloseSocket();
        m_state = S_IDLE;
        if (error == WSAECONNREFUSED)
            Fail(W(m_host) + L" answers, but StripBay Remote is not on there (port " + std::to_wstring(m_port) +
                 L"). In EuroScope: .strips remote on");
        else if (error == WSAENETUNREACH || error == WSAEHOSTUNREACH)
            Fail(L"No route to " + W(m_host) + L"." + NetHint());
        else
            Fail(NoAnswer());
        return;
    }
    m_state = S_HELLO;
    m_lastRx = NowMs();
    SetTimer(m_msg, T_CONNECT_TIMEOUT, 5000, nullptr);   // auf die Antwort warten
    SendFrame(remote::Frame(remote::Hello(m_code, ComputerName(), STRIPBAY_VERSION)));
}

void RemoteApp::CloseSocket()
{
    KillTimer(m_msg, T_CONNECT_TIMEOUT);
    if (m_sock != INVALID_SOCKET)
    {
        shutdown(m_sock, SD_BOTH);
        closesocket(m_sock);
        m_sock = INVALID_SOCKET;
    }
    m_in.Clear();
    m_out.clear();
    m_link.online = false;
    m_state = S_IDLE;
}

void RemoteApp::Disconnected(const std::wstring& why, bool keepAuto)
{
    bool wasOnline = m_state == S_ONLINE;
    CloseSocket();
    m_auto = keepAuto;
    if (m_haveBoard)
    {
        m_board.connected = false;
        m_win.SetBoard(m_board);
    }
    if (wasOnline && m_winCreated && !why.empty()) m_win.ShowNote(why);
    if (wasOnline && m_winCreated)
        m_win.SetOfflineDetail(L"Connection to " + W(m_serverName.empty() ? m_host : m_serverName) +
                               L" lost (EuroScope closed, PC asleep or network changed).");
    if (!wasOnline && m_dlg && IsWindowVisible(m_dlg)) SetStatus(L"EuroScope closed the connection.");
    if (m_auto) SetTimer(m_msg, T_RECONNECT, 3000, nullptr);
}

void RemoteApp::SendFrame(const std::string& frame)
{
    if (m_sock == INVALID_SOCKET) return;
    m_out += frame;
    Flush();
}

void RemoteApp::Flush()
{
    while (!m_out.empty() && m_sock != INVALID_SOCKET)
    {
        int n = send(m_sock, m_out.data(), (int)std::min<size_t>(m_out.size(), 1 << 20), 0);
        if (n > 0) { m_out.erase(0, (size_t)n); continue; }
        if (WSAGetLastError() == WSAEWOULDBLOCK) break;
        PostMessageW(m_msg, WM_RA_SOCK, (WPARAM)m_sock, WSAMAKESELECTREPLY(FD_CLOSE, WSAECONNRESET));
        break;
    }
}

void RemoteApp::ReadAll()
{
    char buf[65536];
    bool closed = false;
    for (;;)
    {
        if (m_sock == INVALID_SOCKET) return;
        int n = recv(m_sock, buf, sizeof(buf), 0);
        if (n > 0) { m_in.Feed(buf, (size_t)n); m_lastRx = NowMs(); continue; }
        if (n == 0 || WSAGetLastError() != WSAEWOULDBLOCK) closed = true;
        break;
    }
    // erst alles auswerten, was angekommen ist (z. B. die Ablehnung mit Grund), dann die Trennung
    std::string p;
    while (m_in.Next(p)) m_pending.push_back(p);
    bool bad = m_in.Bad();
    Drain();
    if (m_sock == INVALID_SOCKET) return;
    if (bad) { Disconnected(L"Connection error - reconnecting\x2026", true); return; }
    if (closed) Disconnected(L"Connection to EuroScope lost - reconnecting\x2026", m_auto || m_state == S_ONLINE);
}

void RemoteApp::Drain()
{
    while (!m_pending.empty())
    {
        std::string p = std::move(m_pending.front());
        m_pending.pop_front();
        Handle(p);
    }
}

// Auswahlliste / Stands: kurz auf die Antwort warten (im LAN wenige Millisekunden)
bool RemoteApp::WaitReply(uint32_t id, std::string& reply)
{
    long long until = NowMs() + 2000;
    for (;;)
    {
        for (auto it = m_pending.begin(); it != m_pending.end(); ++it)
        {
            wire::Reader r(*it);
            if (r.u8() == remote::M_REPLY && r.u32() == id)
            {
                reply = *it;
                m_pending.erase(it);
                PostMessageW(m_msg, WM_RA_DRAIN, 0, 0);
                return true;
            }
        }
        if (m_sock == INVALID_SOCKET) return false;
        long long left = until - NowMs();
        if (left <= 0) return false;
        Flush();
        fd_set rs;
        FD_ZERO(&rs);
        FD_SET(m_sock, &rs);
        timeval tv = { (long)(left / 1000), (long)((left % 1000) * 1000) };
        if (select(0, &rs, nullptr, nullptr, &tv) <= 0) return false;
        char buf[65536];
        int n = recv(m_sock, buf, sizeof(buf), 0);
        if (n <= 0) return false;
        m_in.Feed(buf, (size_t)n);
        m_lastRx = NowMs();
        std::string p;
        while (m_in.Next(p)) m_pending.push_back(p);
    }
}

void RemoteApp::Handle(const std::string& payload)
{
    if (payload.empty()) return;
    uint8_t t = (uint8_t)payload[0];
    if (m_state == S_HELLO)
    {
        std::string reason;
        if (remote::ParseWelcome(payload, m_serverName, m_serverVersion, reason))
        {
            m_state = S_ONLINE;
            m_link.online = true;
            m_auto = true;
            m_fails = 0;
            CloseUdp();
            KillTimer(m_msg, T_RECONNECT);
            SaveIni();
            EnsureWindow();
            m_win.SetRemoteClient(L"Connected to " + W(m_serverName) + L" (" + W(m_host) + L", StripBay " + W(m_serverVersion) + L")");
            m_win.SetTitle(L"StripBay Remote - " + W(m_serverName));
            m_win.ShowNote(L"Connected to " + W(m_serverName) + L".", 3000);
            m_win.SetOfflineDetail(std::wstring());
            if (m_dlg) ShowWindow(m_dlg, SW_HIDE);
            return;
        }
        CloseSocket();
        m_auto = false;
        KillTimer(m_msg, T_RECONNECT);
        if (reason == "code")
            ShowDialog(L"Wrong code. In EuroScope: .strips remote status shows the code.");
        else
            ShowDialog(L"Versions do not match: StripBay in EuroScope is " + W(reason) + L", this is " STRIPBAY_VERSION_W
                       L". Install both from the same package.");
        return;
    }
    if (m_state != S_ONLINE) return;
    if (t == remote::M_BOARD)
    {
        Board b;
        if (!wire::DecodeBoard(payload, 1, b)) return;
        b.connected = true;
        // Ansicht / Bereich: gelten fuer die ganze Sitzung (wer du bist). Eben selbst gewechselt? Dann
        // die eigene Wahl behalten, bis das Plugin sie bestaetigt.
        bool pend = m_pendUntil > NowMs();
        if (pend && (b.view != m_pendView || b.rwyFilter != m_pendFilter)) { }
        else
        {
            m_pendUntil = 0;
            if (b.view != m_win.GetView()) m_win.SetView(b.view);
            if (b.rwyFilter != m_win.GetRwyFilter()) m_win.SetRwyFilter(b.rwyFilter);
            m_mirrorView = b.view;
            m_mirrorFilter = b.rwyFilter;
        }
        m_board = b;
        m_haveBoard = true;
        m_win.SetBoard(b);
        return;
    }
    wire::Reader r(payload);
    r.u8();
    if (t == remote::M_SOUND)
    {
        std::string tone;
        r(tone);
        PlayTone(tone);
    }
    else if (t == remote::M_NOTE)
    {
        std::string text;
        r(text);
        m_win.ShowNote(W(text));
    }
}

// Befehle, die nur dieses Geraet betreffen - wie im Plugin, aber fuer dieses Fenster
bool RemoteApp::LocalCommand(const std::string& line)
{
    std::vector<std::string> t = Tokens(line);
    if (t.size() < 2) return false;
    std::string c0 = Lower(t[0]);
    if (c0 != ".strips" && c0 != ".sb") return false;
    std::string sub = Lower(t[1]), arg = t.size() > 2 ? Lower(t[2]) : std::string();
    if (sub == "zoom")
    {
        int pct = atoi(arg.c_str());
        if (pct >= 60 && pct <= 250) m_win.SetZoom(pct / 100.0);
    }
    else if (sub == "hell" || sub == "light") m_win.SetLight(true);
    else if (sub == "dunkel" || sub == "dark") m_win.SetLight(false);
    else if (sub == "theme") m_win.SetLight(arg == "hell" || arg == "light");
    else if (sub == "layout")
    {
        int l = arg == "auto" ? 0 : (arg == "spalten" || arg == "cols" || arg == "columns") ? 1 : (arg == "raster" || arg == "grid") ? 2 : -1;
        if (l >= 0) m_win.SetLayout(l);
    }
    else if (sub == "full" || sub == "fullscreen")
    {
        bool cur = m_win.GetSettings().fullscreen;
        if (!((arg == "on" && cur) || (arg == "off" && !cur))) m_win.ToggleFullscreen();
    }
    else if (sub == "top" || sub == "ontop")
    {
        bool on = arg == "on" ? true : arg == "off" ? false : !m_win.GetSettings().topmost;
        m_win.SetTopmost(on);
    }
    else if (sub == "show") m_win.Show(true);
    else if (sub == "hide") { m_win.Show(false); PostQuitMessage(0); }
    else if (sub == "settings" || sub == "einstellungen" || sub == "options") m_win.OpenSettings();
    else if (sub == "remote" && (arg == "disconnect" || arg == "off" || arg == "trennen"))
    {
        m_auto = false;
        KillTimer(m_msg, T_RECONNECT);
        Disconnected(std::wstring(), false);
        ShowDialog(L"Disconnected. Connect again or to another PC.");
    }
    else return false;
    SaveIni();
    return true;
}

// Fenster hat etwas geaendert (Zoom, Ansicht, geschlossen ...)
void RemoteApp::WindowState()
{
    SaveIni();
    if (m_winCreated && !m_win.IsVisible()) { PostQuitMessage(0); return; }
    int v = m_win.GetView(), f = m_win.GetRwyFilter();
    if (m_state == S_ONLINE && (v != m_mirrorView || f != m_mirrorFilter))
    {
        m_link.SendViewFilter(v, f);
        m_pendView = v;
        m_pendFilter = f;
        m_pendUntil = NowMs() + 2500;
        m_mirrorView = v;
        m_mirrorFilter = f;
    }
}

// Verbindung klappt nicht: Grund im Verbinden-Fenster und auf der Karte "NOT CONNECTED"
void RemoteApp::Fail(const std::wstring& why)
{
    ++m_fails;
    if (m_dlg && IsWindowVisible(m_dlg)) SetStatus(why);
    if (m_winCreated) m_win.SetOfflineDetail(why);
}

std::wstring RemoteApp::NoAnswer() const
{
    return L"No answer from " + W(m_host) + L". Usually the Windows firewall on the EuroScope PC: run REMOTE-FIREWALL.bat "
           L"there once." + NetHint();
}

// Adresse in einem anderen Netz als dieses Geraet (z. B. Gast-WLAN, VPN)?
std::wstring RemoteApp::NetHint() const
{
    uint32_t ip = 0;
    if (!netif::ParseIp4(m_host, ip)) return std::wstring();
    std::vector<netif::Interface> v = netif::List();
    if (v.empty() || netif::SameNet(ip, v)) return std::wstring();
    return L" Note: " + W(m_host) + L" is not in the network of this device (" + W(v[0].addr) + L") - same Wi-Fi/LAN?";
}

// PC ueber seinen Namen wiederfinden (DHCP hat ihm eine neue IP-Adresse gegeben)
void RemoteApp::Rediscover()
{
    if (m_udp != INVALID_SOCKET) return;
    m_udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_udp == INVALID_SOCKET) return;
    WSAAsyncSelect(m_udp, m_msg, WM_RA_UDP, FD_READ);
    SendDiscovery(m_udp);
    SetTimer(m_msg, T_UDP_DONE, 1500, nullptr);
}

void RemoteApp::OnUdp()
{
    for (;;)
    {
        if (m_udp == INVALID_SOCKET) return;
        char buf[512];
        sockaddr_in from = {};
        int fl = sizeof(from);
        int n = recvfrom(m_udp, buf, sizeof(buf) - 1, 0, (sockaddr*)&from, &fl);
        if (n <= 0) return;   // nichts mehr da (WSAEWOULDBLOCK)
        buf[n] = 0;
        int port = 0;
        std::string name;
        if (!ParseDiscovery(buf, port, name) || Lower(name) != Lower(m_serverName)) continue;
        char ip[64] = {};
        inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
        CloseUdp();
        if (ip == m_host && port == m_port) return;
        m_host = ip;
        m_port = port;
        SaveIni();
        if (m_state == S_CONNECTING || m_state == S_IDLE)
            StartConnect(m_host + (m_port != remote::kDefaultPort ? ":" + std::to_string(m_port) : std::string()), m_code);
        return;
    }
}

void RemoteApp::CloseUdp()
{
    KillTimer(m_msg, T_UDP_DONE);
    if (m_udp != INVALID_SOCKET)
    {
        closesocket(m_udp);
        m_udp = INVALID_SOCKET;
    }
}

void RemoteApp::EnsureWindow()
{
    if (m_winCreated) return;
    m_winCreated = m_win.Create(m_ws);
    if (m_winCreated)
    {
        m_win.SetTitle(L"StripBay Remote");
        m_win.SetRemoteClient(L"Not connected");
    }
}

LRESULT RemoteApp::MsgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_RA_SOCK)
    {
        if ((SOCKET)wp != m_sock) return 0;
        long ev = WSAGETSELECTEVENT(lp);
        int err = WSAGETSELECTERROR(lp);
        if (ev == FD_CONNECT) { OnConnected(err); return 0; }
        if (ev == FD_CLOSE || err)
        {
            ReadAll();   // was noch da ist (z. B. die Ablehnung)
            if (m_sock != INVALID_SOCKET)
                Disconnected(L"Connection to EuroScope lost - reconnecting\x2026", m_auto || m_state == S_ONLINE);
            return 0;
        }
        if (ev == FD_WRITE) { Flush(); return 0; }
        if (ev == FD_READ) { ReadAll(); return 0; }
        return 0;
    }
    if (msg == WM_RA_DRAIN) { Drain(); return 0; }
    if (msg == WM_RA_UDP) { if ((SOCKET)wp == m_udp) OnUdp(); return 0; }
    if (msg == WM_TIMER)
    {
        if (wp == T_PING && m_state == S_ONLINE)
        {
            if (NowMs() - m_lastRx > 8000) Disconnected(L"No answer from EuroScope - reconnecting\x2026", true);
            else m_link.Ping();
        }
        else if (wp == T_RECONNECT && m_auto && m_state == S_IDLE)
        {
            if (m_fails >= 2 && !m_serverName.empty()) Rediscover();   // vielleicht neue IP-Adresse
            StartConnect(m_host + (m_port != remote::kDefaultPort ? ":" + std::to_string(m_port) : std::string()), m_code);
        }
        else if (wp == T_CONNECT_TIMEOUT && (m_state == S_CONNECTING || m_state == S_HELLO))
        {
            bool hello = m_state == S_HELLO;
            CloseSocket();
            if (hello)
                Fail(L"Connected to " + W(m_host) + L", but StripBay did not answer. Another program on port " +
                     std::to_wstring(m_port) + L"? (EuroScope: .strips remote status)");
            else
                Fail(NoAnswer());
        }
        else if (wp == T_UDP_DONE)
            CloseUdp();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int RemoteApp::Run(const std::wstring& cmdLine)
{
    HINSTANCE inst = (HINSTANCE)&__ImageBase;
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = RaMsgProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"StripBayRemoteMsg";
    RegisterClassExW(&wc);
    m_msg = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, inst, nullptr);
    if (!m_msg) return 1;

    m_link.send = [this](const std::string& f) { SendFrame(f); };
    m_link.wait = [this](uint32_t id, std::string& rep) { return WaitReply(id, rep); };
    m_link.localCommand = [this](const std::string& l) { return LocalCommand(l); };
    m_link.windowState = [this]() { WindowState(); };

    LoadIni(m_ws);
    // Aufruf: StripBayRemote.exe [PC[:Port]] [Code]
    std::vector<std::string> args = Tokens(U(cmdLine));
    if (!args.empty()) m_host = args[0];
    if (args.size() > 1) m_code = args[1];

    SetTimer(m_msg, T_PING, 2000, nullptr);
    if (!m_host.empty() && m_code.size() == 6)
    {
        // schon gekoppelt: gleich das Streifenfenster zeigen und verbinden
        EnsureWindow();
        m_auto = true;
        std::string hp = m_host + (m_port != remote::kDefaultPort && m_host.find(':') == std::string::npos ? ":" + std::to_string(m_port) : std::string());
        StartConnect(hp, m_code);
        SetTimer(m_msg, T_RECONNECT, 3000, nullptr);   // klappt es nicht, alle 3 s wieder
        Board b;
        b.connected = false;
        m_win.SetBoard(b);
    }
    else
        ShowDialog(L"Search finds StripBay in your network (EuroScope: .strips remote on).");

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        if (m_dlg && IsWindowVisible(m_dlg) && IsDialogMessageW(m_dlg, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    SaveIni();
    CloseSocket();
    CloseUdp();
    m_win.Destroy();
    if (m_dlg) DestroyWindow(m_dlg);
    if (m_dlgFont) DeleteObject(m_dlgFont);
    DestroyWindow(m_msg);
    return 0;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR cmdLine, int)
{
    // scharf auf hochaufloesenden Bildschirmen (das Streifenfenster schaltet ohnehin je Monitor um)
    typedef BOOL(WINAPI * PFN_SetDpiCtx)(HANDLE);
    if (HMODULE u = GetModuleHandleW(L"user32.dll"))
        if (PFN_SetDpiCtx f = (PFN_SetDpiCtx)(void*)GetProcAddress(u, "SetProcessDpiAwarenessContext")) f((HANDLE)(LONG_PTR)-4);
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);
    WSADATA wd;
    if (WSAStartup(MAKEWORD(2, 2), &wd) != 0)
    {
        MessageBoxW(nullptr, L"Network (Winsock) not available.", L"StripBay Remote", MB_ICONERROR);
        return 1;
    }
    RemoteApp app;
    g_app = &app;
    int rc = app.Run(cmdLine ? cmdLine : L"");
    g_app = nullptr;
    WSACleanup();
    return rc;
}
