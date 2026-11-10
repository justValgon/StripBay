// StripBay - ICAO-Daten aus dem Sektorpaket (siehe Icao.h)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "Icao.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

// ---------------------------------------------------------------------------
// Dateien lesen
// ---------------------------------------------------------------------------
static bool ReadAll(const std::wstring& path, std::string& text)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    char buf[65536];
    DWORD read = 0;
    while (ReadFile(h, buf, sizeof(buf), &read, nullptr) && read > 0)
    {
        text.append(buf, read);
        if (text.size() > 64u * 1024u * 1024u) break;
    }
    CloseHandle(h);
    return true;
}

bool ReadTextFileW(const std::wstring& path, std::string& text)
{
    text.clear();
    return ReadAll(path, text);
}

bool WriteTextFileW(const std::wstring& path, const std::string& text)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD done = 0;
    BOOL ok = WriteFile(h, text.data(), (DWORD)text.size(), &done, nullptr);
    CloseHandle(h);
    return ok && done == (DWORD)text.size();
}

static std::string TrimS(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return std::string();
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Ruft fn(felder) fuer jede Datenzeile auf (Kommentare mit ";" werden uebersprungen)
template <typename Fn>
static void ForEachLine(const std::string& text, Fn fn)
{
    size_t pos = 0;
    while (pos < text.size())
    {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == ';') continue;

        std::vector<std::string> f;
        size_t s = 0;
        for (;;)
        {
            size_t t = line.find('\t', s);
            f.push_back(TrimS(line.substr(s, t == std::string::npos ? std::string::npos : t - s)));
            if (t == std::string::npos) break;
            s = t + 1;
        }
        fn(f);
    }
}

static std::string UpperS(std::string s)
{
    for (char& c : s) c = (char)toupper((unsigned char)c);
    return s;
}

bool LoadAirlines(const std::wstring& path, std::map<std::string, std::string>& out)
{
    std::string text;
    if (!ReadAll(path, text)) return false;
    ForEachLine(text, [&](const std::vector<std::string>& f) {
        if (f.size() >= 3 && f[0].size() == 3 && !f[2].empty()) out[UpperS(f[0])] = f[2];
    });
    return !out.empty();
}

bool LoadAirports(const std::wstring& path, std::map<std::string, std::string>& out)
{
    std::string text;
    if (!ReadAll(path, text)) return false;
    ForEachLine(text, [&](const std::vector<std::string>& f) {
        if (f.size() >= 2 && f[0].size() == 4 && !f[1].empty()) out[UpperS(f[0])] = f[1];
    });
    return !out.empty();
}

// "A9" < "A10" < "B1"
static bool NaturalLess(const std::string& a, const std::string& b)
{
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size())
    {
        if (isdigit((unsigned char)a[i]) && isdigit((unsigned char)b[j]))
        {
            size_t i2 = i, j2 = j;
            while (i2 < a.size() && isdigit((unsigned char)a[i2])) ++i2;
            while (j2 < b.size() && isdigit((unsigned char)b[j2])) ++j2;
            long x = atol(a.substr(i, i2 - i).c_str()), y = atol(b.substr(j, j2 - j).c_str());
            if (x != y) return x < y;
            i = i2;
            j = j2;
        }
        else
        {
            if (a[i] != b[j]) return a[i] < b[j];
            ++i;
            ++j;
        }
    }
    return a.size() - i < b.size() - j;
}

bool ParseGrpCoord(const std::string& raw, double& deg)
{
    std::string s = TrimS(raw);
    if (s.empty()) return false;
    double sign = 1.0;
    char h = (char)toupper((unsigned char)s[0]);
    if (h == 'N' || h == 'S' || h == 'E' || h == 'W')
    {
        if (h == 'S' || h == 'W') sign = -1.0;
        s = s.substr(1);
        // Sektorformat: GGG.MM.SS.sss
        std::vector<double> p;
        size_t a = 0;
        for (int k = 0; k < 3 && a <= s.size(); ++k)
        {
            size_t b = (k < 2) ? s.find('.', a) : std::string::npos;
            std::string part = s.substr(a, b == std::string::npos ? std::string::npos : b - a);
            if (part.empty()) return false;
            p.push_back(atof(part.c_str()));
            if (b == std::string::npos) break;
            a = b + 1;
        }
        if (p.size() != 3) return false;
        deg = sign * (p[0] + p[1] / 60.0 + p[2] / 3600.0);
        return true;
    }
    char* e = nullptr;
    deg = strtod(s.c_str(), &e);
    return e && e != s.c_str();
}

static std::vector<std::string> SplitList(const std::string& s, char sep)
{
    std::vector<std::string> out;
    size_t a = 0;
    for (;;)
    {
        size_t b = s.find(sep, a);
        std::string t = TrimS(s.substr(a, b == std::string::npos ? std::string::npos : b - a));
        if (!t.empty()) out.push_back(t);
        if (b == std::string::npos) break;
        a = b + 1;
    }
    return out;
}

