// StripBay - TSAT aus CDM-Quellen
//
// 1. CDM-Plugin (rpuig2001): schreibt "ASRT/TSAC/TOBT/TSAT/TTOT/..." in die
//    Flight-Strip-Annotation 0. Das liest das Plugin direkt aus EuroScope.
// 2. vACDM (VATGER): teilt nichts ueber EuroScope. Deshalb fragt der
//    VacdmClient im Hintergrund denselben Server ab wie das vACDM-Plugin.
#pragma once

#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

// Sekunden seit 1970 (UTC) aus "2026-10-25T17:05:00.000Z", 0 wenn ungueltig
long long ParseIsoUtc(const std::string& iso);

// Uhrzeit "HHMM" oder "HHMMSS" auf den naechstgelegenen Zeitpunkt (heute,
// gestern oder morgen) umrechnen, 0 wenn ungueltig
long long TimeOfDayToUtc(const std::string& hhmmss, long long nowUtc);

// "HHMM" aus einem UTC-Zeitpunkt
std::string FormatHHMM(long long utc);

// Antwort von GET /api/v1/pilots auswerten: Callsign -> TSAT (UTC)
// ttot/ctot optional: TTOT und CTOT (nur gueltige Zeiten, vACDM schickt sonst 1969)
bool ParseVacdmPilots(const std::string& body, std::map<std::string, long long>& tsat,
                      std::map<std::string, long long>* ttot = nullptr, std::map<std::string, long long>* ctot = nullptr);

// VATSIM-Datenfeed (v3) auswerten: ATIS-Rufzeichen -> Buchstabe, z. B. EDDB_ATIS -> B
bool ParseVatsimAtis(const std::string& body, std::map<std::string, std::string>& codes);

// Einfacher HTTP(S)-GET (WinHTTP). stop bricht laufende Downloads ab.
bool HttpGet(const std::string& url, std::string& body, std::string& error, const std::atomic<bool>* stop);
// GET (postData leer) oder POST mit Formulardaten (application/x-www-form-urlencoded)
bool HttpRequest(const std::string& url, const std::string& postData, std::string& body, std::string& error,
                 const std::atomic<bool>* stop, int receiveTimeoutMs = 6000);
// beliebige Methode (GET, POST, PATCH) mit Inhaltstyp; ok bei HTTP 2xx
bool HttpSend(const std::string& url, const wchar_t* method, const std::string& contentType, const std::string& data,
              std::string& body, std::string& error, const std::atomic<bool>* stop, int receiveTimeoutMs = 6000);
// "2026-11-05T12:05:00.000Z" aus einem UTC-Zeitpunkt
std::string FormatIsoUtc(long long utc);
// JSON fuer vACDM: TOBT setzen (wie das vACDM-Plugin: bestaetigt; liegt die neue TOBT nach der TSAT,
// wird die TSAT zurueckgesetzt, damit vACDM neu rechnet)
std::string VacdmTobtJson(const std::string& callsign, long long tobtUtc, long long tsatUtc);

