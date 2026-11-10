// StripBay - TSAT aus CDM-Quellen (siehe Cdm.h)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>

#include "Cdm.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <vector>

#pragma comment(lib, "winhttp.lib")

// ---------------------------------------------------------------------------
// Zeit
// ---------------------------------------------------------------------------
static long long DaysFromCivil(long long y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long long)doe - 719468;
}

long long ParseIsoUtc(const std::string& iso)
{
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
    if (sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &s) < 5) return 0;
    if (y < 2000 || mo < 1 || mo > 12 || d < 1 || d > 31) return 0;   // vACDM nutzt 1969-12-31 fuer "keine Zeit"
    return DaysFromCivil(y, (unsigned)mo, (unsigned)d) * 86400LL + h * 3600LL + mi * 60LL + s;
}

long long TimeOfDayToUtc(const std::string& t, long long nowUtc)
{
    if (t.size() < 4) return 0;
    for (char c : t)
        if (c < '0' || c > '9') return 0;
    int hh = (t[0] - '0') * 10 + (t[1] - '0');
    int mm = (t[2] - '0') * 10 + (t[3] - '0');
    int ss = t.size() >= 6 ? (t[4] - '0') * 10 + (t[5] - '0') : 0;
    if (hh > 23 || mm > 59 || ss > 59) return 0;   // faengt auch "999999" ab
    long long day = nowUtc - (nowUtc % 86400);
    long long v = day + hh * 3600LL + mm * 60LL + ss;
    if (v - nowUtc > 43200) v -= 86400;
    else if (nowUtc - v > 43200) v += 86400;
    return v;
}

std::string FormatHHMM(long long utc)
{
    if (utc <= 0) return std::string();
    long long sod = utc % 86400;
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d%02d", (int)(sod / 3600), (int)((sod % 3600) / 60));
    return buf;
}

// ---------------------------------------------------------------------------
// Kleiner JSON-Parser (nur was fuer die Pilotenliste gebraucht wird)
// ---------------------------------------------------------------------------
namespace
{
struct JVal
{
    enum Type { NUL, BOOL, NUM, STR, ARR, OBJ } type = NUL;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<JVal> arr;
    std::vector<std::pair<std::string, JVal>> obj;

    const JVal* Get(const char* key) const
    {
        if (type != OBJ) return nullptr;
        for (const auto& kv : obj)
            if (kv.first == key) return &kv.second;
        return nullptr;
    }
    std::string Str(const char* key) const
    {
        const JVal* v = Get(key);
        return (v && v->type == STR) ? v->s : std::string();
    }
};

class JParser
{
public:
    JParser(const char* p, const char* e) : m_p(p), m_e(e) {}

    bool Parse(JVal& out)
    {
        Ws();
        if (!Value(out, 0)) return false;
        return true;
    }

private:
    void Ws()
    {
        while (m_p < m_e && (*m_p == ' ' || *m_p == '\t' || *m_p == '\n' || *m_p == '\r')) ++m_p;
    }

    bool Lit(const char* w)
    {
        size_t n = strlen(w);
        if ((size_t)(m_e - m_p) < n || strncmp(m_p, w, n) != 0) return false;
        m_p += n;
        return true;
    }