bool ParseStandsText(const std::string& text, std::map<std::string, std::vector<std::string>>& out,
                     std::map<std::string, std::map<std::string, StandInfo>>* info)
{
    size_t pos = 0;
    StandInfo* cur = nullptr;   // gerade gelesene Stand-Definition (Zusatzzeilen gehoeren zu ihr)
    while (pos < text.size())
    {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = TrimS(text.substr(pos, end - pos));
        pos = end + 1;
        if (line.empty() || line.compare(0, 2, "//") == 0) continue;
        std::string up = UpperS(line);
        if (up.compare(0, 6, "STAND:") == 0)
        {
            cur = nullptr;
            std::vector<std::string> f = SplitList(line.substr(6), ':');
            if (f.size() < 2) continue;
            std::string apt = UpperS(f[0]), name = UpperS(f[1]);
            if (apt.size() != 4 || name.empty() || name.size() > 8) continue;
            out[apt].push_back(name);
            if (!info) continue;
            StandInfo& si = (*info)[apt][name];
            si = StandInfo();
            if (f.size() >= 5 && ParseGrpCoord(f[2], si.lat) && ParseGrpCoord(f[3], si.lon))
            {
                si.radius = atof(f[4].c_str());
                si.hasPos = si.radius > 0;
            }
            cur = &si;
            continue;
        }
        if (!cur) continue;
        if (up.compare(0, 5, "COPY") == 0) { cur = nullptr; continue; }
        if (up.compare(0, 7, "BLOCKS:") == 0)
        {
            std::vector<std::string> f = SplitList(line.substr(7), ':');
            if (!f.empty())
                for (const std::string& b : SplitList(f[0], ',')) cur->blocks.push_back(UpperS(b));
        }
        else if (up.compare(0, 4, "WTC:") == 0) cur->wtc = UpperS(TrimS(line.substr(4)));
        else if (up.compare(0, 4, "USE:") == 0) cur->use = UpperS(TrimS(line.substr(4)));
        else if (up.compare(0, 9, "CALLSIGN:") == 0)
        {
            for (const std::string& part : SplitList(line.substr(9), ':'))
                for (const std::string& a : SplitList(part, ','))
                    if (!TrimS(a).empty()) cur->airlines.push_back(UpperS(TrimS(a)));
        }
        else if (up.compare(0, 9, "PRIORITY:") == 0) cur->priority = atoi(line.c_str() + 9);
        else if (up == "MANUAL") cur->manual = true;
        else if (up == "AREA") cur->area = true;
        else if (up.compare(0, 6, "COORD:") == 0)
        {
            std::vector<std::string> f = SplitList(line.substr(6), ':');
            double la, lo;
            if (f.size() >= 2 && ParseGrpCoord(f[0], la) && ParseGrpCoord(f[1], lo)) cur->poly.push_back({ la, lo });
        }
    }
    for (auto& kv : out)
    {
        std::sort(kv.second.begin(), kv.second.end(), NaturalLess);
        kv.second.erase(std::unique(kv.second.begin(), kv.second.end()), kv.second.end());
    }
    return !out.empty();
}

bool LoadStands(const std::wstring& path, std::map<std::string, std::vector<std::string>>& out,
                std::map<std::string, std::map<std::string, StandInfo>>* info)
{
    std::string text;
    if (!ReadAll(path, text)) return false;
    return ParseStandsText(text, out, info);
}

// ---------------------------------------------------------------------------
// Suchen
// ---------------------------------------------------------------------------
static bool SameName(const wchar_t* a, const wchar_t* b)
{
    return CompareStringOrdinal(a, -1, b, -1, TRUE) == CSTR_EQUAL;
}

