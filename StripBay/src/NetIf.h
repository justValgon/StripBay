// StripBay Remote - die IPv4-Netzwerkadapter dieses Rechners.
// Fuer die Suche im Netz (Rundruf auf jedem Adapter, nicht nur auf dem "ersten") und um die
// richtige Adresse anzuzeigen: das Heimnetz (Adapter mit Standard-Gateway) zuerst, virtuelle
// Adapter (Hyper-V, WSL, VPN, VirtualBox ...) zuletzt.
#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#ifdef _MSC_VER
#pragma comment(lib, "iphlpapi.lib")
#endif

#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <string>
#include <vector>

namespace netif
{
    struct Interface
    {
        std::string addr;        // "192.168.1.20"
        std::string bcast;       // "192.168.1.255"
        uint32_t ip = 0;         // Host-Byte-Reihenfolge
        uint32_t mask = 0;
        bool gateway = false;    // hat ein Standard-Gateway: das echte Heimnetz
        bool virt = false;       // virtueller Adapter
        std::string name;        // Anzeigename, z. B. "WLAN"
    };

    inline std::string Ip4(uint32_t hostOrder)
    {
        return std::to_string(hostOrder >> 24) + "." + std::to_string((hostOrder >> 16) & 255) + "." +
               std::to_string((hostOrder >> 8) & 255) + "." + std::to_string(hostOrder & 255);
    }

    inline bool ParseIp4(const std::string& s, uint32_t& hostOrder)
    {
        in_addr a = {};
        if (inet_pton(AF_INET, s.c_str(), &a) != 1) return false;
        hostOrder = ntohl(a.s_addr);
        return true;
    }

    inline std::string Narrow(const wchar_t* w)
    {
        std::string s;
        if (w)
            for (; *w; ++w) s += (*w >= 32 && *w < 127) ? (char)*w : '?';
        return s;
    }

    inline bool LooksVirtual(const wchar_t* w)
    {
        if (!w) return false;
        std::wstring s(w);
        for (wchar_t& c : s) c = (wchar_t)towlower(c);
        const wchar_t* keys[] = { L"vethernet", L"virtual", L"hyper-v", L"vmware", L"virtualbox", L"wsl", L"tap-", L"tap ",
                                  L"vpn", L"wireguard", L"tailscale", L"zerotier", L"hamachi", L"loopback", L"bluetooth" };
        for (const wchar_t* k : keys)
            if (s.find(k) != std::wstring::npos) return true;
        return false;
    }

    inline int Rank(const Interface& f)
    {
        return f.gateway ? (f.virt ? 1 : 0) : (f.virt ? 3 : 2);
    }

    inline std::vector<Interface> List()
    {
        std::vector<Interface> v;
        ULONG flags = GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
        ULONG len = 16384;
        std::vector<unsigned char> buf;
        ULONG rc = ERROR_BUFFER_OVERFLOW;
        for (int i = 0; i < 4 && rc == ERROR_BUFFER_OVERFLOW; ++i)
        {
            buf.assign(len + 1024, 0);
            len = (ULONG)buf.size();
            rc = GetAdaptersAddresses(AF_INET, flags, nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()), &len);
        }
        if (rc != NO_ERROR) return v;
        for (IP_ADAPTER_ADDRESSES* a = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()); a; a = a->Next)
        {
            if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
            bool virt = a->IfType == IF_TYPE_TUNNEL || LooksVirtual(a->FriendlyName) || LooksVirtual(a->Description);
            for (IP_ADAPTER_UNICAST_ADDRESS* u = a->FirstUnicastAddress; u; u = u->Next)
            {
                if (!u->Address.lpSockaddr || u->Address.lpSockaddr->sa_family != AF_INET) continue;
                uint32_t ip = ntohl(reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr)->sin_addr.s_addr);
                if ((ip >> 24) == 127 || (ip >> 16) == 0xA9FE || ip == 0) continue;   // 127.x, 169.254.x
                int pl = (int)u->OnLinkPrefixLength;
                if (pl <= 0 || pl > 32) pl = 24;
                Interface f;
                f.ip = ip;
                f.mask = pl >= 32 ? 0xFFFFFFFFu : ~(0xFFFFFFFFu >> pl);
                f.addr = Ip4(ip);
                f.bcast = Ip4(ip | ~f.mask);
                f.gateway = a->FirstGatewayAddress != nullptr;
                f.virt = virt;
                f.name = Narrow(a->FriendlyName);
                v.push_back(f);
            }
        }
        std::stable_sort(v.begin(), v.end(), [](const Interface& x, const Interface& y) { return Rank(x) < Rank(y); });
        return v;
    }

    // Liegt die Adresse in einem der Netze dieses Rechners?
    inline bool SameNet(uint32_t ip, const std::vector<Interface>& v)
    {
        for (const Interface& f : v)
            if ((ip & f.mask) == (f.ip & f.mask)) return true;
        return false;
    }
}
