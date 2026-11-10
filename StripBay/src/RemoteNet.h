// StripBay Remote - Netz im Plugin: TCP-Server und Suche im lokalen Netz (UDP).
// Alle Socket-Ereignisse kommen als Fenstermeldungen im EuroScope-Hauptthread an (WSAAsyncSelect),
// das Plugin darf deshalb direkt die EuroScope-API benutzen - kein zweiter Thread.
#pragma once

#include "RemoteProto.h"

#include <map>
#include <string>
#include <vector>

class RemoteServerNet
{
public:
    RemoteServerNet() = default;
    ~RemoteServerNet();
    RemoteServerNet(const RemoteServerNet&) = delete;
    RemoteServerNet& operator=(const RemoteServerNet&) = delete;

    // Server starten (Port fuer TCP, Suche auf remote::kDiscoveryPort). false = Fehler in err
    bool Start(int port, remote::Hub* hub, std::string& err);
    void Stop();
    bool Running() const { return m_running; }
    int  Port() const { return m_port; }

    void Send(int client, const std::string& frame);
    void Drop(int client);

    // IPv4-Adressen dieses PCs (ohne 127.x), z. B. "192.168.1.20"
    static std::vector<std::string> LocalAddresses();
    static std::string ComputerName();

    // nur fuer RemoteNet.cpp
    void OnSocket(uintptr_t sock, long event, int error);
    void OnTimer();

private:
    struct Conn { uintptr_t sock = 0; remote::FrameReader in; std::string out; std::string addr; };
    remote::Hub* m_hub = nullptr;
    void* m_wnd = nullptr;      // HWND (Nachrichtenfenster)
    uintptr_t m_listen = ~(uintptr_t)0;
    uintptr_t m_udp = ~(uintptr_t)0;
    std::map<int, Conn> m_conns;
    std::map<uintptr_t, int> m_bySock;
    int m_nextId = 1;
    int m_port = 0;
    bool m_running = false;
    bool m_wsa = false;
    void Flush(int client);
    void CloseConn(int client, bool notify);
};
