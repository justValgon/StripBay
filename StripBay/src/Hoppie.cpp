// StripBay - Hoppie-ACARS-Client (CPDLC, DCL)
#include "Hoppie.h"
#include "Cdm.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <random>

static const char* kHoppieUrl = "https://www.hoppie.nl/acars/system/connect.html";

static std::string TrimH(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return std::string();
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static long long NowSec()
{
    return (long long)std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string UrlEncode(const std::string& s)
{
    static const char* hex = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : s)
    {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c;
        else if (c == ' ') o += '+';
        else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
    }
    return o;
}

bool ParseHoppieReply(const std::string& body, std::vector<HoppieMsg>& out, std::string& error)
{
    std::string b = TrimH(body);
    if (b.compare(0, 2, "ok") != 0)
    {
        size_t o = b.find('{'), c = b.rfind('}');
        error = (o != std::string::npos && c != std::string::npos && c > o) ? b.substr(o + 1, c - o - 1)
                                                                               : (b.empty() ? std::string("empty response") : b);
        return false;
    }
    // Bloecke {CS typ {inhalt}} auf oberster Ebene
    size_t i = 2;
    while (i < b.size())
    {
        size_t o = b.find('{', i);
        if (o == std::string::npos) break;
        int depth = 0;
        size_t e = o;
        for (; e < b.size(); ++e)
        {
            if (b[e] == '{') ++depth;
            else if (b[e] == '}' && --depth == 0) break;
        }
        if (e >= b.size()) break;
        std::string inner = TrimH(b.substr(o + 1, e - o - 1));
        i = e + 1;
        size_t s1 = inner.find(' ');
        if (s1 == std::string::npos) continue;
        size_t s2 = inner.find(' ', s1 + 1);
        HoppieMsg m;
        m.from = inner.substr(0, s1);
        m.type = s2 == std::string::npos ? inner.substr(s1 + 1) : inner.substr(s1 + 1, s2 - s1 - 1);
        std::string rest = s2 == std::string::npos ? std::string() : TrimH(inner.substr(s2 + 1));
        if (rest.size() >= 2 && rest.front() == '{' && rest.back() == '}') rest = rest.substr(1, rest.size() - 2);
        m.packet = TrimH(rest);
        for (char& c : m.type) c = (char)tolower((unsigned char)c);
        for (char& c : m.from) c = (char)toupper((unsigned char)c);
        out.push_back(m);
    }
    return true;
}

bool ParseCpdlc(const std::string& packet, int& min, int& mrn, std::string& ra, std::string& text)
{
    min = -1;
    mrn = -1;
    ra.clear();
    text.clear();
    std::string p = TrimH(packet);
    if (p.compare(0, 7, "/data2/") != 0 && p.compare(0, 7, "/DATA2/") != 0) return false;
    std::string f[3];
    size_t pos = 7;
    for (int k = 0; k < 3; ++k)
    {
        size_t sl = p.find('/', pos);
        if (sl == std::string::npos) return false;
        f[k] = p.substr(pos, sl - pos);
        pos = sl + 1;
    }
    text = TrimH(p.substr(pos));
    min = f[0].empty() ? -1 : atoi(f[0].c_str());
    mrn = f[1].empty() ? -1 : atoi(f[1].c_str());
    ra = f[2];
    for (char& c : ra) c = (char)toupper((unsigned char)c);
    return true;
}

std::string CpdlcDisplay(const std::string& text)
{
    std::string o;
    for (char c : text)
    {
        char d = (c == '@' || c == '\r' || c == '\n') ? ' ' : c;
        if (d == ' ' && (o.empty() || o.back() == ' ')) continue;
        o += d;
    }
    while (!o.empty() && o.back() == ' ') o.pop_back();
    return o;
}

// ---------------------------------------------------------------------------
HoppieClient::~HoppieClient()
{
    Stop();
}

void HoppieClient::Start()
{
    if (m_thread.joinable()) return;
    m_stop = false;
    m_thread = std::thread(&HoppieClient::Run, this);
}

void HoppieClient::Stop()
{
    if (!m_thread.joinable()) return;
    m_stop = true;
    m_cv.notify_all();
    m_thread.join();
}

void HoppieClient::Configure(const std::string& logon, const std::string& station)
{
    std::lock_guard<std::mutex> lk(m_mx);
    if (logon != m_logon || station != m_station)
    {
        m_logon = logon;
        m_station = station;
        m_connected = false;
        m_lastError.clear();
        m_pollNow = true;
        m_cv.notify_all();
    }
}

std::string HoppieClient::Station()
{
    std::lock_guard<std::mutex> lk(m_mx);
    return m_station;
}

void HoppieClient::Send(const std::string& to, const std::string& type, const std::string& packet)
{
    std::lock_guard<std::mutex> lk(m_mx);
    m_out.push_back({ to, type, packet });
    m_cv.notify_all();
}

void HoppieClient::PollSoon()
{
    std::lock_guard<std::mutex> lk(m_mx);
    m_fastUntil = NowSec() + 180;
    m_cv.notify_all();
}

std::vector<HoppieMsg> HoppieClient::TakeIncoming()
{
    std::lock_guard<std::mutex> lk(m_mx);
    std::vector<HoppieMsg> v;
    v.swap(m_in);
    return v;
}

std::vector<HoppieSent> HoppieClient::TakeSent()
{
    std::lock_guard<std::mutex> lk(m_mx);
    std::vector<HoppieSent> v;
    v.swap(m_sent);
    return v;
}

bool HoppieClient::Connected()
{
    std::lock_guard<std::mutex> lk(m_mx);
    return m_connected;
}

std::string HoppieClient::Status()
{
    std::lock_guard<std::mutex> lk(m_mx);
    std::string s = m_station.empty() ? std::string("no station") : "Station " + m_station;
    if (m_logon.empty()) s += ", no logon code";
    else if (m_connected) s += ", connected";
    else s += ", not connected";
    if (!m_lastError.empty()) s += " (" + m_lastError + ")";
    return s;
}

bool HoppieClient::Request(const std::string& to, const std::string& type, const std::string& packet,
                           std::string& body, std::string& error)
{
    std::string logon, station;
    {
        std::lock_guard<std::mutex> lk(m_mx);
        logon = m_logon;
        station = m_station;
    }
    std::string data = "logon=" + UrlEncode(logon) + "&from=" + UrlEncode(station) + "&to=" + UrlEncode(to) +
                       "&type=" + UrlEncode(type) + "&packet=" + UrlEncode(packet);
    // wie die verbreiteten Clients (vSMR, TopSky): GET mit Parametern in der URL. Hoppie antwortet
    // unter Last mitunter erst nach 10+ Sekunden -> langer Empfangs-Timeout.
    return HttpRequest(std::string(kHoppieUrl) + "?" + data, std::string(), body, error, &m_stop, 25000);
}

void HoppieClient::Run()
{
    std::mt19937 rng((unsigned)std::time(nullptr));
    long long nextPoll = NowSec() + 2;
    long long lastSend = 0;
    while (!m_stop)
    {
        HoppieMsg out;
        bool haveOut = false, configured = false, pollNow = false;
        long long fastUntil = 0;
        {
            std::lock_guard<std::mutex> lk(m_mx);
            configured = !m_logon.empty() && !m_station.empty();
            if (configured && !m_out.empty() && NowSec() >= lastSend + 2)
            {
                out = m_out.front();
                m_out.pop_front();
                haveOut = true;
            }
            pollNow = m_pollNow;
            m_pollNow = false;
            fastUntil = m_fastUntil;
        }
        long long now = NowSec();
        if (now < fastUntil && nextPoll > now + 20) nextPoll = now + 20;   // Antwort erwartet
        if (haveOut)
        {
            std::string body, error;
            bool ok = Request(out.from, out.type, out.packet, body, error);
            std::vector<HoppieMsg> dummy;
            if (ok && !ParseHoppieReply(body, dummy, error)) ok = false;
            lastSend = NowSec();
            std::lock_guard<std::mutex> lk(m_mx);
            m_sent.push_back({ out.from, out.type, out.packet, ok, error });
            if (ok) { m_connected = true; m_lastOk = lastSend; m_lastError.clear(); }
            else m_lastError = error;
            continue;
        }
        if (configured && (pollNow || now >= nextPoll))
        {
            std::string body, error;
            std::vector<HoppieMsg> in;
            bool ok = Request("SERVER", "poll", std::string(), body, error) && ParseHoppieReply(body, in, error);
            now = NowSec();
            {
                std::lock_guard<std::mutex> lk(m_mx);
                m_connected = ok;
                if (ok)
                {
                    m_lastOk = now;
                    m_lastError.clear();
                    for (HoppieMsg& m : in) m_in.push_back(m);
                }
                else m_lastError = error;
            }
            // 45-75 s zufaellig, nach dem Senden eine Weile alle 20 s
            nextPoll = now + (now < fastUntil ? 20 : 45 + (long long)(rng() % 31));
            continue;
        }
        std::unique_lock<std::mutex> lk(m_mx);
        long long wake = configured ? nextPoll : now + 5;
        if (!m_out.empty()) wake = std::min(wake, lastSend + 2);
        long long wait = std::max(1LL, wake - NowSec());
        m_cv.wait_for(lk, std::chrono::seconds(wait), [&] { return m_stop.load() || m_pollNow || (!m_out.empty() && NowSec() >= lastSend + 2); });
    }
}