static void Search(const std::wstring& dir, int depth, int& budget, std::wstring& airlines, std::wstring& airports,
                   std::wstring& stands, const std::atomic<bool>& stop)
{
    if (stop || budget <= 0 || (!airlines.empty() && !airports.empty() && !stands.empty())) return;
    --budget;

    std::vector<std::wstring> subdirs;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW((dir + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (h == INVALID_HANDLE_VALUE) return;
    do
    {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            if (fd.cFileName[0] == L'.') continue;   // ., .., .git usw.
            if (fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_REPARSE_POINT))
                continue;
            subdirs.push_back(dir + L"\\" + fd.cFileName);
        }
        else if (airlines.empty() && SameName(fd.cFileName, L"ICAO_Airlines.txt"))
            airlines = dir + L"\\" + fd.cFileName;
        else if (airports.empty() && SameName(fd.cFileName, L"ICAO_Airports.txt"))
            airports = dir + L"\\" + fd.cFileName;
        else if (stands.empty() && SameName(fd.cFileName, L"GRpluginStands.txt"))
            stands = dir + L"\\" + fd.cFileName;
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    if (depth <= 0) return;
    for (const std::wstring& sd : subdirs) Search(sd, depth - 1, budget, airlines, airports, stands, stop);
}

IcaoLoader::~IcaoLoader()
{
    Stop();
}

void IcaoLoader::Start(const std::vector<std::wstring>& roots)
{
    Stop();
    {
        std::lock_guard<std::mutex> lk(m_mx);
        m_ready = false;
    }
    m_stop = false;
    m_thread = std::thread(&IcaoLoader::Run, this, roots);
}

void IcaoLoader::Stop()
{
    if (!m_thread.joinable()) return;
    m_stop = true;
    m_thread.join();
}

bool IcaoLoader::TryTake(IcaoTables& out)
{
    std::lock_guard<std::mutex> lk(m_mx);
    if (!m_ready) return false;
    m_ready = false;
    out = std::move(m_result);
    m_result = IcaoTables();
    return true;
}

void IcaoLoader::Run(std::vector<std::wstring> roots)
{
    IcaoTables t;
    std::wstring airlines, airports, stands;
    int budget = 6000;   // so viele Ordner hoechstens, damit die Suche nie ewig laeuft
    for (const std::wstring& r : roots)
    {
        if (r.empty()) continue;
        DWORD attr = GetFileAttributesW(r.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) continue;
        Search(r, 6, budget, airlines, airports, stands, m_stop);
        if (m_stop || (!airlines.empty() && !airports.empty() && !stands.empty())) break;
    }
    if (!m_stop)
    {
        if (!airlines.empty() && LoadAirlines(airlines, t.airlines)) t.airlinesPath = airlines;
        if (!airports.empty() && LoadAirports(airports, t.airports)) t.airportsPath = airports;
        if (!stands.empty() && LoadStands(stands, t.stands, &t.standInfo)) t.standsPath = stands;
    }
    std::lock_guard<std::mutex> lk(m_mx);
    m_result = std::move(t);
    m_ready = true;
}

// ---------------------------------------------------------------------------
// Anzeige
// ---------------------------------------------------------------------------
static const char* kAlpha[26] = { "ALFA", "BRAVO", "CHARLIE", "DELTA", "ECHO", "FOXTROT", "GOLF", "HOTEL", "INDIA",
                                  "JULIETT", "KILO", "LIMA", "MIKE", "NOVEMBER", "OSCAR", "PAPA", "QUEBEC", "ROMEO",
                                  "SIERRA", "TANGO", "UNIFORM", "VICTOR", "WHISKEY", "X-RAY", "YANKEE", "ZULU" };
static const char* kDigit[10] = { "ZERO", "ONE", "TWO", "THREE", "FOUR", "FIVE", "SIX", "SEVEN", "EIGHT", "NINER" };

std::string SpokenCallsign(const std::string& cs, const std::map<std::string, std::string>& airlines)
{
    std::string u = UpperS(cs);
    // Airline: drei Buchstaben, danach eine Ziffer (DLH4AB, CFG4PN)
    if (u.size() > 3 && isalpha((unsigned char)u[0]) && isalpha((unsigned char)u[1]) && isalpha((unsigned char)u[2]) &&
        isdigit((unsigned char)u[3]))
    {
        auto it = airlines.find(u.substr(0, 3));
        if (it != airlines.end()) return UpperS(it->second) + " " + u.substr(3);
        return std::string();
    }
    // Sonst Kennzeichen buchstabieren (DIABC, N123AB)
    std::string out;
    for (char c : u)
    {
        const char* w = nullptr;
        if (c >= 'A' && c <= 'Z') w = kAlpha[c - 'A'];
        else if (c >= '0' && c <= '9') w = kDigit[c - '0'];
        if (!w) continue;
        if (!out.empty()) out += ' ';
        out += w;
    }
    return out;
}

std::string NiceName(const std::string& upper)
{
    std::string out = upper;
    bool start = true;
    for (char& c : out)
    {
        unsigned char uc = (unsigned char)c;
        if (isalpha(uc) || uc >= 0x80)
        {
            c = start ? (char)toupper(uc) : (char)tolower(uc);
            start = false;
        }
        else
        {
            start = (c == ' ' || c == '/' || c == '-' || c == '(' || c == '.');
        }
    }
    // Kleine Fuellwoerter klein lassen: "Palma de Mallorca", "Frankfurt am Main"
    static const char* small[] = { "De", "Del", "Da", "Do", "Dos", "Das", "La", "Le", "Les", "El", "Am", "An", "Im",
                                   "Of", "The", "Y", "Di" };
    size_t pos = 0;
    while ((pos = out.find(' ', pos)) != std::string::npos)
    {
        size_t s = pos + 1;
        size_t e = out.find_first_of(" /-", s);
        std::string w = out.substr(s, e == std::string::npos ? std::string::npos : e - s);
        for (const char* sm : small)
            if (w == sm) out[s] = (char)tolower((unsigned char)out[s]);
        pos = s;
    }
    return out;
}