    static void PutUtf8(std::string& s, unsigned cp)
    {
        if (cp < 0x80) s += (char)cp;
        else if (cp < 0x800) { s += (char)(0xC0 | (cp >> 6)); s += (char)(0x80 | (cp & 0x3F)); }
        else { s += (char)(0xE0 | (cp >> 12)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
    }

    bool String(std::string& out)
    {
        if (m_p >= m_e || *m_p != '"') return false;
        ++m_p;
        while (m_p < m_e && *m_p != '"')
        {
            char c = *m_p++;
            if (c != '\\') { out += c; continue; }
            if (m_p >= m_e) return false;
            char e = *m_p++;
            switch (e)
            {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u':
            {
                if (m_e - m_p < 4) return false;
                unsigned cp = 0;
                for (int i = 0; i < 4; ++i)
                {
                    char h = *m_p++;
                    cp <<= 4;
                    if (h >= '0' && h <= '9') cp |= (unsigned)(h - '0');
                    else if (h >= 'a' && h <= 'f') cp |= (unsigned)(h - 'a' + 10);
                    else if (h >= 'A' && h <= 'F') cp |= (unsigned)(h - 'A' + 10);
                    else return false;
                }
                PutUtf8(out, cp);
                break;
            }
            default: return false;
            }
        }
        if (m_p >= m_e) return false;
        ++m_p;
        return true;
    }

    bool Value(JVal& v, int depth)
    {
        if (depth > 64 || m_p >= m_e) return false;
        char c = *m_p;
        if (c == '{')
        {
            v.type = JVal::OBJ;
            ++m_p;
            Ws();
            if (m_p < m_e && *m_p == '}') { ++m_p; return true; }
            for (;;)
            {
                Ws();
                std::string key;
                if (!String(key)) return false;
                Ws();
                if (m_p >= m_e || *m_p != ':') return false;
                ++m_p;
                Ws();
                v.obj.emplace_back(std::move(key), JVal());
                if (!Value(v.obj.back().second, depth + 1)) return false;
                Ws();
                if (m_p < m_e && *m_p == ',') { ++m_p; continue; }
                if (m_p < m_e && *m_p == '}') { ++m_p; return true; }
                return false;
            }
        }
        if (c == '[')
        {
            v.type = JVal::ARR;
            ++m_p;
            Ws();
            if (m_p < m_e && *m_p == ']') { ++m_p; return true; }
            for (;;)
            {
                Ws();
                v.arr.emplace_back();
                if (!Value(v.arr.back(), depth + 1)) return false;
                Ws();
                if (m_p < m_e && *m_p == ',') { ++m_p; continue; }
                if (m_p < m_e && *m_p == ']') { ++m_p; return true; }
                return false;
            }
        }
        if (c == '"') { v.type = JVal::STR; return String(v.s); }
        if (Lit("true")) { v.type = JVal::BOOL; v.b = true; return true; }
        if (Lit("false")) { v.type = JVal::BOOL; v.b = false; return true; }
        if (Lit("null")) { v.type = JVal::NUL; return true; }
        // Zahl
        const char* start = m_p;
        while (m_p < m_e && *m_p && (strchr("+-.eE", *m_p) || (*m_p >= '0' && *m_p <= '9'))) ++m_p;
        if (m_p == start) return false;
        v.type = JVal::NUM;
        v.n = atof(std::string(start, m_p).c_str());
        return true;
    }

    const char* m_p;
    const char* m_e;
};
}  // namespace

bool ParseVacdmPilots(const std::string& body, std::map<std::string, long long>& tsat,
                      std::map<std::string, long long>* ttot, std::map<std::string, long long>* ctot)
{
    JVal root;
    JParser parser(body.data(), body.data() + body.size());
    if (!parser.Parse(root)) return false;

    // je nach Serverversion: Array oder { "pilots": [...] }
    const JVal* list = &root;
    if (root.type == JVal::OBJ) list = root.Get("pilots");
    if (!list || list->type != JVal::ARR) return false;

    for (const JVal& p : list->arr)
    {
        const JVal* inactive = p.Get("inactive");
        if (inactive && inactive->type == JVal::BOOL && inactive->b) continue;
        std::string cs = p.Str("callsign");
        const JVal* v = p.Get("vacdm");
        if (cs.empty() || !v) continue;
        long long t = ParseIsoUtc(v->Str("tsat"));
        if (t > 0) tsat[cs] = t;
        if (ttot) { long long x = ParseIsoUtc(v->Str("ttot")); if (x > 0) (*ttot)[cs] = x; }
        if (ctot) { long long x = ParseIsoUtc(v->Str("ctot")); if (x > 0) (*ctot)[cs] = x; }
    }
    return true;
}

bool ParseVatsimAtis(const std::string& body, std::map<std::string, std::string>& codes)
{
    // Der Datenfeed ist gross, nur das Feld "atis" parsen
    size_t k = body.find("\"atis\":");
    if (k == std::string::npos) return false;
    size_t a = body.find('[', k);
    if (a == std::string::npos) return false;
    JVal list;
    JParser parser(body.data() + a, body.data() + body.size());
    if (!parser.Parse(list) || list.type != JVal::ARR) return false;
    for (const JVal& e : list.arr)
    {
        std::string cs = e.Str("callsign");
        const JVal* c = e.Get("atis_code");
        if (cs.empty() || !c || c->type != JVal::STR || c->s.empty()) continue;
        codes[cs] = c->s.substr(0, 1);
    }
    return true;
}

// ---------------------------------------------------------------------------
// vACDM-Abfrage
// ---------------------------------------------------------------------------
VacdmClient::VacdmClient() {}

VacdmClient::~VacdmClient()
{
    Stop();
}

void VacdmClient::Start()
{
    if (m_thread.joinable()) return;
    m_stop = false;
    m_thread = std::thread(&VacdmClient::Run, this);
}

void VacdmClient::Stop()
{
    if (!m_thread.joinable()) return;
    m_stop = true;
    m_cv.notify_all();
    m_thread.join();
}

void VacdmClient::SetUrl(const std::string& baseUrl)
{
    std::string u = baseUrl;
    while (!u.empty() && (u.back() == '/' || u.back() == ' ')) u.pop_back();
    std::lock_guard<std::mutex> lk(m_mx);
    if (u != m_url)
    {
        m_url = u;
        m_tsat.clear(); m_ttot.clear(); m_ctot.clear();
        m_cv.notify_all();   // sofort neu abfragen
    }
}

void VacdmClient::SetAirports(const std::set<std::string>& airports)
{
    std::lock_guard<std::mutex> lk(m_mx);
    if (airports != m_airports)
    {
        m_airports = airports;
        m_cv.notify_all();
    }
}

std::string VacdmClient::GetUrl()
{
    std::lock_guard<std::mutex> lk(m_mx);
    return m_url;
}

long long VacdmClient::GetTsat(const std::string& callsign)
{
    std::lock_guard<std::mutex> lk(m_mx);
    auto it = m_tsat.find(callsign);
    return it == m_tsat.end() ? 0 : it->second;
}

long long VacdmClient::GetTtot(const std::string& callsign)
{
    std::lock_guard<std::mutex> lk(m_mx);
    auto it = m_ttot.find(callsign);
    return it == m_ttot.end() ? 0 : it->second;
}

long long VacdmClient::GetCtot(const std::string& callsign)
{
    std::lock_guard<std::mutex> lk(m_mx);
    auto it = m_ctot.find(callsign);
    return it == m_ctot.end() ? 0 : it->second;
}

std::string VacdmClient::Status()
{
    std::lock_guard<std::mutex> lk(m_mx);
    std::string s = "vACDM " + (m_url.empty() ? std::string("(no URL)") : m_url) + ": ";
    if (!m_thread.joinable()) return s + "aus";
    if (m_lastOk > 0)
    {
        long long sod = m_lastOk % 86400;
        char buf[96];
        snprintf(buf, sizeof(buf), "last download %02d:%02d:%02dZ, %d aircraft with TSAT", (int)(sod / 3600),
                 (int)((sod % 3600) / 60), (int)(sod % 60), m_lastCount);
        s += buf;
    }
    else s += "no response yet";
    if (!m_lastError.empty()) s += ", error: " + m_lastError;
    return s;
}

std::string FormatIsoUtc(long long utc)
{
    if (utc <= 0) return "1969-12-31T23:59:59.999Z";
    time_t t = (time_t)utc;
    struct tm g = {};
#ifdef _WIN32
    gmtime_s(&g, &t);
#else
    gmtime_r(&t, &g);
#endif
    char buf[40];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.000Z", g.tm_year + 1900, g.tm_mon + 1, g.tm_mday, g.tm_hour, g.tm_min,
             g.tm_sec);
    return buf;
}

std::string VacdmTobtJson(const std::string& callsign, long long tobtUtc, long long tsatUtc)
{
    const std::string none = "1969-12-31T23:59:59.999Z";
    std::string j = "{\"callsign\":\"" + callsign + "\",\"vacdm\":{\"tobt\":\"" + FormatIsoUtc(tobtUtc) + "\",\"tobt_state\":\"CONFIRMED\"";
    if (tsatUtc <= 0 || tobtUtc >= tsatUtc) j += ",\"tsat\":\"" + none + "\"";
    j += ",\"ttot\":\"" + none + "\",\"asat\":\"" + none + "\",\"aobt\":\"" + none + "\",\"atot\":\"" + none + "\"}}";
    return j;
}

void VacdmClient::SetTobt(const std::string& callsign, long long tobtUtc)
{
    std::lock_guard<std::mutex> lk(m_mx);
    auto t = m_tsat.find(callsign);
    m_patches.push_back({ callsign, VacdmTobtJson(callsign, tobtUtc, t == m_tsat.end() ? 0 : t->second) });
    m_cv.notify_all();
}

std::string VacdmClient::LastPatch()
{
    std::lock_guard<std::mutex> lk(m_mx);
    return m_lastPatch;
}

void VacdmClient::Run()
{
    while (!m_stop)
    {
        std::string url;
        std::set<std::string> apts;
        std::vector<std::pair<std::string, std::string>> patches;
        {
            std::lock_guard<std::mutex> lk(m_mx);
            url = m_url;
            apts = m_airports;
            patches.swap(m_patches);
        }
        // TOBT-Meldungen zuerst (danach die Liste neu holen, damit die neue TSAT gleich da ist)
        for (const auto& pt : patches)
        {
            if (url.empty()) break;
            std::string body, error;
            bool ok = HttpSend(url + "/api/v1/pilots/" + pt.first, L"PATCH", "application/json", pt.second, body, error, &m_stop);
            std::lock_guard<std::mutex> lk(m_mx);
            m_lastPatch = pt.first + (ok ? ": TOBT sent to vACDM" : ": TOBT not accepted by vACDM (" + error + ")");
        }

        if (!url.empty() && !apts.empty())
        {
            std::string full = url + "/api/v1/pilots";
            char sep = '?';
            for (const std::string& a : apts)
            {
                full += sep;
                full += "adep=" + a;
                sep = '&';
            }

            std::string body, error;
            if (Fetch(full, body, error))
            {
                std::map<std::string, long long> tsat, ttot, ctot;
                if (ParseVacdmPilots(body, tsat, &ttot, &ctot))
                {
                    std::lock_guard<std::mutex> lk(m_mx);
                    if (url == m_url)
                    {
                        m_tsat.swap(tsat); m_ttot.swap(ttot); m_ctot.swap(ctot);
                        m_lastOk = (long long)std::time(nullptr);
                        m_lastCount = (int)m_tsat.size();
                        m_lastError.clear();
                    }
                }
                else
                {
                    std::lock_guard<std::mutex> lk(m_mx);
                    m_lastError = "response is not valid JSON";
                }
            }
            else
            {
                std::lock_guard<std::mutex> lk(m_mx);
                m_lastError = error;
            }
        }

        std::unique_lock<std::mutex> lk(m_mx);
        m_cv.wait_for(lk, std::chrono::seconds(10), [this] { return m_stop.load() || !m_patches.empty(); });
    }
}

static std::wstring Widen(const std::string& s)
{
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

std::string VacdmClient::LastError()
{
    std::lock_guard<std::mutex> lk(m_mx);
    return m_lastError;
}

bool VacdmClient::Fetch(const std::string& url, std::string& body, std::string& error)
{
    return HttpGet(url, body, error, &m_stop);
}

bool HttpGet(const std::string& url, std::string& body, std::string& error, const std::atomic<bool>* stop)
{
    return HttpRequest(url, std::string(), body, error, stop);
}

bool HttpRequest(const std::string& url, const std::string& postData, std::string& body, std::string& error,
                 const std::atomic<bool>* stop, int receiveTimeoutMs)
{
    if (postData.empty()) return HttpSend(url, L"GET", std::string(), std::string(), body, error, stop, receiveTimeoutMs);
    return HttpSend(url, L"POST", "application/x-www-form-urlencoded", postData, body, error, stop, receiveTimeoutMs);
}

bool HttpSend(const std::string& url, const wchar_t* method, const std::string& contentType, const std::string& postData,
              std::string& body, std::string& error, const std::atomic<bool>* stop, int receiveTimeoutMs)
{
    bool post = !postData.empty();
    std::wstring wurl = Widen(url);
    URL_COMPONENTS uc = {};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {}, path[2048] = {};
    uc.lpszHostName = host;
    uc.dwHostNameLength = 255;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 2047;
    uc.dwExtraInfoLength = (DWORD)-1;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc))
    {
        error = "invalid URL";
        return false;
    }
    std::wstring object = std::wstring(path, uc.dwUrlPathLength);
    if (uc.lpszExtraInfo && uc.dwExtraInfoLength) object += std::wstring(uc.lpszExtraInfo, uc.dwExtraInfoLength);