// Update-Hinweis: fragt einmal im Hintergrund die neueste Version bei GitHub ab
//  https://api.github.com/repos/<owner>/<repo>/releases/latest -> "tag_name": "v4.2.0"
class UpdateChecker
{
public:
    ~UpdateChecker();
    void Start(const std::string& repo);
    void Stop();
    // true genau einmal, sobald die Abfrage fertig ist; tag leer = nicht erreichbar
    bool TryTake(std::string& tag);

private:
    std::thread       m_thread;
    std::atomic<bool> m_stop{ false };
    std::atomic<bool> m_done{ false };
    std::atomic<bool> m_running{ false };
    std::mutex        m_mx;
    std::string       m_tag;
};
// "tag_name" aus der GitHub-Antwort (ohne fuehrendes v), leer = nicht gefunden
inline std::string ParseReleaseTag(const std::string& body)
{
    size_t p = body.find("\"tag_name\"");
    if (p == std::string::npos) return std::string();
    p = body.find(':', p);
    if (p == std::string::npos) return std::string();
    p = body.find('"', p);
    if (p == std::string::npos) return std::string();
    size_t e = body.find('"', p + 1);
    if (e == std::string::npos) return std::string();
    std::string t = body.substr(p + 1, e - p - 1);
    if (!t.empty() && (t[0] == 'v' || t[0] == 'V')) t.erase(0, 1);
    if (t.empty() || t.size() > 20) return std::string();
    for (char c : t) if (!((c >= '0' && c <= '9') || c == '.')) return std::string();
    return t;
}
// Versionen "4.2.0" vergleichen: <0, 0, >0 (fehlende Teile = 0)
inline int CompareVersions(const std::string& a, const std::string& b)
{
    size_t i = 0, j = 0;
    for (int part = 0; part < 4; ++part)
    {
        long x = 0, y = 0;
        while (i < a.size() && a[i] >= '0' && a[i] <= '9') x = x * 10 + (a[i++] - '0');
        while (j < b.size() && b[j] >= '0' && b[j] <= '9') y = y * 10 + (b[j++] - '0');
        if (x != y) return x < y ? -1 : 1;
        if (i < a.size()) ++i;
        if (j < b.size()) ++j;
    }
    return 0;
}

// METAR und ATIS-Buchstaben im Hintergrund:
//  METAR:  https://metar.vatsim.net/<ICAO> (EuroScope liefert ihn zusaetzlich selbst)
//  ATIS:   https://data.vatsim.net/v3/vatsim-data.json, Feld "atis" -> atis_code
class WxClient
{
public:
    ~WxClient();
    void Start();
    void Stop();
    bool IsRunning() const { return m_thread.joinable(); }
    void SetAirports(const std::set<std::string>& airports);

    std::string GetMetar(const std::string& icao);
    // ATIS-Buchstaben eines Platzes: "B" oder bei getrennter ATIS "D C  A D"
    std::string GetAtis(const std::string& icao);
    std::string Status();

private:
    void Run();

    std::thread             m_thread;
    std::atomic<bool>       m_stop{ false };
    std::mutex              m_mx;
    std::condition_variable m_cv;
    std::set<std::string>              m_airports;
    std::map<std::string, std::string> m_metar;   // ICAO -> METAR
    std::map<std::string, std::string> m_atis;    // ATIS-Rufzeichen -> Buchstabe
    std::string                        m_lastError;
    long long                          m_lastOk = 0;
};

class VacdmClient
{
public:
    VacdmClient();
    ~VacdmClient();

    void Start();
    void Stop();
    bool IsRunning() const { return m_thread.joinable(); }

    void SetUrl(const std::string& baseUrl);
    void SetAirports(const std::set<std::string>& airports);
    std::string GetUrl();

    // TSAT eines Callsigns, 0 wenn keine bekannt
    long long GetTsat(const std::string& callsign);
    // TTOT / CTOT eines Callsigns, 0 wenn keine
    long long GetTtot(const std::string& callsign);
    long long GetCtot(const std::string& callsign);
    std::string LastError();
    // TOBT an vACDM melden (im Hintergrund, PATCH /api/v1/pilots/<cs>); Ergebnis in LastPatch()
    void SetTobt(const std::string& callsign, long long tobtUtc);
    std::string LastPatch();

    // Kurzer Status fuer ".strips cdm"
    std::string Status();

private:
    void Run();
    bool Fetch(const std::string& url, std::string& body, std::string& error);

    std::thread             m_thread;
    std::atomic<bool>       m_stop{ false };
    std::mutex              m_mx;
    std::condition_variable m_cv;

    // durch m_mx geschuetzt
    std::string                      m_url;
    std::set<std::string>            m_airports;
    std::map<std::string, long long> m_tsat;
    std::map<std::string, long long> m_ttot;
    std::map<std::string, long long> m_ctot;
    std::string                      m_lastError;
    long long                        m_lastOk = 0;
    int                              m_lastCount = 0;
    std::vector<std::pair<std::string, std::string>> m_patches;   // Callsign, JSON (warten aufs Senden)
    std::string                      m_lastPatch;
};