    bool ok = false;
    HINTERNET ses = WinHttpOpen(L"StripBay", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET con = nullptr, req = nullptr;
    if (ses)
    {
        WinHttpSetTimeouts(ses, 5000, 8000, 8000, receiveTimeoutMs);
        con = WinHttpConnect(ses, host, uc.nPort, 0);
    }
    if (con)
        req = WinHttpOpenRequest(con, method, object.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                 uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    bool sent = false;
    if (req && post)
    {
        std::wstring hdr = L"Accept: application/json\r\nContent-Type: " + Widen(contentType) + L"\r\n";
        sent = WinHttpSendRequest(req, hdr.c_str(), (DWORD)-1L,
                                  (LPVOID)postData.data(), (DWORD)postData.size(), (DWORD)postData.size(), 0) != FALSE;
    }
    else if (req)
        sent = WinHttpSendRequest(req, L"Accept: application/json\r\n", (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) != FALSE;
    if (sent &&
        WinHttpReceiveResponse(req, nullptr))
    {
        DWORD status = 0, len = sizeof(status);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                            &status, &len, WINHTTP_NO_HEADER_INDEX);
        DWORD avail = 0;
        while (WinHttpQueryDataAvailable(req, &avail) && avail > 0)
        {
            size_t old = body.size();
            body.resize(old + avail);
            DWORD read = 0;
            if (!WinHttpReadData(req, &body[old], avail, &read)) break;
            body.resize(old + read);
            if (body.size() > 32u * 1024u * 1024u || (stop && *stop)) break;
        }
        if (status >= 200 && status < 300) ok = true;
        else
        {
            char buf[48];
            snprintf(buf, sizeof(buf), "HTTP %lu", (unsigned long)status);
            error = buf;
        }
    }
    else
    {
        char buf[48];
        snprintf(buf, sizeof(buf), "no connection (code %lu)", (unsigned long)GetLastError());
        error = buf;
    }

    if (req) WinHttpCloseHandle(req);
    if (con) WinHttpCloseHandle(con);
    if (ses) WinHttpCloseHandle(ses);
    return ok;
}

// ---------------------------------------------------------------------------
// METAR und ATIS
// ---------------------------------------------------------------------------
WxClient::~WxClient()
{
    Stop();
}

void WxClient::Start()
{
    if (m_thread.joinable()) return;
    m_stop = false;
    m_thread = std::thread(&WxClient::Run, this);
}

void WxClient::Stop()
{
    if (!m_thread.joinable()) return;
    m_stop = true;
    m_cv.notify_all();
    m_thread.join();
}

void WxClient::SetAirports(const std::set<std::string>& airports)
{
    std::lock_guard<std::mutex> lk(m_mx);
    if (airports != m_airports)
    {
        m_airports = airports;
        m_cv.notify_all();   // sofort holen
    }
}

std::string WxClient::GetMetar(const std::string& icao)
{
    std::lock_guard<std::mutex> lk(m_mx);
    auto it = m_metar.find(icao);
    return it == m_metar.end() ? std::string() : it->second;
}

std::string WxClient::GetAtis(const std::string& icao)
{
    std::lock_guard<std::mutex> lk(m_mx);
    // ICAO_ATIS (eine ATIS) oder ICAO_D_ATIS / ICAO_A_ATIS (getrennt)
    std::string single, parts;
    for (const auto& kv : m_atis)
    {
        const std::string& cs = kv.first;
        if (cs.compare(0, icao.size() + 1, icao + "_") != 0) continue;
        if (cs.size() < 5 || cs.compare(cs.size() - 5, 5, "_ATIS") != 0) continue;
        size_t start = icao.size() + 1, end = cs.size() - 5;   // zwischen "ICAO_" und "_ATIS"
        std::string mid = end > start ? cs.substr(start, end - start) : std::string();   // "", "D", "A"
        if (mid.empty()) single = kv.second;
        else
        {
            if (!parts.empty()) parts += "  ";
            parts += mid + " " + kv.second;
        }
    }
    if (!single.empty() && parts.empty()) return single;
    if (!single.empty()) return single + "  " + parts;
    return parts;
}

std::string WxClient::Status()
{
    std::lock_guard<std::mutex> lk(m_mx);
    std::string s = "Wetter: ";
    if (!m_thread.joinable()) return s + "aus";
    if (m_lastOk > 0)
    {
        long long sod = m_lastOk % 86400;
        char buf[96];
        snprintf(buf, sizeof(buf), "last download %02d:%02d:%02dZ, %d METAR, %d ATIS online", (int)(sod / 3600),
                 (int)((sod % 3600) / 60), (int)(sod % 60), (int)m_metar.size(), (int)m_atis.size());
        s += buf;
    }
    else s += "no response yet";
    if (!m_lastError.empty()) s += ", error: " + m_lastError;
    return s;
}

void WxClient::Run()
{
    while (!m_stop)
    {
        std::set<std::string> apts;
        {
            std::lock_guard<std::mutex> lk(m_mx);
            apts = m_airports;
        }
        std::string err;
        std::map<std::string, std::string> metar;
        for (const std::string& a : apts)
        {
            if (m_stop) break;
            std::string body, e;
            if (HttpGet("https://metar.vatsim.net/" + a, body, e, &m_stop))
            {
                size_t nl = body.find_first_of("\r\n");
                std::string line = body.substr(0, nl);
                while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) line.pop_back();
                if (line.size() > 8) metar[a] = line;
            }
            else err = "METAR: " + e;
        }
        std::map<std::string, std::string> atis;
        bool atisOk = false;
        if (!apts.empty() && !m_stop)
        {
            std::string body, e;
            if (HttpGet("https://data.vatsim.net/v3/vatsim-data.json", body, e, &m_stop))
                atisOk = ParseVatsimAtis(body, atis);
            else err = "ATIS: " + e;
            if (!atisOk && err.empty()) err = "ATIS: response not readable";
        }
        {
            std::lock_guard<std::mutex> lk(m_mx);
            for (const auto& kv : metar) m_metar[kv.first] = kv.second;
            if (atisOk) m_atis.swap(atis);
            m_lastError = err;
            if (!metar.empty() || atisOk) m_lastOk = (long long)std::time(nullptr);
        }
        std::unique_lock<std::mutex> lk(m_mx);
        m_cv.wait_for(lk, std::chrono::seconds(60), [this] { return m_stop.load(); });
    }
}

// ---------------------------------------------------------------------------
// Update-Hinweis
// ---------------------------------------------------------------------------
UpdateChecker::~UpdateChecker()
{
    Stop();
}

void UpdateChecker::Start(const std::string& repo)
{
    if (m_running) return;                      // laeuft noch
    if (m_thread.joinable()) m_thread.join();   // fertig: aufraeumen
    m_stop = false;
    m_done = false;
    m_running = true;
    m_thread = std::thread([this, repo]() {
        std::string body, err, tag;
        if (HttpGet("https://api.github.com/repos/" + repo + "/releases/latest", body, err, &m_stop)) tag = ParseReleaseTag(body);
        {
            std::lock_guard<std::mutex> lk(m_mx);
            m_tag = tag;
        }
        m_done = true;
        m_running = false;
    });
}

void UpdateChecker::Stop()
{
    if (!m_thread.joinable()) return;
    m_stop = true;
    m_thread.join();
}

bool UpdateChecker::TryTake(std::string& tag)
{
    if (!m_done.exchange(false)) return false;
    std::lock_guard<std::mutex> lk(m_mx);
    tag = m_tag;
    return true;
}
