// StripBay - EuroScope-Plugin
#include "StripBayPlugin.h"
#include "Tone.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <ctime>

using namespace EuroScopePlugIn;

static StripBayPlugin* g_plugin = nullptr;

extern "C" IMAGE_DOS_HEADER __ImageBase;

// ---------------------------------------------------------------------------
// Unsichtbarer Radarschirm. Nur noetig, um notfalls eingebaute
// EuroScope-Funktionen (Clearance-Flag) auszuloesen. Zeichnet nichts.
// ---------------------------------------------------------------------------
class AnchorScreen final : public CRadarScreen
{
public:
    virtual void OnAsrContentToBeClosed(void) override
    {
        if (g_plugin) g_plugin->ScreenClosed(this);
        delete this;
    }
};

// ---------------------------------------------------------------------------
// Helfer
// ---------------------------------------------------------------------------
static std::string S(const char* p) { return p ? std::string(p) : std::string(); }

static std::string Upper(std::string s)
{
    for (char& c : s) c = (char)toupper((unsigned char)c);
    return s;
}

static std::string Lower(std::string s)
{
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

static std::string Trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return std::string();
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static std::vector<std::string> Split(const std::string& s, const char* delims)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : s)
    {
        if (strchr(delims, c))
        {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        }
        else cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// Teilt auch leere Felder: "a//b" -> "a", "", "b"
static std::vector<std::string> SplitKeep(const std::string& s, char delim)
{
    std::vector<std::string> out(1);
    for (char c : s)
    {
        if (c == delim) out.emplace_back();
        else out.back() += c;
    }
    return out;
}

// Nur Buchstaben und Ziffern, gross: "ST-UP" -> "STUP"
static std::string NormState(const std::string& raw)
{
    std::string o;
    for (char c : raw)
        if (isalnum((unsigned char)c)) o += (char)toupper((unsigned char)c);
    return o;
}

static GroundCmd ParseGroundState(const std::string& raw)
{
    std::string n = NormState(raw);
    if (n.empty() || n == "NSTS") return GC_NSTS;
    if (n == "ONFREQ" || n == "ONFRQ" || n == "FREQ") return GC_ONFREQ;
    if (n == "STUP")              return GC_STUP;
    if (n == "PUSH")              return GC_PUSH;
    if (n == "TAXI")              return GC_TAXI;
    if (n == "LINEUP")            return GC_LINEUP;
    if (n == "DEPA")              return GC_DEPA;
    if (n == "TXIN" || n == "TAXIIN") return GC_TXIN;
    if (n == "PARK")              return GC_PARK;
    if (n == "ARR")               return GC_NSTS;   // Ankunft ohne Status
    return GC_UNKNOWN;            // z. B. DE-ICE
}

// Vergleichbarer Schluessel: "" und "NSTS" sind derselbe Zustand, "ST-UP" = "STUP"
static std::string CanonState(const std::string& raw)
{
    GroundCmd c = ParseGroundState(raw);
    return c == GC_UNKNOWN ? NormState(raw) : std::to_string((int)c);
}


// Kurzlebige Befehle anderer Plugins im Scratchpad. Die nie als "alten Inhalt"
// zurueckschreiben, sonst wuerden sie ein zweites Mal verteilt.
static bool IsTransientScratch(const std::string& sp)
{
    std::string u;
    for (char c : sp) u += (char)toupper((unsigned char)c);
    return u.compare(0, 6, "GRP/S/") == 0 || u.find(".VSID_") != std::string::npos || u.find(".SB_") != std::string::npos;
}

// Hold Short von StripBay: ".SB_HS_V1", ".SB_HS_NONE" (geloescht). false = keine solche Nachricht
static bool ParseHoldShort(const std::string& sp, std::string& text)
{
    size_t p = sp.find(".SB_HS_");
    if (p == std::string::npos) return false;
    std::string v;
    for (char c : sp.substr(p + 7))
    {
        if (!isalnum((unsigned char)c)) break;
        v += (char)toupper((unsigned char)c);
    }
    text = (v == "NONE") ? std::string() : v.substr(0, 6);
    return true;
}

// Uhrzeit mit fuehrender Null: "853" -> "0853" (auch "85300" -> "085300")
static std::string PadHHMM(const std::string& t)
{
    if (t.empty() || t.size() > 6) return t;
    for (char c : t) if (!isdigit((unsigned char)c)) return t;
    if (t.size() < 4) return std::string(4 - t.size(), '0') + t;
    if (t.size() == 5) return "0" + t;
    return t;
}

// Uebergaben von StripBay: ".SB_XF_TWR|GND|NONE" (Abfluege), ".SB_AP_RWY|REL|FIN" (Ankuenfte)
static bool ParseSbWord(const std::string& sp, const char* key, std::string& v)
{
    size_t p = sp.find(key);
    if (p == std::string::npos) return false;
    v.clear();
    for (char c : sp.substr(p + strlen(key)))
    {
        if (!isalnum((unsigned char)c)) break;
        v += (char)toupper((unsigned char)c);
    }
    return true;
}

// Wie ParseSbWord, aber mit "-" als Trenner (Rollweg: ".SB_TX_A-B-B3")
static bool ParseSbText(const std::string& sp, const char* key, std::string& v)
{
    size_t p = sp.find(key);
    if (p == std::string::npos) return false;
    v.clear();
    for (char c : sp.substr(p + strlen(key)))
    {
        if (!isalnum((unsigned char)c) && c != '-') break;
        v += (char)toupper((unsigned char)c);
    }
    return true;
}

// Rollweg vereinheitlichen: Grossbuchstaben und Ziffern, Teile mit einem Leerzeichen getrennt, hoechstens 24 Zeichen
static std::string NormTaxi(const std::string& text)
{
    std::string out, tok;
    for (char c : text + " ")
    {
        if (isalnum((unsigned char)c)) { tok += (char)toupper((unsigned char)c); continue; }
        if (tok.empty()) continue;
        if (out.size() + tok.size() + (out.empty() ? 0 : 1) > 24) break;
        out += (out.empty() ? "" : " ") + tok;
        tok.clear();
    }
    return out;
}

// eigene Version fuer ".SB_VN_": "4.2.0" -> "4X2X0"
static std::string VersionWire()
{
    std::string v = STRIPBAY_VERSION;
    for (char& c : v) if (c == '.') c = 'X';
    return v;
}

// "HHMMSS" (UTC) -> Zeitpunkt nahe bei now, sonst 0
static long long PlanFromHhmmss(const std::string& v, long long now)
{
    if (v.size() != 6) return 0;
    for (char c : v) if (!isdigit((unsigned char)c)) return 0;
    int h = atoi(v.substr(0, 2).c_str()), m = atoi(v.substr(2, 2).c_str()), sec = atoi(v.substr(4, 2).c_str());
    if (h > 23 || m > 59 || sec > 59) return 0;
    long long t = now - now % 86400 + h * 3600LL + m * 60LL + sec;
    if (t - now > 43200) t -= 86400;
    else if (now - t > 43200) t += 86400;
    return t;
}
static std::string Hhmmss(long long t)
{
    char b[8];
    snprintf(b, sizeof(b), "%02d%02d%02d", (int)(t % 86400 / 3600), (int)(t % 3600 / 60), (int)(t % 60));
    return b;
}

static bool Glob(const std::string& pat, const std::string& txt);
static double DistM(double la1, double lo1, double la2, double lo2);
static bool InPoly(const std::vector<std::pair<double, double>>& poly, double la, double lo);

// Stand passt zu einer Liste "W*, G1, A1*": Muster mit * bzw. ?, sonst gleich oder mit Buchstaben dahinter (W4A)
static bool StandInList(const std::string& list, const std::string& standIn)
{
    std::string stand;
    for (char c : standIn) stand += (char)toupper((unsigned char)c);
    if (stand.empty()) return false;
    std::string tok;
    auto test = [&](const std::string& t) {
        if (t.empty()) return false;
        if (t.find('*') != std::string::npos || t.find('?') != std::string::npos) return Glob(t, stand);
        return stand == t || (stand.compare(0, t.size(), t) == 0 && stand.size() > t.size() &&
                              isalpha((unsigned char)stand[t.size()]));
    };
    for (char c : list + ",")
    {
        if (c == ',' || c == ' ' || c == ';') { if (test(tok)) return true; tok.clear(); }
        else tok += (char)toupper((unsigned char)c);
    }
    return false;
}

// Durchstarten von StripBay: ".SB_GA_TRUE" / ".SB_GA_FALSE". -1 = keine solche Nachricht
static int ParseGoAround(const std::string& sp)
{
    if (sp.find(".SB_GA_TRUE") != std::string::npos) return 1;
    if (sp.find(".SB_GA_FALSE") != std::string::npos) return 0;
    return -1;
}

static std::string DlTime(long long t)
{
    time_t tt = (time_t)t;
    struct tm g;
#ifdef _WIN32
    gmtime_s(&g, &tt);
#else
    gmtime_r(&tt, &g);
#endif
    char b[8];
    snprintf(b, sizeof(b), "%02d%02dZ", g.tm_hour, g.tm_min);
    return b;
}

// Anfrage nach einer Departure Clearance (PDC/DCL), als Telex oder CPDLC
static bool IsDclRequest(const std::string& t)
{
    bool req = t.find("REQ") != std::string::npos;
    return t.find("PREDEP") != std::string::npos || t.compare(0, 3, "RCD") == 0 ||
           (req && (t.find("PDC") != std::string::npos || t.find("DCL") != std::string::npos ||
                    t.find("CLEARANCE") != std::string::npos || t.find("CLR") != std::string::npos));
}

// vSID verteilt das CTL-Flag als Scratchpad ".VSID_CTL_TRUE" / ".VSID_CTL_FALSE".
// Rueckgabe: 1 = gesetzt, 0 = geloescht, -1 = keine CTL-Nachricht
static int ParseVsidCtl(const std::string& sp)
{
    size_t p = sp.find(".VSID_CTL_");
    if (p == std::string::npos) return -1;
    std::string rest = sp.substr(p + 10);
    if (rest.compare(0, 4, "TRUE") == 0) return 1;
    if (rest.compare(0, 5, "FALSE") == 0) return 0;
    return -1;
}

// vSID-Handover-Flag ".VSID_HOV_TRUE" / ".VSID_HOV_FALSE": 1, 0 oder -1
static int ParseVsidHov(const std::string& sp)
{
    size_t p = sp.find(".VSID_HOV_");
    if (p == std::string::npos) return -1;
    std::string rest = sp.substr(p + 10);
    if (rest.compare(0, 4, "TRUE") == 0) return 1;
    if (rest.compare(0, 5, "FALSE") == 0) return 0;
    return -1;
}

// vSID-Intersection ".VSID_INT_L6_TRUE" (zugewiesen), "_FALSE" (able for),
// ".VSID_INT_NONE_FALSE" (keine). Rueckgabe false, wenn keine solche Nachricht.
static bool ParseVsidInt(const std::string& sp, std::string& name, bool& assigned)
{
    size_t p = sp.find(".VSID_INT_");
    if (p == std::string::npos) return false;
    std::vector<std::string> f = SplitKeep(sp.substr(p + 10), '_');
    if (f.empty() || f[0].empty()) return false;
    name = (f[0] == "NONE") ? std::string() : f[0].substr(0, 3);
    assigned = !name.empty() && f.size() > 1 && f[1].compare(0, 4, "TRUE") == 0;
    return true;
}

// Eingabe wie in vSID: "+L6" = zugewiesen, "-L6" = able for (Scratchpad hoechstens 4 Zeichen)
static bool ParsePlusMinusInt(const std::string& sp, std::string& name, bool& assigned)
{
    std::string t = Trim(sp);
    if (t.size() < 2 || t.size() > 4 || (t[0] != '+' && t[0] != '-')) return false;
    if (!isalpha((unsigned char)t[1])) return false;   // "-5" ist keine Intersection
    for (size_t i = 1; i < t.size(); ++i)
        if (!isalnum((unsigned char)t[i])) return false;
    name = Upper(t.substr(1));
    assigned = (t[0] == '+');
    return true;
}

// Gelandet: am Platz, langsam und weder im Steig- noch im Sinkflug.
// EuroScope kennt kein "am Boden", deshalb diese drei Merkmale zusammen.
static bool OnGround(double distDest, int gs, int vs)
{
    return distDest <= 4.0 && gs <= 60 && vs > -400 && vs < 400;
}

// Parkposition aus "GRP/S/<stand>", auch mitten in einem Text und egal ob gross/klein
static std::string ParseGrpStand(const std::string& text)
{
    std::string up;
    for (char c : text) up += (char)toupper((unsigned char)c);
    size_t p = up.find("GRP/S/");
    if (p == std::string::npos) return std::string();
    std::string rest = text.substr(p + 6);
    size_t end = rest.find_first_of("/ \t\r\n");
    return Trim(rest.substr(0, end));
}

// Allgemeiner: ".../S/<stand>/..." (so liest auch FlightStrips die Annotation 6).
// Die TopSky-Holding-Daten in derselben Annotation ("h/.../h", "t/.../t") stoeren nicht.
static std::string ParseSlashStand(const std::string& text)
{
    std::vector<std::string> seg;
    std::string cur;
    for (char c : text)
    {
        if (c == '/') { seg.push_back(cur); cur.clear(); }
        else cur += c;
    }
    seg.push_back(cur);
    for (size_t i = 0; i + 1 < seg.size(); ++i)
    {
        std::string a = Trim(seg[i]);
        std::string b = Trim(seg[i + 1]);
        if ((a == "S" || a == "s") && !b.empty() && b.size() <= 8) return b;
    }
    return std::string();
}

// Scratchpad-Texte, die EuroScope als Ground State auswertet.
// Fuer Startup gibt es je nach ES-Version zwei Schreibweisen.
static const char* const kScratch[GC_COUNT][3] = {
    { "NSTS",   nullptr, nullptr },
    { "ONFREQ", nullptr, nullptr },
    { "ST-UP",  "STUP",  nullptr },
    { "PUSH",   nullptr, nullptr },
    { "TAXI",   nullptr, nullptr },
    { "LINEUP", nullptr, nullptr },
    { "DEPA",   nullptr, nullptr },
    { "TXIN",   "TAXIIN", nullptr },
    { "PARK",   nullptr, nullptr },
};

// Ground-State-Befehl im Scratchpad ("ST-UP", "PUSH" ...)? Der ist nie der echte Scratchpad-Text:
// ihn spaeter "zurueckzuschreiben" wuerde den Status wieder umstellen (z. B. Startup -> Pending).
static bool IsGsCommand(const std::string& sp)
{
    std::string u;
    for (char c : sp) u += (char)toupper((unsigned char)c);
    while (!u.empty() && u.back() == ' ') u.pop_back();
    for (int i = 0; i < GC_COUNT; ++i)
        for (int k = 0; k < 3 && kScratch[i][k]; ++k)
            if (u == kScratch[i][k]) return true;
    return false;
}

static const char* kStateName[GC_COUNT] = { "Pending", "On Freq", "Startup", "Push", "Taxi", "Line-up", "Departure",
                                            "Taxi In", "Parked" };

// Quelltext ist UTF-8, EuroScope erwartet ANSI
static std::string Utf8ToAcp(const std::string& utf8)
{
    int wn = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (wn <= 0) return utf8;
    std::wstring w(wn, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &w[0], wn);
    int an = WideCharToMultiByte(CP_ACP, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (an <= 0) return utf8;
    std::string a(an, '\0');
    WideCharToMultiByte(CP_ACP, 0, w.c_str(), -1, &a[0], an, nullptr, nullptr);
    a.resize(an - 1);
    return a;
}

// Umgekehrt: Text aus EuroScope (ANSI) fuer Msg()
static std::string AcpToUtf8(const std::string& acp)
{
    if (acp.empty()) return acp;
    int wn = MultiByteToWideChar(CP_ACP, 0, acp.c_str(), -1, nullptr, 0);
    if (wn <= 0) return acp;
    std::wstring w(wn, L'\0');
    MultiByteToWideChar(CP_ACP, 0, acp.c_str(), -1, &w[0], wn);
    int un = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (un <= 0) return acp;
    std::string u(un, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &u[0], un, nullptr, nullptr);
    u.resize(un - 1);
    return u;
}

// Gross, auch Umlaute (ANSI): "süd" -> "SÜD"
static std::string UpperAcp(const std::string& s)
{
    std::string o = s;
    if (!o.empty()) CharUpperBuffA(&o[0], (DWORD)o.size());
    return o;
}

// "24R", "24 R", "rwy24r" -> "24R"
static std::string NormRwy(const std::string& raw)
{
    std::string o;
    for (char c : raw)
        if (isalnum((unsigned char)c)) o += (char)toupper((unsigned char)c);
    if (o.compare(0, 3, "RWY") == 0) o = o.substr(3);
    if (o.size() == 1 || (o.size() == 2 && isalpha((unsigned char)o[1]))) o = "0" + o;   // "6L" -> "06L"
    return o;
}

static std::string JoinSet(const std::set<std::string>& s, const char* sep)
{
    std::string o;
    for (const std::string& x : s)
    {
        if (!o.empty()) o += sep;
        o += x;
    }
    return o;
}

// ---------------------------------------------------------------------------
// Aufbau
// ---------------------------------------------------------------------------
StripBayPlugin::StripBayPlugin()
    : CPlugIn(COMPATIBILITY_CODE, STRIPBAY_NAME, STRIPBAY_VERSION, STRIPBAY_AUTHOR, STRIPBAY_COPYRIGHT),
      m_window(this)
{
    g_plugin = this;
    // festgelegtes Platz-Profil (sonst automatisch, bis dahin Berlin)
    if (const char* v = GetDataFromSettings("Profile"))
    {
        std::string pr = Upper(Trim(S(v)));
        if (pr.size() == 4) m_profileForced = m_profileIcao = pr;
    }
    LoadAirportFile(false);   // eigene Plaetze (StripBayAirports.txt)
    SetRunwayDefaults();
    SetFreqDefaults();

    WindowSettings ws;
    LoadSettings(ws);
    if (!m_window.Create(ws))
        Msg("Window could not be created.");
    m_lastRwyFilter = m_window.GetRwyFilter();
    RemoteSetup();

    RefreshAirports();
    RefreshRunways();
    UpdateVacdm();
    UpdateWx();
    UpdateDatalink();
    StartIcaoSearch();
    PushBoard();
    Msg(std::string("Version ") + STRIPBAY_VERSION + " loaded. Help: .strips help");
}

StripBayPlugin::~StripBayPlugin()
{
    g_plugin = nullptr;
    m_vacdm.Stop();
    m_wx.Stop();
    m_upd.Stop();
    m_hoppie.Stop();
    m_icaoLoader.Stop();
    m_hub.log = nullptr;       // beim Entladen nichts mehr melden
    m_hub.changed = nullptr;
    m_hub.host = nullptr;
    m_net.Stop();
    m_window.Destroy();
}

// Automatische Meldungen (Timer, Netz): gleiche Meldung hoechstens einmal je Minute, insgesamt
// hoechstens 4 je 10 s (sonst springt der EuroScope-Chat staendig auf)
void StripBayPlugin::MsgAuto(const std::string& utf8)
{
    if (!m_inCommand)
    {
        auto seen = m_msgSeen.find(utf8);
        if (seen != m_msgSeen.end() && m_tick - seen->second < 60) return;
        if (m_msgWin < 0 || m_tick - m_msgWin >= 10) { m_msgWin = m_tick; m_msgCount = 0; }
        if (++m_msgCount > 4) return;
        m_msgSeen[utf8] = m_tick;
        if (m_msgSeen.size() > 500) m_msgSeen.clear();
    }
    Msg(utf8);
}

void StripBayPlugin::Log(const std::string& line)
{
    if (!m_logOn) return;
    time_t t = time(nullptr);
    struct tm g = {};
#ifdef _WIN32
    gmtime_s(&g, &t);
#else
    gmtime_r(&t, &g);
#endif
    char ts[16];
    snprintf(ts, sizeof(ts), "%02d:%02d:%02dZ ", g.tm_hour, g.tm_min, g.tm_sec);
    m_log.push_back(ts + line);
    while (m_log.size() > 5000) m_log.pop_front();
    m_logDirty = true;
}

void StripBayPlugin::FlushLog()
{
    if (!m_logDirty) return;
    m_logDirty = false;
    std::string out = "; StripBay " STRIPBAY_VERSION " log\r\n";
    for (const std::string& l : m_log) out += l + "\r\n";
    std::wstring dir = PluginDir();
    WriteTextFileW((dir.empty() ? std::wstring() : dir + L"\\") + L"StripBay.log", out);
}

void StripBayPlugin::Msg(const std::string& utf8)
{
    Log("MSG " + utf8);
    {
        time_t tt = time(nullptr);
        char ts[16];
        snprintf(ts, sizeof(ts), "%02d:%02d:%02dZ ", (int)(tt % 86400 / 3600), (int)(tt % 3600 / 60), (int)(tt % 60));
        m_recentMsgs.push_back(ts + utf8);
        while (m_recentMsgs.size() > 80) m_recentMsgs.pop_front();
    }
    std::string text = Utf8ToAcp(utf8);
    DisplayUserMessage(STRIPBAY_NAME, STRIPBAY_NAME, text.c_str(), true, true, false, false, false);
    m_hub.Note(utf8);   // auch auf dem Remote-Geraet zeigen
}

// ---------------------------------------------------------------------------
// EuroScope-Ereignisse
// ---------------------------------------------------------------------------
void StripBayPlugin::OnTimer(int Counter)
{
    ++m_tick;
    ProcessSync();
    ProcessAutoStands();
    ProcessAutoSquawks();
    if (m_tick % 5 == 0) ScanControllers();
    Announce();
    ProcessUpdate();
    if (m_tick % 10 == 1) SyncGrpStands();
    // hoechstens einmal je Minute (Lotsen, die kurz weg und wieder da sind, loesen sonst
    // staendig neue Scratchpad-Runden aus)
    if (m_logOn && m_tick - m_logFlushed >= 5) { m_logFlushed = m_tick; FlushLog(); }
    // Angebot an mich zu lange nicht angenommen: einmal Erinnerungston
    if (m_offerRemind > 0 && m_respOn)
        for (const auto& of : m_offer)
        {
            if (m_offerByMe.count(of.first) || m_offerReminded.count(of.first) || !UnitIsMine(of.second)) continue;
            auto at = m_offerAt.find(of.first);
            if (at == m_offerAt.end() || m_tick - at->second < m_offerRemind) continue;
            m_offerReminded.insert(of.first);
            PlayEventSound("offer");
            MsgAuto(of.first + ": handover from " + (EffResp(of.first).empty() ? std::string("?") : EffResp(of.first)) + " waiting for you.");
        }
    // nach dem Login (verbunden, Streifen da): einmal um die Merker der anderen bitten
    if (!m_syncAsked && m_tick > 5 && !m_knownCtrls.empty())
    {
        CController me = ControllerMyself();
        if (me.IsValid() && me.IsController())
            for (const auto& kv : m_entries)
            {
                if (kv.second.freeStrip) continue;
                CFlightPlan f = FlightPlanSelect(kv.first.c_str());
                // nur ueber einen Flieger, dessen Scratchpad ich setzen darf (nicht von anderen getrackt)
                if (!f.IsValid() || (!f.GetTrackingControllerIsMe() && !Trim(S(f.GetTrackingControllerCallsign())).empty())) continue;
                SendScratch(kv.first, ".SB_SQ_TRUE");
                m_syncAsked = true;
                break;
            }
    }
    if (m_resyncAt >= 0 && m_tick >= m_resyncAt && (m_resyncForce || m_lastResync < 0 || m_tick - m_lastResync >= 60))
    {
        m_resyncAt = -1;
        m_resyncForce = false;
        m_lastResync = m_tick;
        Resync();
    }
    if (m_autoAirports && Counter % 10 == 0) RefreshAirports();
    if (Counter % 10 == 0) RefreshRunways();
    if (Counter % 10 == 0) UpdateVacdm();
    if (Counter % 10 == 0) UpdateWx();
    if (Counter % 10 == 0) UpdateDatalink();
    if (m_hoppie.IsRunning()) DlProcess();

    // ICAO-Daten fertig gesucht?
    IcaoTables t;
    if (m_icaoLoader.TryTake(t))
    {
        m_icao = std::move(t);
        m_icaoDone = true;
        m_grpStandsStamp.clear();   // gleich wieder die Stands des geladenen GRP nehmen
        bool complete = !m_icao.airlines.empty() && !m_icao.airports.empty();
        if (m_icaoReport || !complete)
        {
            char buf[96];
            snprintf(buf, sizeof(buf), "ICAO data: %d airlines, %d airports.", (int)m_icao.airlines.size(),
                     (int)m_icao.airports.size());
            Msg(buf);
            if (!complete)
                Msg("ICAO_Airlines.txt or ICAO_Airports.txt not found. Set the folder: .strips icao C:\\path\\to\\package");
        }
        m_icaoReport = false;
    }
    Sync();
}

void StripBayPlugin::OnFlightPlanControllerAssignedDataUpdate(CFlightPlan FlightPlan, int DataType)
{
    if (!FlightPlan.IsValid()) return;
    std::string cs = S(FlightPlan.GetCallsign());

    // Das Ground Radar Plugin verteilt Parkpositionen kurz als Scratchpad "GRP/S/<stand>"
    if (DataType == CTR_DATA_TYPE_SCRATCH_PAD_STRING)
    {
        std::string sp = S(FlightPlan.GetControllerAssignedData().GetScratchPadString());
        if (!IsTransientScratch(sp) && !IsGsCommand(sp) && !m_sync.count(cs) && !m_applyingGs) m_realSp[cs] = sp;
        if (m_logOn && (IsTransientScratch(sp) || IsGsCommand(sp))) Log(std::string(IsOwnSync(cs, sp) ? "ECHO " : "IN ") + cs + " " + sp);
        // eigene, gerade gesendete Nachricht: Zustand ist lokal schon gesetzt (und evtl.
        // inzwischen weiter geaendert) -> nicht noch einmal auswerten
        if (IsOwnSync(cs, sp)) sp.clear();
        if (!sp.empty()) HistSync(cs, sp, false);

        // Merker eines anderen Lotsen: er ist neuer als alles, was bei mir fuer denselben Merker noch in der
        // Warteschlange wartet (z. B. aus einem Resync) - das Veraltete wird nicht mehr gesendet, sonst
        // ueberschreibt es den neuen Stand bei allen. Eine neue Zustaendigkeit erledigt auch ein wartendes Angebot.
        // (Nicht fuer Meldungen, die je Lotse verschieden sind: Stelle an/ab, Bitte um Resync.)
        if (sp.size() > 7 && sp.compare(0, 4, ".SB_") == 0 && sp.compare(0, 7, ".SB_UN_") != 0 && sp.compare(0, 7, ".SB_UX_") != 0 &&
            sp.compare(0, 7, ".SB_SQ_") != 0 && sp.compare(0, 7, ".SB_VN_") != 0)
        {
            auto q = m_sync.find(cs);
            if (q != m_sync.end())
            {
                std::deque<std::string>& m = q->second.msgs;
                const bool newResp = sp.compare(0, 7, ".SB_RS_") == 0;
                for (size_t i = m.size(); i-- > (q->second.busy ? 1u : 0u);)
                    if (m[i].compare(0, 7, sp, 0, 7) == 0 || (newResp && m[i].compare(0, 7, ".SB_HO_") == 0 && m[i] != ".SB_HO_NONE"))
                        m.erase(m.begin() + (std::ptrdiff_t)i);
            }
        }

        // CTL-Flag von vSID (auch von anderen Lotsen)
        int ctl = ParseVsidCtl(sp);
        if (ctl >= 0)
        {
            m_ctl[cs] = (ctl == 1);
        }

        // Handover-Flag (weggeschickt) und Intersection von vSID
        int hov = ParseVsidHov(sp);
        if (hov >= 0) m_hov[cs] = (hov == 1);
        int ga = ParseGoAround(sp);
        if (ga >= 0) m_ga[cs] = (ga == 1);
        std::string xv;
        if (ParseSbWord(sp, ".SB_XF_", xv))
        {
            if (xv == "TWR" || xv == "GND") m_xfer[cs] = xv;
            else m_xfer.erase(cs);
        }
        if (ParseSbWord(sp, ".SB_AP_", xv) && (xv == "RWY" || xv == "REL" || xv == "FIN")) m_arrPos[cs] = xv;
        // On Freq (wenn EuroScope ONFREQ nicht kennt)
        if (ParseSbWord(sp, ".SB_OF_", xv))
        {
            if (xv == "TRUE") m_onFreq[cs] = true;
            else m_onFreq.erase(cs);
        }
        // Stelle mit StripBay (und Zustaendigkeit an) meldet sich bzw. ab: an sie gehen Uebergaben als Angebot
        if (ParseSbWord(sp, ".SB_UN_", xv) && IsRespUnit(xv)) m_sbUnits[xv] = m_tick;
        if (ParseSbWord(sp, ".SB_UX_", xv)) m_sbUnits.erase(xv);
        // ein Lotse hat sich eingeloggt und bittet um die Merker
        if (ParseSbWord(sp, ".SB_SQ_", xv) && m_tick - m_lastSqAnswer >= 30)
        {
            m_lastSqAnswer = m_tick;
            m_resyncAt = m_tick + 2 + (long long)(m_tick % 4);   // etwas gestreut, nicht alle gleichzeitig
            m_resyncForce = true;
            m_announceAgain = true;
        }
        // anderer Lotse hat bewusst auf "kein Status" gesetzt
        if (ParseSbWord(sp, ".SB_NS_", xv)) { m_lastGs.erase(cs); m_gsRestore.erase(cs); m_nsAt[cs] = (long long)m_tick; }
        // von Hand Inbound/Outbound bzw. Diversion
        if (ParseSbWord(sp, ".SB_IO_", xv) && (xv == "ARR" || xv == "DEP")) SetDirection(cs, xv == "ARR", false);
        if (ParseSbWord(sp, ".SB_DV_", xv)) SetDivert(cs, xv == "NONE" ? std::string() : xv, false);
        // Hervorhebung (Farbe 0-5)
        if (ParseSbWord(sp, ".SB_HL_", xv))
        {
            int k = atoi(xv.c_str());
            if (k > 0 && k <= 5) m_hl[cs] = k; else m_hl.erase(cs);
        }
        // Line-up (wenn EuroScope LINEUP nicht kennt)
        if (ParseSbWord(sp, ".SB_LU_", xv))
        {
            if (xv == "TRUE") m_lineup[cs] = true;
            else m_lineup.erase(cs);
        }
        // Taxi In bis zur Parkposition freigegeben (Haekchen)
        if (ParseSbWord(sp, ".SB_SC_", xv))
        {
            if (xv == "TRUE") m_stdClr[cs] = true;
            else m_stdClr.erase(cs);
        }
        // Zustaendigkeit (DEL, APN, GND, TWR, je mit N/S)
        if (ParseSbWord(sp, ".SB_RS_", xv))
        {
            auto pr = m_resp.find(cs);
            std::string prev = pr == m_resp.end() ? std::string() : pr->second;
            if (IsRespUnit(xv)) m_resp[cs] = xv;
            else if (xv == "NONE") m_resp.erase(cs);
            // war meins und ist jetzt bei einer anderen Stelle: noch kurz grau zu sehen, dann weg
            if (IsRespUnit(xv) && !prev.empty() && UnitIsMine(prev) && !UnitIsMine(xv) && !m_handedAt.count(cs)) m_handedAt[cs] = m_tick;
            if (IsRespUnit(xv) && UnitIsMine(xv)) m_handedAt.erase(cs);
            // angenommen: Angebot erledigt
            auto of = m_offer.find(cs);
            if (of != m_offer.end() && (xv == of->second || xv == "NONE")) { m_offer.erase(of); m_offerByMe.erase(cs); }
        }
        // Zeitstrahl: geplante Bahnzeit (Reihenfolge per Ziehen), HHMMSS UTC
        if (ParseSbWord(sp, ".SB_TP_", xv))
        {
            long long pt = PlanFromHhmmss(xv, (long long)time(nullptr));
            if (pt > 0) m_tlPlan[cs] = pt;
            else m_tlPlan.erase(cs);
        }
        // Push-Richtung (Nase)
        if (ParseSbWord(sp, ".SB_PB_", xv))
        {
            if (xv == "N" || xv == "E" || xv == "S" || xv == "W") m_pushDir[cs] = xv;
            else m_pushDir.erase(cs);
        }
        // Rollweg (Teile mit "-" getrennt) und Enteisung
        if (ParseSbText(sp, ".SB_TX_", xv))
        {
            std::string rt = xv == "NONE" ? std::string() : NormTaxi(xv);
            if (rt.empty()) m_taxi.erase(cs);
            else m_taxi[cs] = rt;
        }
        if (ParseSbWord(sp, ".SB_DI_", xv))
        {
            int k = atoi(xv.c_str());
            if (k == 1 || k == 2) m_deice[cs] = k; else m_deice.erase(cs);
        }
        // Version einer anderen StripBay; Meldungen, die es nur bis 3.x gab
        if (ParseSbWord(sp, ".SB_VN_", xv)) VersionSeen(xv);
        if (sp.find(".SB_RQ_") != std::string::npos || sp.find(".SB_AK_") != std::string::npos) VersionSeen("3");
        // Angebot abgelehnt (von Stelle xv): beim Anbietenden 30 s rot
        if (ParseSbWord(sp, ".SB_HX_", xv))
        {
            if (m_offerByMe.count(cs))
            {
                m_rejectedAt[cs] = m_tick;
                m_rejectedBy[cs] = xv;
                MsgAuto(cs + ": handover rejected by " + xv + ".");
                PlayEventSound("unable");
                // der Streifen steht wieder bei mir: Abflug aus der Uebergabe-Bay zurueck nach Taxi
                if (m_xfer.count(cs)) SendXfer(cs, "NONE");
            }
            m_offer.erase(cs);
            m_offerByMe.erase(cs);
            m_offerAt.erase(cs);
            m_offerReminded.erase(cs);
        }
        // Uebergabe angeboten (an Stelle xv) bzw. zurueckgenommen/erledigt (NONE)
        if (ParseSbWord(sp, ".SB_HO_", xv))
        {
            if (IsRespUnit(xv))
            {
                if (!m_offer.count(cs) || m_offer[cs] != xv) { m_offerAt[cs] = m_tick; m_offerReminded.erase(cs); }
                m_offer[cs] = xv;
                m_offerByMe.erase(cs);
            }
            else { m_offer.erase(cs); m_offerByMe.erase(cs); m_offerReminded.erase(cs); }
        }
        std::string hs;
        if (ParseHoldShort(sp, hs))
        {
            if (hs.empty()) m_hs.erase(cs);
            else m_hs[cs] = hs;
        }
        std::string intName;
        bool intSet = false;
        if (ParseVsidInt(sp, intName, intSet) || ParsePlusMinusInt(sp, intName, intSet))
            m_int[cs] = { intName, intSet };

        std::string stand = ParseGrpStand(sp);
        if (!stand.empty())
        {
            m_standLive[cs] = stand;
            m_standSrc[cs] = "GRP broadcast";
            m_standSp[cs] = stand;
        }
    }

    auto it = m_entries.find(cs);
    if (it == m_entries.end()) return;
    UpdateEntry(FlightPlan, it->second.arrival);
    PushBoard();
}

void StripBayPlugin::OnFlightPlanFlightPlanDataUpdate(CFlightPlan FlightPlan)
{
    if (!FlightPlan.IsValid()) return;
    std::string cs = S(FlightPlan.GetCallsign());
    auto it = m_entries.find(cs);
    if (it == m_entries.end()) return;
    // Platz geaendert? Dann entscheidet der naechste Sync neu.
    bool arrival = it->second.arrival;
    std::string apt = Upper(Trim(S(arrival ? FlightPlan.GetFlightPlanData().GetDestination()
                                           : FlightPlan.GetFlightPlanData().GetOrigin())));
    if (!apt.empty() && !(arrival ? m_arrAirports : m_airports).count(apt))
        RemoveEntry(cs);
    else
        UpdateEntry(FlightPlan, arrival);
    PushBoard();
}

void StripBayPlugin::OnFlightPlanDisconnect(CFlightPlan FlightPlan)
{
    ForgetFlight(S(FlightPlan.GetCallsign()));
    PushBoard();
}

// Flieger komplett vergessen: Streifen, Recover-Liste, alle synchronisierten Merker
void StripBayPlugin::ForgetFlight(const std::string& cs)
{
    auto de = m_entries.find(cs);
    if (de != m_entries.end() && !de->second.freeStrip) RemoveEntry(cs);
    m_hidden.erase(cs);
    m_parked.erase(cs);
    m_trashOrder.erase(std::remove(m_trashOrder.begin(), m_trashOrder.end(), cs), m_trashOrder.end());
    m_resp.erase(cs);
    m_onFreq.erase(cs);
    m_stdClr.erase(cs);
    m_hl.erase(cs);
    m_handedAt.erase(cs);
    m_lineup.erase(cs);
    m_divert.erase(cs);
    m_ioManual.erase(cs);
    m_lastGs.erase(cs);
    m_gsRestore.erase(cs);
    m_nsAt.erase(cs);
    m_hist.erase(cs);
    m_histLast.erase(cs);
    m_gsByMeAt.erase(cs);
    m_realSp.erase(cs);
    m_offer.erase(cs);
    m_offerByMe.erase(cs);
    m_offerAt.erase(cs);
    m_offerReminded.erase(cs);
    m_rejectedAt.erase(cs);
    m_rejectedBy.erase(cs);
    m_pushDir.erase(cs);
    m_tlPlan.erase(cs);
    m_pinned.erase(cs);
    m_hiddenArr.erase(cs);
    m_forced.erase(cs);
    m_standLive.erase(cs);
    m_standAnn.erase(cs);
    m_standSrc.erase(cs);
    m_standSp.erase(cs);
    m_ctl.erase(cs);
    m_hov.erase(cs);
    m_int.erase(cs);
    m_hs.erase(cs);
    m_taxi.erase(cs);
    m_deice.erase(cs);
    m_ga.erase(cs);
    m_dl.erase(cs);
    m_xfer.erase(cs);
    m_arrPos.erase(cs);
    m_parkPlan.erase(cs);
}

void StripBayPlugin::OnNewMetarReceived(const char* sStation, const char* sFullMetar)
{
    std::string st = Upper(Trim(S(sStation)));
    std::string m = Trim(S(sFullMetar));
    if (st.empty() || m.empty()) return;
    m_metarEs[st] = m;
    PushBoard();
}

// Welcher Platz steht oben? Der geteilte Platz (Berlin), sonst der erste aktive
std::string StripBayPlugin::WxAirport() const
{
    if (m_airports.count(m_profileIcao) || m_arrAirports.count(m_profileIcao)) return m_profileIcao;
    if (!m_airports.empty()) return *m_airports.begin();
    if (!m_arrAirports.empty()) return *m_arrAirports.begin();
    return std::string();
}

void StripBayPlugin::UpdateWx()
{
    if (!m_wxEnabled)
    {
        m_wx.Stop();
        return;
    }
    std::set<std::string> apts;
    std::string a = WxAirport();
    if (!a.empty()) apts.insert(a);
    m_wx.SetAirports(apts);
    m_wx.Start();
}

void StripBayPlugin::OnAirportRunwayActivityChanged(void)
{
    RefreshAirports();
    RefreshRunways();
    Sync();
}

CRadarScreen* StripBayPlugin::OnRadarScreenCreated(const char*, bool, bool, bool, bool)
{
    AnchorScreen* s = new AnchorScreen();
    m_screens.push_back(s);
    return s;
}

void StripBayPlugin::ScreenClosed(AnchorScreen* screen)
{
    m_screens.erase(std::remove(m_screens.begin(), m_screens.end(), screen), m_screens.end());
}

// ---------------------------------------------------------------------------
// Synchronisation
// ---------------------------------------------------------------------------
void StripBayPlugin::RefreshAirports()
{
    if (!m_autoAirports) { ChooseProfile(); return; }

    std::set<std::string> apts, arr;
    for (CSectorElement el = SectorFileElementSelectFirst(SECTOR_ELEMENT_AIRPORT); el.IsValid();
         el = SectorFileElementSelectNext(el, SECTOR_ELEMENT_AIRPORT))
    {
        std::vector<std::string> parts = Split(S(el.GetName()), " \t");
        if (parts.empty()) continue;
        if (el.IsElementActive(true)) apts.insert(Upper(parts[0]));    // aktiv fuer Abfluege
        if (el.IsElementActive(false)) arr.insert(Upper(parts[0]));    // aktiv fuer Ankuenfte
    }

    // Nichts aktiv: Flughafen aus dem eigenen Callsign nehmen (EDDF_GND -> EDDF)
    std::string own;
    CController me = ControllerMyself();
    if (me.IsValid())
    {
        std::string cs = Upper(S(me.GetCallsign()));
        std::string prefix = cs.substr(0, cs.find('_'));
        if (prefix.size() == 4) own = prefix;
    }
    if (apts.empty() && !own.empty()) apts.insert(own);
    if (arr.empty() && !own.empty()) arr.insert(own);
    // Platz in den Einstellungen gewaehlt (z. B. Hannover): nur dessen Flieger
    if (!m_profileForced.empty())
    {
        apts = { m_profileForced };
        arr = { m_profileForced };
    }

    if (apts != m_airports || arr != m_arrAirports)
    {
        m_airports = apts;
        m_arrAirports = arr;
        m_vacdm.SetAirports(m_airports);
        PushBoard();
    }
    ChooseProfile();
}

// ---------------------------------------------------------------------------
// Getrennte Pisten (z. B. Berlin: Nord und Sued unabhaengig betrieben)
// Massgeblich ist immer die Piste im Tag: GetDepartureRwy bzw. GetArrivalRwy.
// Aendert jemand sie (Tag, vSID, anderer Lotse), wandert der Streifen mit.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Platz-Profile: was an einem Platz anders ist (Pisten-Aufteilung, Apron ja/nein,
// Frequenzketten, Haltepunkte, automatische Uebergabe). Alles Uebrige ist gleich.
// ---------------------------------------------------------------------------
struct AirportProfile
{
    const char* icao;
    const char* city;
    bool split;                         // zwei Tower / getrennte Bahnen
    const char* rwyA;                   // Nordbahn (Pisten mit Komma)
    const char* rwyB;                   // Suedbahn
    const char* fixedRwys;              // ohne Sektordaten
    bool hasApron;
    int  autoHandoffFt;                 // >0: Abflug weg ueber dieser Hoehe (MSL)
    const char* depA; const char* depB; const char* depNone;   // Abflugketten (leer = EuroScope)
    const char* arrA; const char* arrB; const char* arrNone;   // Ankunftsketten (nach der Landung)
    const char* freqStand;              // Ankuenfte nach Stand
    const char* freqDel;                // Delivery nach Stand
    const char* gndStands;              // Stands ohne Apron (Startup bei Ground)
    const char* hsList;                 // Hold-Short-Schnellauswahl
    const char* intNorth; const char* intSouth;
    bool splitGnd;                      // Ground getrennt Nord/Sued (Berlin) - sonst ein Ground
    const char* twrA; const char* twrB; const char* twrNone;   // Tower je Bahn (naechste Station fuer Ground/Delivery)
    const char* sidFreq;                // Abflug nach SID: "PENEM,ORTAG=126.065; *=126.175"
};

// Berlin: Apron Nord 129.605, Main Apron (Sued) 121.855 bzw. EDDB_A_GND, Ground Nord 129.505, Ground Sued 121.705.
// GAT und Apron 1 haben kein Apron: Startup und Rollen direkt bei Ground Nord.
// Ein Apron fuer beide (z. B. nur EDDB_A_GND): die Frequenz bleibt je Seite (Nord 129.605, Sued 121.855),
// wie beim Tower ("Muster=Frequenz": ist das Muster online, gilt die Frequenz dahinter)
static const char* kArrNorthDef = "129.605 > EDDB_E_GND=129.605 > EDDB_*APN=129.605 > 121.855=129.605 > EDDB_A_GND=129.605 > 129.505";
static const char* kArrSouthDef = "121.855 > EDDB_A_GND=121.855 > EDDB_*APN=121.855 > 129.605=121.855 > EDDB_E_GND=121.855 > 121.705";
static const char* kStandDef = "G*,APRON1*,APN1*,AP1=129.505; W1,W2,W3,W4=121.705";
static const char* kDelDef = "G*,APRON1*,APN1*,AP1=129.505; A*,D01,D02,D03,D1,D2,D3,E*=129.605 > EDDB_E_GND=129.605 > EDDB_*APN=129.605 > 121.855=129.605 > EDDB_A_GND=129.605 > 129.505; "
                             "B*,C*,D*=121.855 > EDDB_A_GND=121.855 > EDDB_*APN=121.855 > 129.605=121.855 > EDDB_E_GND=121.855 > 121.705; W*=121.705";
static const char* kGndStandsDef = "W*,G*,APRON1*,APN1*,AP1";
static const char* kBerlinDepTail = " > 136.450 > 123.225 > 136.050 > 133.725 > 122.800!";
static const AirportProfile kProfiles[] = {
    // Berlin: zwei Tower, zwei Ground, Apron Nord/Sued, Delivery
    { "EDDB", "BERLIN", true, "06L,24R,07L,25R", "06R,24L,07R,25L", "06L 06R 24L 24R", true, 0,
      "EDDB_*APP=120.340", "EDDB_*APP=120.630", "EDDB_*APP=120.340",
      kArrNorthDef, kArrSouthDef, "",
      kStandDef,
      kDelDef,
      kGndStandsDef, "V1 V2 V3 V4 VC P1 P2 B C F G T",
      "L1 L2 L3 L4 L5 L6 L7 L8 K1 K2 K3 K4 K6", "M1 M2 M3 M4 M5 M6 M7 M8 M9",
      true, "", "", "", "" },
    // Leipzig: zwei Tower (Nord 08L/26R auf 125.955, Sued 08R/26L auf 121.105), ein Ground 121.805,
    // kein Apron. Abfluege nach SID: PENEM, ORTAG -> 126.065, alle anderen -> 126.175
    { "EDDP", "LEIPZIG", true, "08L,26R", "08R,26L", "08L 08R 26L 26R", false, 0,
      "", "", "",
      "121.805 > EDDP_*GND", "121.805 > EDDP_*GND", "121.805 > EDDP_*GND",
      "", "", "", "", "", "",
      false, "125.955", "121.105", "125.955 > 121.105", "PENEM,ORTAG=126.065; *=126.175" },
    // Hannover: ein Tower 120.180, ein Ground 121.955, kein Apron; IFR-Abfluege verschwinden ueber 1500 ft
    { "EDDV", "HANNOVER", false, "", "", "09L 09C 09R 27L 27C 27R", false, 1500,
      "", "", "119.490 > 124.240 > 125.025 > 127.625 > 133.725 > 122.800!",
      "", "", "121.955 > EDDV_*GND",
      "", "", "", "", "N M L KTO K J H G", "A B C D E",
      false, "", "", "120.180", "" },
};

// Eigene Plaetze aus StripBayAirports.txt (neben der StripBay.dll). Gehen den eingebauten vor.
static std::deque<std::string> g_profileText;          // haelt die Texte (c_str bleibt gueltig)
static std::vector<AirportProfile> g_fileProfiles;

static const AirportProfile* FindProfile(const std::string& icao)
{
    for (const AirportProfile& p : g_fileProfiles) if (icao == p.icao) return &p;
    for (const AirportProfile& p : kProfiles) if (icao == p.icao) return &p;
    return nullptr;
}

static std::vector<const AirportProfile*> AllProfiles()
{
    std::vector<const AirportProfile*> v;
    for (const AirportProfile& p : g_fileProfiles) v.push_back(&p);
    for (const AirportProfile& p : kProfiles)
    {
        bool dup = false;
        for (const AirportProfile* q : v) if (std::string(q->icao) == p.icao) dup = true;
        if (!dup) v.push_back(&p);
    }
    return v;
}

// INI-Text:  [EDDH]  city = HAMBURG  runways = 05 15 23 33  split = no  rwyA = 05,23  rwyB = 15,33
//            apron = no  splitGnd = no  autoHandoffFt = 0  depA/depB/dep  arrA/arrB/arr  freqStand  freqDel
//            gndStands  holdShort  intA  intB  twrA  twrB  twr  sidFreq     (";" oder "#" = Kommentar)
// Liefert die Zahl der Plaetze; Fehler (unbekannter Schluessel, Zeile ohne "=") in err
int ParseAirportProfiles(const std::string& text, std::vector<AirportProfile>& out, std::deque<std::string>& store,
                         std::string& err)
{
    out.clear();
    auto keep = [&](const std::string& v) -> const char* { store.push_back(v); return store.back().c_str(); };
    auto yes = [](std::string v) { for (char& c : v) c = (char)tolower((unsigned char)c); return v == "yes" || v == "ja" || v == "1" || v == "true" || v == "on"; };
    AirportProfile* cur = nullptr;
    int line = 0;
    std::string ln;
    for (size_t pos = 0; pos <= text.size();)
    {
        size_t nl = text.find('\n', pos);
        ln = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = nl == std::string::npos ? text.size() + 1 : nl + 1;
        ++line;
        if (!ln.empty() && ln.back() == '\r') ln.pop_back();
        std::string t = Trim(ln);
        if (t.empty() || t[0] == ';' || t[0] == '#') continue;
        if (t[0] == '[')
        {
            std::string icao = Upper(Trim(t.substr(1, t.find(']') == std::string::npos ? std::string::npos : t.find(']') - 1)));
            if (icao.size() != 4) { err += "line " + std::to_string(line) + ": [ICAO] expected; "; cur = nullptr; continue; }
            AirportProfile p = { keep(icao), keep(icao), false, keep(""), keep(""), keep(""), false, 0,
                                 keep(""), keep(""), keep(""), keep(""), keep(""), keep(""), keep(""), keep(""),
                                 keep(""), keep(""), keep(""), keep(""), false, keep(""), keep(""), keep(""), keep("") };
            out.push_back(p);
            cur = &out.back();
            continue;
        }
        size_t eq = t.find('=');
        if (!cur || eq == std::string::npos) { err += "line " + std::to_string(line) + ": ignored; "; continue; }
        std::string k = Lower(Trim(t.substr(0, eq))), v = Trim(t.substr(eq + 1));
        size_t cm = v.find(" ;");
        if (cm != std::string::npos) v = Trim(v.substr(0, cm));
        if (k == "city") cur->city = keep(Upper(v));
        else if (k == "split") cur->split = yes(v);
        else if (k == "rwya") cur->rwyA = keep(Upper(v));
        else if (k == "rwyb") cur->rwyB = keep(Upper(v));
        else if (k == "runways" || k == "rwys") cur->fixedRwys = keep(Upper(v));
        else if (k == "apron") cur->hasApron = yes(v);
        else if (k == "splitgnd") cur->splitGnd = yes(v);
        else if (k == "autohandoffft") cur->autoHandoffFt = atoi(v.c_str());
        else if (k == "depa") cur->depA = keep(v);
        else if (k == "depb") cur->depB = keep(v);
        else if (k == "dep") cur->depNone = keep(v);
        else if (k == "arra") cur->arrA = keep(v);
        else if (k == "arrb") cur->arrB = keep(v);
        else if (k == "arr") cur->arrNone = keep(v);
        else if (k == "freqstand") cur->freqStand = keep(v);
        else if (k == "freqdel") cur->freqDel = keep(v);
        else if (k == "gndstands") cur->gndStands = keep(v);
        else if (k == "holdshort" || k == "hs") cur->hsList = keep(Upper(v));
        else if (k == "inta" || k == "intnorth") cur->intNorth = keep(Upper(v));
        else if (k == "intb" || k == "intsouth") cur->intSouth = keep(Upper(v));
        else if (k == "twra") cur->twrA = keep(v);
        else if (k == "twrb") cur->twrB = keep(v);
        else if (k == "twr") cur->twrNone = keep(v);
        else if (k == "sidfreq") cur->sidFreq = keep(v);
        else err += "line " + std::to_string(line) + ": unknown key " + k + "; ";
    }
    return (int)out.size();
}

std::wstring StripBayPlugin::PluginDir() const
{
    if (!m_pluginDir.empty()) return m_pluginDir;
    wchar_t path[MAX_PATH] = {};
    if (!GetModuleFileNameW((HINSTANCE)&__ImageBase, path, MAX_PATH)) return std::wstring();
    std::wstring p = path;
    size_t s = p.find_last_of(L"\\/");
    return s == std::wstring::npos ? std::wstring() : p.substr(0, s);
}

// Alle StripBay-Einstellungen (wie in den EuroScope-Einstellungen gespeichert) als "Schluessel=Wert" in eine
// Datei; je Platz die Schluessel mit "_ICAO". Der Hoppie-Logon-Code nur auf Wunsch (persoenlich).
static const char* const kGlobalKeys[] = {
    "Airports", "ArrivalRange", "AutoVacate", "Cdm", "CdmUrl", "ColWidths", "DlAuto", "DlOn", "DlSound", "DlStation",
    "GrpAutoFn", "IcaoDir", "Layout", "NextButton", "NextFreqArr", "NextFreqDep", "OfferBlink", "OfferRemind", "Remote", "RemotePort", "Profile", "Range2", "Responsibility",
    "RunwayDropState", "RunwayFilter", "RunwaySplitOn", "SqkFnTopSky", "Theme", "Timeline", "TimelineEstimate", "TimelineNight", "TimelineRange", "TimelineTheme", "TopDown", "TransitionAltitude",
    "TsatSort", "UpdateCheck", "View", "ViewPresets", "Wx", "Zoom", "Sound_request", "Sound_wilco", "Sound_unable", "Sound_offer" };
static const char* const kProfileKeys[] = {
    "FreqArr1v2", "FreqArr2v2", "FreqChainArr0", "FreqDelv3", "FreqDep0v3", "FreqDep1v3", "FreqDep2v3", "FreqSid",
    "FreqStand2", "FreqTwr0", "FreqTwr1", "FreqTwr2", "GndStands", "HoldShortList2", "IntNorth", "IntSouth", "RunwaySplit" };

bool StripBayPlugin::ExportSettings(const std::wstring& file, bool withCode)
{
    std::string out = "; StripBay " STRIPBAY_VERSION " - Einstellungen (Import: .strips import)\r\n";
    auto add = [&](const std::string& k) {
        const char* v = GetDataFromSettings(k.c_str());
        if (v && *v) out += k + "=" + v + "\r\n";
    };
    for (const char* k : kGlobalKeys) add(k);
    if (withCode) add("DlCode");
    std::set<std::string> apts;
    for (const AirportProfile* p : AllProfiles()) apts.insert(p->icao);
    if (!m_profileIcao.empty()) apts.insert(m_profileIcao);
    for (const std::string& a : apts)
        for (const char* k : kProfileKeys) add(a == "EDDB" ? std::string(k) : std::string(k) + "_" + a);
    return WriteTextFileW(file, out);
}

int StripBayPlugin::ImportSettings(const std::wstring& file)
{
    std::string text;
    if (!ReadTextFileW(file, text)) return -1;
    std::set<std::string> known;
    for (const char* k : kGlobalKeys) known.insert(k);
    known.insert("DlCode");
    int n = 0;
    std::string ln;
    for (size_t pos = 0; pos <= text.size();)
    {
        size_t nl = text.find('\n', pos);
        ln = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = nl == std::string::npos ? text.size() + 1 : nl + 1;
        if (!ln.empty() && ln.back() == '\r') ln.pop_back();
        std::string t = Trim(ln);
        if (t.empty() || t[0] == ';' || t[0] == '#') continue;
        size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        std::string k = Trim(t.substr(0, eq)), v = t.substr(eq + 1);
        bool ok = known.count(k) > 0;
        for (const char* pk : kProfileKeys)
            if (k == pk || (k.size() == strlen(pk) + 5 && k.compare(0, strlen(pk) + 1, std::string(pk) + "_") == 0)) ok = true;
        if (!ok) continue;
        SaveDataToSettings(k.c_str(), "StripBay", v.c_str());
        ++n;
    }
    return n;
}

int StripBayPlugin::LoadAirportFile(bool verbose)
{
    std::wstring dir = PluginDir();
    std::wstring file = (dir.empty() ? std::wstring() : dir + L"\\") + L"StripBayAirports.txt";
    std::string text, err;
    if (!ReadTextFileW(file, text))
    {
        g_fileProfiles.clear();
        if (verbose) Msg("No StripBayAirports.txt next to StripBay.dll (create one with .strips airportfile template).");
        return 0;
    }
    std::vector<AirportProfile> v;
    std::deque<std::string> store;
    int n = ParseAirportProfiles(text, v, store, err);
    g_profileText.swap(store);
    g_fileProfiles.swap(v);
    std::string names;
    for (const AirportProfile& p : g_fileProfiles) names += (names.empty() ? "" : " ") + std::string(p.icao);
    if (verbose || !err.empty())
        Msg("StripBayAirports.txt: " + std::to_string(n) + " airport(s)" + (names.empty() ? "" : " (" + names + ")") +
            (err.empty() ? std::string(".") : ", " + err));
    return n;
}

std::string StripBayPlugin::PKey(const char* base) const
{
    // Berlin behaelt die alten Schluessel (bestehende Einstellungen gelten weiter)
    if (m_profileIcao == "EDDB") return base;
    return std::string(base) + "_" + m_profileIcao;
}

void StripBayPlugin::SetRunwayDefaults()
{
    bool on = m_split.enabled;
    m_split = RunwaySplit();
    m_split.enabled = on;
    const AirportProfile* p = FindProfile(m_profileIcao);
    if (!p || !p->split) return;                    // ein Tower: keine Aufteilung
    m_split.airport = p->icao;
    m_split.name[RWY_A] = "NORD";
    m_split.name[RWY_B] = "S\xDC" "D";   // ANSI, wie EuroScope-Texte
    // Berlin: seit 2024 06/24, aeltere Sektorpakete kennen noch 07/25
    for (const std::string& r : Split(p->rwyA, ",")) m_split.rwys[RWY_A].insert(r);
    for (const std::string& r : Split(p->rwyB, ",")) m_split.rwys[RWY_B].insert(r);
}

int StripBayPlugin::RunwayGroup(const std::string& airport, const std::string& rwy, bool* unknown) const
{
    if (unknown) *unknown = false;
    if (!m_split.enabled || m_split.airport.empty()) return RWY_NONE;
    if (Upper(Trim(airport)) != m_split.airport) return RWY_NONE;
    std::string r = NormRwy(rwy);
    for (int g = RWY_A; g <= RWY_B; ++g)
        if (!r.empty() && m_split.rwys[g].count(r)) return g;
    if (unknown) *unknown = true;   // am geteilten Platz, aber keine oder eine fremde Piste
    return RWY_NONE;
}

bool StripBayPlugin::SplitActive() const
{
    if (!m_split.enabled || m_split.airport.empty()) return false;
    return m_airports.count(m_split.airport) > 0 || m_arrAirports.count(m_split.airport) > 0;
}

// Welche Pisten sind im Runway-Dialog gerade aktiv? Nur fuer die Anzeige
// ("NORD 24R"), die Zuordnung haengt allein an der Piste im Tag.
void StripBayPlugin::RefreshRunways()
{
    // aktive Pisten aller Plaetze (fuer die Warnung "Piste nicht aktiv") und die Lage der Pisten
    std::map<std::string, std::set<std::string>> act[2];
    m_rwyGeom.clear();
    for (CSectorElement el = SectorFileElementSelectFirst(SECTOR_ELEMENT_RUNWAY); el.IsValid();
         el = SectorFileElementSelectNext(el, SECTOR_ELEMENT_RUNWAY))
    {
        std::vector<std::string> ap = Split(S(el.GetAirportName()), " \t");
        if (ap.empty()) continue;
        CPosition p0, p1;
        if (el.GetPosition(&p0, 0) && el.GetPosition(&p1, 1))
            m_rwyGeom.push_back({ Upper(ap[0]), p0.m_Latitude, p0.m_Longitude, p1.m_Latitude, p1.m_Longitude });
        for (int i = 0; i < 2; ++i)
            for (int k = 0; k < 2; ++k)
                if (el.IsElementActive(k == 0, i)) act[k][Upper(ap[0])].insert(NormRwy(S(el.GetRunwayName(i))));
    }
    bool actChanged = act[0] != m_actRwy[0] || act[1] != m_actRwy[1];
    m_actRwy[0].swap(act[0]);
    m_actRwy[1].swap(act[1]);

    std::set<std::string> found[3];
    if (!m_split.airport.empty())
    {
        for (CSectorElement el = SectorFileElementSelectFirst(SECTOR_ELEMENT_RUNWAY); el.IsValid();
             el = SectorFileElementSelectNext(el, SECTOR_ELEMENT_RUNWAY))
        {
            std::vector<std::string> ap = Split(S(el.GetAirportName()), " \t");
            if (ap.empty() || Upper(ap[0]) != m_split.airport) continue;
            for (int i = 0; i < 2; ++i)
            {
                if (!el.IsElementActive(true, i) && !el.IsElementActive(false, i)) continue;
                std::string r = NormRwy(S(el.GetRunwayName(i)));
                for (int g = RWY_A; g <= RWY_B; ++g)
                    if (m_split.rwys[g].count(r)) found[g].insert(r);
            }
        }
    }
    bool changed = false;
    for (int g = RWY_A; g <= RWY_B; ++g)
    {
        std::string a = JoinSet(found[g], "/");
        if (a != m_split.active[g])
        {
            m_split.active[g] = a;
            changed = true;
        }
    }
    if (changed || actChanged) PushBoard();
}

// Auf einer Piste: zwischen den Enden (plus 100 m) und hoechstens 45 m neben der Mittellinie.
// Ebene Naeherung um das erste Pistenende, fuer Pistenlaengen voellig ausreichend.
int StripBayPlugin::OnRunwayGeom(const std::string& apt, double lat, double lon) const
{
    bool any = false;
    for (const RwyGeom& r : m_rwyGeom)
    {
        if (r.apt != apt) continue;
        any = true;
        const double kLat = 111320.0;
        double kLon = kLat * std::cos(r.lat0 * 3.14159265358979 / 180.0);
        double vx = (r.lon1 - r.lon0) * kLon, vy = (r.lat1 - r.lat0) * kLat;
        double px = (lon - r.lon0) * kLon, py = (lat - r.lat0) * kLat;
        double len = std::sqrt(vx * vx + vy * vy);
        if (len < 1.0) continue;
        double along = (px * vx + py * vy) / len;
        double cross = std::fabs(px * vy - py * vx) / len;
        if (along >= -100.0 && along <= len + 100.0 && cross <= 45.0) return 1;
    }
    return any ? 0 : -1;
}

// "EDDB;NORD:06L,24R;SUED:06R,24L" (Gruppen mit ";" oder Leerzeichen getrennt,
// Pisten mit "," oder "/")
bool StripBayPlugin::ParseRunwaySplit(const std::string& text)
{
    std::vector<std::string> p = Split(text, "; \t");
    if (p.size() != 3) return false;
    RunwaySplit ns;
    ns.enabled = m_split.enabled;
    ns.airport = Upper(p[0]);
    if (ns.airport.size() != 4) return false;
    for (int g = RWY_A; g <= RWY_B; ++g)
    {
        const std::string& s = p[g];
        size_t colon = s.find(':');
        if (colon == std::string::npos || colon == 0) return false;
        ns.name[g] = UpperAcp(s.substr(0, colon));
        for (const std::string& r : Split(s.substr(colon + 1), ",/"))
        {
            std::string n = NormRwy(r);
            if (!n.empty()) ns.rwys[g].insert(n);
        }
        if (ns.rwys[g].empty()) return false;
    }
    if (ns.name[RWY_A] == ns.name[RWY_B]) return false;
    for (const std::string& r : ns.rwys[RWY_A])
        if (ns.rwys[RWY_B].count(r)) return false;   // eine Piste nur in einer Gruppe
    m_split = ns;
    return true;
}

std::string StripBayPlugin::RunwaySplitText() const
{
    std::string t = m_split.airport;
    for (int g = RWY_A; g <= RWY_B; ++g) t += ";" + m_split.name[g] + ":" + JoinSet(m_split.rwys[g], ",");
    return t;
}

void StripBayPlugin::SaveRunwaySplit()
{
    SaveDataToSettings(PKey("RunwaySplit").c_str(), "StripBay getrennte Pisten", RunwaySplitText().c_str());
    SaveDataToSettings("RunwaySplitOn", "StripBay getrennte Pisten an/aus", m_split.enabled ? "1" : "0");
}

// Filter-Argument: beide | Name der Gruppe (auch nur Anfangsbuchstabe) | Piste | 1 | 2
int StripBayPlugin::ParseRunwayFilterArg(const std::string& arg) const
{
    std::string a = UpperAcp(Trim(arg));
    if (a.empty()) return -1;
    if (a == "BEIDE" || a == "ALLE" || a == "ALL" || a == "BOTH" || a == "0") return 0;
    if (a == "1") return RWY_A;
    if (a == "2") return RWY_B;
    for (int g = RWY_A; g <= RWY_B; ++g)
        if (a == m_split.name[g]) return g;
    std::string r = NormRwy(a);
    for (int g = RWY_A; g <= RWY_B; ++g)
        if (m_split.rwys[g].count(r)) return g;
    for (int g = RWY_A; g <= RWY_B; ++g)   // "N", "S", "SUED" fuer "SÜD"
        if (!m_split.name[g].empty() && a[0] == m_split.name[g][0]) return g;
    return -1;
}

void StripBayPlugin::RunwayStatus()
{
    if (!m_split.enabled)
    {
        Msg("Split runways: off (.strips runways on).");
        return;
    }
    Msg("Split runways " + m_split.airport + (SplitActive() ? "" : " (airport currently not active)") + ":");
    for (int g = RWY_A; g <= RWY_B; ++g)
    {
        std::string a = m_split.active[g].empty() ? std::string("none active") : "active " + m_split.active[g];
        Msg("  " + AcpToUtf8(m_split.name[g]) + ": " + JoinSet(m_split.rwys[g], ", ") + "  (" + a + ")");
    }
    int f = m_window.GetRwyFilter();
    Msg("  Showing: " + (f == 0 ? std::string("both runways") : "only " + AcpToUtf8(m_split.name[f])));
}

// ---------------------------------------------------------------------------
// Naechste Frequenz
// ---------------------------------------------------------------------------
// Einfaches Muster: "*" = beliebig viele Zeichen, sonst exakt (gross/klein egal)
static bool Glob(const std::string& pat, const std::string& txt)
{
    size_t p = 0, t = 0, star = std::string::npos, mark = 0;
    while (t < txt.size())
    {
        if (p < pat.size() && (pat[p] == txt[t] || pat[p] == '?')) { ++p; ++t; }
        else if (p < pat.size() && pat[p] == '*') { star = p++; mark = t; }
        else if (star != std::string::npos) { p = star + 1; t = ++mark; }
        else return false;
    }
    while (p < pat.size() && pat[p] == '*') ++p;
    return p == pat.size();
}

bool StripBayPlugin::ControllerFreq(CController c, std::string& freq, std::string& who)
{
    if (!c.IsValid() || !c.IsController()) return false;
    std::string cs = S(c.GetCallsign());
    CController me = ControllerMyself();
    if (me.IsValid() && Upper(S(me.GetCallsign())) == Upper(cs)) return false;   // das bin ich selbst
    double f = c.GetPrimaryFrequency();
    if (f < 100.0 || f > 199.0) return false;   // 199.998 = keine Frequenz
    char buf[16];
    snprintf(buf, sizeof(buf), "%.3f", f);
    freq = buf;
    who = cs;
    return true;
}

// Berlin ab Werk (siehe .strips freq):
//  Abflug, beide Bahnen: Bremen Radar 120.340, wenn ein EDDB-Approach online ist,
//    sonst 136.450 > 123.225 > 136.050 > 133.725, wer davon online ist, sonst UNICOM 122.800
//  Ankunft Nord: Apron Nord 129.605, sonst Ground Nord 129.505, sonst niemand ("-")
//  Ankunft Sued: Apron Sued 121.855, sonst Ground Sued 121.705, sonst niemand ("-")
//  (nur wer online ist; keine Frequenz mit "!" mehr, ab 2.0.25)
// Die eigene Station zaehlt nie: Ground Nord sieht bei Ankuenften also Apron Nord.
void StripBayPlugin::SetFreqDefaults()
{
    // aus dem Platz-Profil; ohne Profil: keine Ketten (EuroScope bzw. Rufzeichen-Muster)
    const AirportProfile* p = FindProfile(m_profileIcao);
    for (int g = 0; g < 3; ++g) { m_chainDep[g].clear(); m_chainArr[g].clear(); m_chainTwr[g].clear(); }
    m_freqSid.clear();
    m_splitGnd = true;
    m_freqStand.clear(); m_freqDel.clear(); m_gndStands.clear(); m_hsList.clear(); m_intNorth.clear(); m_intSouth.clear();
    m_hasApron = true;          // unbekannter Platz: Apron-Ansicht bleibt verfuegbar
    m_autoHandoffFt = 0;
    m_fixedRwys.clear();
    if (!p) return;
    // Berlin: Abfluege je Bahn Bremen Radar 120.340 / 120.630, ohne EDDB_*APP die UNICOM-Kette
    std::string tail = (std::string(p->icao) == "EDDB") ? kBerlinDepTail : "";
    if (*p->depA) m_chainDep[RWY_A] = std::string(p->depA) + tail;
    if (*p->depB) m_chainDep[RWY_B] = std::string(p->depB) + tail;
    if (*p->depNone) m_chainDep[RWY_NONE] = std::string(p->depNone) + tail;
    m_chainArr[RWY_A] = p->arrA;
    m_chainArr[RWY_B] = p->arrB;
    m_chainArr[RWY_NONE] = p->arrNone;
    m_freqStand = p->freqStand;
    m_freqDel = p->freqDel;
    m_gndStands = p->gndStands;
    m_hsList = p->hsList;
    m_intNorth = p->intNorth;
    m_intSouth = p->intSouth;
    m_hasApron = p->hasApron;
    m_autoHandoffFt = p->autoHandoffFt;
    m_splitGnd = p->splitGnd;
    m_chainTwr[RWY_A] = p->twrA;
    m_chainTwr[RWY_B] = p->twrB;
    m_chainTwr[RWY_NONE] = p->twrNone;
    m_freqSid = p->sidFreq;
    m_fixedRwys = Split(p->fixedRwys, " ");
}

// Profil waehlen: festgelegt (.strips profile), sonst eigenes Callsign (EDDV_TWR -> EDDV),
// sonst der erste aktive Platz mit Profil, sonst der erste aktive Platz (allgemein)
void StripBayPlugin::ChooseProfile()
{
    std::string want = m_profileForced;
    if (want.empty())
    {
        CController me = ControllerMyself();
        if (me.IsValid())
        {
            std::string cs = Upper(S(me.GetCallsign()));
            std::string pre = cs.substr(0, cs.find('_'));
            if (pre.size() == 4 && FindProfile(pre)) want = pre;
        }
    }
    if (want.empty())
        for (const AirportProfile* p : AllProfiles())
            if (m_airports.count(p->icao) || m_arrAirports.count(p->icao)) { want = p->icao; break; }
    if (want.empty() && !m_airports.empty()) want = *m_airports.begin();
    if (want.empty() && !m_arrAirports.empty()) want = *m_arrAirports.begin();
    if (want.empty() || want == m_profileIcao) return;
    ApplyProfile(want);
}

void StripBayPlugin::ApplyProfile(const std::string& icao)
{
    m_profileIcao = icao;
    SetRunwayDefaults();
    SetFreqDefaults();
    LoadProfileOptions();
    RefreshRunways();
    Sync();
}

// Einstellungen, die je Platz gelten (Berlin: alte Schluessel, sonst mit "_ICAO")
void StripBayPlugin::LoadProfileOptions()
{
    if (const char* v = GetDataFromSettings(PKey("RunwaySplit").c_str()))
    {
        std::string t = Trim(S(v));
        RunwaySplit keep = m_split;
        if (!t.empty() && Lower(t) != "berlin" && ParseRunwaySplit(t) && m_split.airport != m_profileIcao) m_split = keep;
    }
    if (const char* v = GetDataFromSettings(PKey("FreqDep0v3").c_str())) m_chainDep[RWY_NONE] = Trim(S(v));
    if (const char* v = GetDataFromSettings(PKey("FreqDep1v3").c_str())) m_chainDep[RWY_A] = Trim(S(v));
    if (const char* v = GetDataFromSettings(PKey("FreqDep2v3").c_str())) m_chainDep[RWY_B] = Trim(S(v));
    if (const char* v = GetDataFromSettings(PKey("FreqArr1v2").c_str())) m_chainArr[RWY_A] = Trim(S(v));
    if (const char* v = GetDataFromSettings(PKey("FreqArr2v2").c_str())) m_chainArr[RWY_B] = Trim(S(v));
    if (const char* v = GetDataFromSettings(PKey("FreqChainArr0").c_str())) m_chainArr[RWY_NONE] = Trim(S(v));
    if (const char* v = GetDataFromSettings(PKey("FreqStand2").c_str())) m_freqStand = Trim(S(v));
    if (const char* v = GetDataFromSettings(PKey("FreqDelv3").c_str())) m_freqDel = Trim(S(v));
    if (const char* v = GetDataFromSettings(PKey("FreqTwr0").c_str())) m_chainTwr[RWY_NONE] = Trim(S(v));
    if (const char* v = GetDataFromSettings(PKey("FreqTwr1").c_str())) m_chainTwr[RWY_A] = Trim(S(v));
    if (const char* v = GetDataFromSettings(PKey("FreqTwr2").c_str())) m_chainTwr[RWY_B] = Trim(S(v));
    if (const char* v = GetDataFromSettings(PKey("FreqSid").c_str())) m_freqSid = Trim(S(v));
    if (m_profileIcao == "EDDB") MigrateFreqChains();
    if (const char* v = GetDataFromSettings(PKey("HoldShortList2").c_str()))
    {
        m_hsList = Trim(S(v));
        // alte Berliner Standards (ohne F, VC vor V3) auf den neuen umstellen
        if (m_hsList == "V1 V2 VC V3 V4 P1 P2 B C G T" || m_hsList == "V1 V2 VC V3 V4 P1 P2 B C F G T")
            m_hsList = "V1 V2 V3 V4 VC P1 P2 B C F G T";
    }
    if (const char* v = GetDataFromSettings(PKey("IntNorth").c_str())) if (!Trim(S(v)).empty()) m_intNorth = Upper(Trim(S(v)));
    if (const char* v = GetDataFromSettings(PKey("IntSouth").c_str())) if (!Trim(S(v)).empty()) m_intSouth = Upper(Trim(S(v)));
    if (const char* v = GetDataFromSettings(PKey("GndStands").c_str())) m_gndStands = Upper(Trim(S(v)));
    if (m_profileIcao == "EDDB") MigrateFreqChains();
}

// Alte Werkseinstellungen (bis 2.0.24) zeigten Apron immer an, auch offline ("!"):
// gespeicherte alte Werte auf die neuen umstellen, eigene Ketten bleiben
void StripBayPlugin::MigrateFreqChains()
{
    // bis 2.0.33: Main Apron nur ueber die Frequenz, GAT/Apron 1 ohne eigene Regel
    if (m_chainArr[RWY_B] == "121.855 > 121.705") m_chainArr[RWY_B] = kArrSouthDef;
    {
        const std::string oldDel = "G*,APRON1*,APN1*,AP1=129.505; A*,D01,D02,D03,D1,D2,D3,E*=129.605 > 129.505; "
                                   "B*,C*,D*=121.855 > EDDB_A_GND > 121.705; W*=121.705";
        if (m_freqDel == oldDel) m_freqDel = kDelDef;
    }
    if (m_freqStand == "W1,W2,W3,W4=121.705") m_freqStand = kStandDef;
    if (m_freqDel == "A*,D01,D02,D03,D1,D2,D3,E*=129.605 > 129.505; B*,C*,D*=121.855 > 121.705; W*=121.705") m_freqDel = kDelDef;
    if (m_gndStands == "W*") m_gndStands = kGndStandsDef;
    if (m_chainArr[RWY_A] == "129.605!") m_chainArr[RWY_A] = kArrNorth;
    if (m_chainArr[RWY_B] == "121.855!") m_chainArr[RWY_B] = kArrSouth;
    // bis 2.0.73: ohne Apron Nord direkt Ground Nord - jetzt erst Main Apron
    if (m_chainArr[RWY_A] == "129.605 > 129.505") m_chainArr[RWY_A] = kArrNorthDef;
    // bis 2.0.76: Apron der anderen Seite mit dessen Frequenz - jetzt Frequenz je Seite
    if (m_chainArr[RWY_A] == "129.605 > EDDB_E_GND > 121.855 > EDDB_A_GND > 129.505") m_chainArr[RWY_A] = kArrNorthDef;
    if (m_chainArr[RWY_B] == "121.855 > EDDB_A_GND > 121.705") m_chainArr[RWY_B] = kArrSouthDef;
    {
        const std::string old74 = "G*,APRON1*,APN1*,AP1=129.505; A*,D01,D02,D03,D1,D2,D3,E*=129.605 > EDDB_E_GND > 121.855 > EDDB_A_GND > 129.505; "
                                  "B*,C*,D*=121.855 > EDDB_A_GND > 121.705; W*=121.705";
        if (m_freqDel == old74) m_freqDel = kDelDef;
    }
    size_t p;
    while ((p = m_freqDel.find("=129.605!")) != std::string::npos) m_freqDel.replace(p, 9, "=129.605 > 129.505");
    while ((p = m_freqDel.find("=121.855!")) != std::string::npos) m_freqDel.replace(p, 9, "=121.855 > 121.705");
}

// Bekannte Berliner Frequenzen, falls dort gerade niemand online ist
static std::string FreqName(const std::string& f)
{
    if (f == "120.340" || f == "120.630") return "Bremen Radar";
    if (f == "129.505") return "Ground North";
    if (f == "121.705") return "Ground South";
    if (f == "129.605") return "Apron North";
    if (f == "121.855") return "Main Apron";
    if (f == "122.800") return "Advisory";
    return std::string();
}

// "120.34" -> "120.340", 0 wenn keine Frequenz
static std::string NormFreq(const std::string& f)
{
    double v = atof(f.c_str());
    if (v < 100.0 || v > 199.0) return std::string();
    char buf[16];
    snprintf(buf, sizeof(buf), "%.3f", v);
    return buf;
}

bool StripBayPlugin::EvalFreqChain(const std::string& chain, std::string& freq, std::string& who)
{
    // Die Kette laeuft "nach unten" (z. B. Apron, sonst Ground). Komme ich selbst darin vor,
    // ist alles danach meine eigene Vertretung, kein Nachbar: dann gibt es keinen ("-").
    CController me = ControllerMyself();
    char mine[16] = {};
    std::string myCs;
    if (me.IsValid())
    {
        snprintf(mine, sizeof(mine), "%.3f", me.GetPrimaryFrequency());
        myCs = Upper(S(me.GetCallsign()));
    }
    for (const std::string& raw : Split(chain, ">, ;"))
    {
        std::string rule = Upper(Trim(raw));
        if (rule.empty()) continue;
        bool always = rule.back() == '!';
        if (always) rule.pop_back();
        size_t eq = rule.find('=');
        std::string pattern = eq == std::string::npos ? std::string() : rule.substr(0, eq);
        std::string f = NormFreq(eq == std::string::npos ? rule : rule.substr(eq + 1));
        // nur ein Rufzeichen-Muster (z. B. EDDB_N_TWR): dessen Frequenz, wenn online
        if (eq == std::string::npos && f.empty()) pattern = rule;
        // Muster kann auch eine Frequenz sein ("121.855=129.605": wer auf 121.855 ist, gilt mit 129.605)
        std::string pf = pattern.empty() ? std::string() : NormFreq(pattern);
        if ((!f.empty() && f == mine) || (!pf.empty() && pf == mine) ||
            (!pattern.empty() && pf.empty() && !myCs.empty() && Glob(pattern, myCs))) return false;
        if (always && !f.empty())
        {
            freq = f;
            who = FreqName(f);
            for (CController c = ControllerSelectFirst(); c.IsValid(); c = ControllerSelectNext(c))
            {
                std::string cf, cw;
                if (ControllerFreq(c, cf, cw) && cf == f) { who = cw; break; }
            }
            return true;
        }
        for (CController c = ControllerSelectFirst(); c.IsValid(); c = ControllerSelectNext(c))
        {
            std::string cf, cw;
            if (!ControllerFreq(c, cf, cw)) continue;   // nicht ich, nur Lotsen mit Frequenz
            bool hit = pattern.empty() ? (cf == f) : !pf.empty() ? (cf == pf) : Glob(pattern, Upper(cw));
            if (!hit) continue;
            freq = f.empty() ? cf : f;
            who = cw;
            return true;
        }
    }
    return false;
}

// Wer bin ich fuer Abfluege? 1 Delivery, 2 Ground/Apron, 3 Tower (auch sonst)
int StripBayPlugin::MyRole()
{
    switch (m_window.GetView())
    {
    case VIEW_DEL: return 1;
    case VIEW_GND: case VIEW_APN: return 2;
    case VIEW_TWR: return 3;
    default: break;
    }
    CController me = ControllerMyself();
    std::string cs = me.IsValid() ? Upper(S(me.GetCallsign())) : std::string();
    auto ends = [&](const char* x) { size_t n = strlen(x); return cs.size() >= n && cs.compare(cs.size() - n, n, x) == 0; };
    if (ends("_DEL")) return 1;
    if (ends("_GND") || ends("_APN")) return 2;
    return 3;
}

// Abflug nach SID: "PENEM,ORTAG=126.065; *=126.175" - erste passende Gruppe, nur wer online ist
bool StripBayPlugin::EvalSidFreq(const std::string& sidIn, std::string& freq, std::string& who)
{
    if (m_freqSid.empty()) return false;
    std::string sid = Upper(Trim(sidIn));
    for (const std::string& group : Split(m_freqSid, ";"))
    {
        size_t eq = group.find('=');
        if (eq == std::string::npos) continue;
        bool hit = false;
        for (const std::string& p : Split(group.substr(0, eq), ", "))
        {
            std::string pat = Upper(Trim(p));
            if (pat.empty()) continue;
            if (pat == "*") hit = true;
            else if (pat.find('*') != std::string::npos || pat.find('?') != std::string::npos) hit = hit || Glob(pat, sid);
            else hit = hit || (!sid.empty() && sid.compare(0, pat.size(), pat) == 0);
        }
        if (!hit) continue;
        return EvalFreqChain(Trim(group.substr(eq + 1)), freq, who);
    }
    return false;
}

// Naechste Station am Platz fuer Abfluege: "GND" (Ground der Bahn) oder "TWR" (Tower der Bahn).
// Erst die Kette aus dem Profil, sonst nach Rufzeichen (<APT>_N_GND, <APT>_GND ...). Nur wer online ist.
bool StripBayPlugin::StationFreq(const std::string& kind, int g, const std::string& apt, std::string& freq, std::string& who)
{
    std::string side = g == RWY_A ? "N" : g == RWY_B ? "S" : "";
    if (kind == "TWR")
    {
        const std::string& twr = !m_chainTwr[g].empty() ? m_chainTwr[g] : m_chainTwr[RWY_NONE];
        if (!twr.empty() && EvalFreqChain(twr, freq, who)) return true;
        // Berlin: jede Bahn hat ihre Tower-Frequenz (Nord 120.030, Sued 118.805). Ist irgendein Tower
        // online (auch nur einer), gilt fuer die Bahn ihre Frequenz - er hoert beide ab.
        if (apt == "EDDB" && !side.empty())
        {
            CController me = ControllerMyself();
            std::string myCs = me.IsValid() ? Upper(S(me.GetCallsign())) : std::string();
            if (Glob(apt + "_*TWR", myCs)) return false;   // ich bin selbst Tower
            for (CController c = ControllerSelectFirst(); c.IsValid(); c = ControllerSelectNext(c))
            {
                std::string cf, cw;
                if (!ControllerFreq(c, cf, cw) || !Glob(apt + "_*TWR", Upper(cw))) continue;
                freq = side == "N" ? "120.030" : "118.805";
                who = cw;
                return true;
            }
            return false;
        }
        // erst der Tower dieser Bahn, dann ein gemeinsamer; ist nur ein Tower da, macht er beide Bahnen
        std::string chain = (side.empty() ? std::string() : apt + "_" + side + "_TWR > ") + apt + "_TWR > " + apt + "_*TWR";
        return EvalFreqChain(chain, freq, who);
    }
    // Ground: nicht Berlins Main Apron (EDDB_A_GND). Berlin nach Frequenz (Ground Nord 129.505, Sued 121.705)
    if (apt == "EDDB" && !side.empty() && EvalFreqChain(side == "N" ? "129.505" : "121.705", freq, who)) return true;
    // Plaetze ohne Apron: die Kette nach der Landung ist der Ground (Leipzig, Hannover)
    if (!m_hasApron)
    {
        const std::string& arr = !m_chainArr[g].empty() ? m_chainArr[g] : m_chainArr[RWY_NONE];
        if (!arr.empty() && EvalFreqChain(arr, freq, who)) return true;
    }
    std::string chain = (side.empty() || !m_splitGnd ? std::string() : apt + "_" + side + "_GND > ") + apt + "_GND";
    if (EvalFreqChain(chain, freq, who)) return true;
    // geteilter Ground und Seite bekannt: nicht der Ground der anderen Bahn
    if (!side.empty() && m_splitGnd) return false;
    for (CController c = ControllerSelectFirst(); c.IsValid(); c = ControllerSelectNext(c))
    {
        std::string cf, cw;
        if (!ControllerFreq(c, cf, cw)) continue;
        std::string u = Upper(cw);
        if (u == apt + "_A_GND" || u == apt + "_E_GND" || !Glob(apt + "_*GND", u)) continue;
        CController me = ControllerMyself();
        if (me.IsValid() && Upper(S(me.GetCallsign())) == u) continue;
        freq = cf; who = cw;
        return true;
    }
    return false;
}

// Reihenfolge:
//  1. laufender Handoff: dessen Ziel
//  2. am geteilten Platz (Berlin) die Frequenzkette (Abflug bzw. Ankunft je Bahn)
//  3. naechster Lotse, wie EuroScope ihn aus Route und Sektoren bestimmt (Sector Indicator)
//  4. Ersatzmuster (.strips freq): Abfluege {APT}_*DEP, {APT}_*APP; Ankuenfte {APT}_*GND
void StripBayPlugin::NextFrequency(CFlightPlan& fp, bool arrival, const std::string& apt, const std::string& rwy,
                                   std::string& freq, std::string& who)
{
    freq.clear();
    who.clear();
    std::string cs = S(fp.GetCallsign());
    // von Hand im Detailmenue gesetzt
    auto ov = m_freqOverride.find(cs);
    if (ov != m_freqOverride.end())
    {
        freq = ov->second;
        who = "manual";
        return;
    }
    std::string id = Trim(S(fp.GetHandoffTargetControllerId()));
    if (!id.empty() && ControllerFreq(ControllerSelectByPositionId(id.c_str()), freq, who)) return;

    // Durchstarter gehen zurueck zum Radar: Abflugkette der Bahn
    {
        auto ga = m_ga.find(cs);
        if (arrival && ga != m_ga.end() && ga->second && apt == m_profileIcao)
        {
            int g = RWY_NONE;
            std::string r = NormRwy(rwy);
            for (int k = RWY_A; k <= RWY_B; ++k)
                if (!r.empty() && m_split.rwys[k].count(r)) g = k;
            if (!m_chainDep[g].empty()) { EvalFreqChain(m_chainDep[g], freq, who); return; }
        }
    }

    // Delivery an Plaetzen ohne Stand-Regeln (Leipzig, Hannover): nach der Freigabe geht es
    // zum Ground der Bahn (die Kette der Ankuenfte nach der Landung), ohne Ground "-"
    if (!arrival && m_freqDel.empty() && apt == m_profileIcao)
    {
        CController me = ControllerMyself();
        std::string myCs = me.IsValid() ? Upper(S(me.GetCallsign())) : std::string();
        bool del = (myCs.size() > 4 && myCs.compare(myCs.size() - 4, 4, "_DEL") == 0) || m_window.GetView() == VIEW_DEL;
        if (del)
        {
            int g = RWY_NONE;
            std::string r = NormRwy(rwy);
            for (int k = RWY_A; k <= RWY_B; ++k)
                if (!r.empty() && m_split.airport == apt && m_split.rwys[k].count(r)) g = k;
            // Delivery -> Ground der Bahn -> Tower der Bahn -> Radar (Abflugkette)
            std::string gnd = !m_chainArr[g].empty() ? m_chainArr[g] : m_chainArr[RWY_NONE];
            if (!gnd.empty() && EvalFreqChain(gnd, freq, who)) return;
            if (StationFreq("GND", g, apt, freq, who)) return;
            if (StationFreq("TWR", g, apt, freq, who)) return;
            freq.clear(); who.clear();
        }
    }

    // Delivery: Abfluege nach Stand. Gilt, wenn du als DEL eingeloggt bist oder die
    // Ansicht Delivery offen ist.
    if (!arrival && !m_freqDel.empty() && apt == m_profileIcao)
    {
        CController me = ControllerMyself();
        std::string myCs = me.IsValid() ? Upper(S(me.GetCallsign())) : std::string();
        bool del = (myCs.size() > 4 && myCs.compare(myCs.size() - 4, 4, "_DEL") == 0) || m_window.GetView() == VIEW_DEL;
        // Apron ohne Delivery online: fuer Flieger noch ohne Startup (kein Status, ON FREQ) gilt die Delivery-Regel auch
        if (!del && m_window.GetView() == VIEW_APN && !DelOnline())
        {
            std::string gs = Upper(S(fp.GetGroundState()));
            del = gs.empty() || gs == "NSTS" || gs == "ONFREQ";
        }
        auto st = m_standLive.find(cs);
        std::string standNow = (st != m_standLive.end() && !st->second.empty()) ? st->second : StandAtPosition(apt, fp);
        if (standNow.empty())
        {
            auto en = m_entries.find(cs);
            if (en != m_entries.end()) standNow = en->second.data.stand;
        }
        // Stand unbekannt: naechste Stelle nach der Pistenseite (Apron -> Ground -> Tower), nicht Radar
        if (del && standNow.empty())
        {
            int g3 = RWY_NONE;
            std::string r3 = NormRwy(rwy);
            for (int k = RWY_A; k <= RWY_B; ++k)
                if (!r3.empty() && m_split.rwys[k].count(r3)) g3 = k;
            std::string ch = !m_chainArr[g3].empty() ? m_chainArr[g3] : m_chainArr[RWY_NONE];
            if (!ch.empty() && EvalFreqChain(ch, freq, who)) return;
            freq.clear(); who.clear();
            if (StationFreq("GND", g3, apt, freq, who)) return;
            if (StationFreq("TWR", g3, apt, freq, who)) return;
            freq.clear(); who.clear();
        }
        if (del && !standNow.empty())
        {
            std::string stand = Upper(standNow);
            for (const std::string& group : Split(m_freqDel, ";"))
            {
                size_t eq = group.find('=');
                if (eq == std::string::npos) continue;
                bool hit = false;
                for (const std::string& pat : Split(group.substr(0, eq), ", "))
                    if (Glob(Upper(pat), stand)) hit = true;
                if (!hit) continue;
                // Treffer: Apron bzw. Ground nach Stand. Niemand davon online: weiter zu Ground,
                // dann Tower der Bahn, erst dann Radar
                if (EvalFreqChain(group.substr(eq + 1), freq, who)) return;
                freq.clear(); who.clear();
                int g2 = RWY_NONE;
                std::string r2 = NormRwy(rwy);
                for (int k = RWY_A; k <= RWY_B; ++k)
                    if (!r2.empty() && m_split.rwys[k].count(r2)) g2 = k;
                if (StationFreq("GND", g2, apt, freq, who)) return;
                if (StationFreq("TWR", g2, apt, freq, who)) return;
                freq.clear(); who.clear();
                break;
            }
        }
    }

    if (!m_profileIcao.empty() && apt == m_profileIcao)
    {
        int g = RWY_NONE;
        std::string r = NormRwy(rwy);
        for (int k = RWY_A; k <= RWY_B; ++k)
            if (!r.empty() && m_split.rwys[k].count(r)) g = k;
        // Ankuenfte: erst die Regel nach Parkposition (z. B. W1-W4 -> Ground Sued)
        auto st = m_standLive.find(cs);
        if (arrival && st != m_standLive.end() && !st->second.empty() && !m_freqStand.empty())
        {
            // Gruppen mit ";" trennen: "W1,W2,W3,W4=121.705; G*=129.505". Bei einem Treffer gilt nur
            // diese Kette: ist dort niemand online (z. B. kein Ground), bleibt das Feld leer.
            std::string stand = Upper(st->second);
            for (const std::string& group : Split(m_freqStand, ";"))
            {
                size_t eq = group.find('=');
                if (eq == std::string::npos) continue;
                bool hit = false;
                for (const std::string& p : Split(group.substr(0, eq), ", "))
                {
                    std::string pre = Upper(Trim(p));
                    if (pre.empty()) continue;
                    if (pre.find('*') != std::string::npos || pre.find('?') != std::string::npos) hit = hit || Glob(pre, stand);
                    else hit = hit || stand == pre || (stand.compare(0, pre.size(), pre) == 0 && stand.size() > pre.size() &&
                                                       isalpha((unsigned char)stand[pre.size()]));
                }
                if (!hit) continue;
                if (!EvalFreqChain(Trim(group.substr(eq + 1)), freq, who)) { freq.clear(); who.clear(); }
                return;
            }
        }
        if (arrival)
        {
            // Ankunft: Kette nach der Landung (Apron/Ground). Findet sie niemanden, bleibt der
            // Flieger auf deiner Frequenz ("-").
            const std::string& chain = !m_chainArr[g].empty() ? m_chainArr[g] : m_chainArr[RWY_NONE];
            if (!chain.empty())
            {
                EvalFreqChain(chain, freq, who);
                return;
            }
        }
        else
        {
            // Apron gibt an Ground, sonst an den Tower; Ground an den Tower; erst dann Radar
            if (MyRole() == 2)
            {
                if (m_window.GetView() == VIEW_APN && StationFreq("GND", g, apt, freq, who)) return;
                if (StationFreq("TWR", g, apt, freq, who)) return;
            }
            // Delivery (hier ohne Treffer nach Stand): Ground, dann Tower
            if (MyRole() == 1)
            {
                if (StationFreq("GND", g, apt, freq, who)) return;
                if (StationFreq("TWR", g, apt, freq, who)) return;
            }
            // Abflug nach SID (Leipzig: PENEM/ORTAG -> 126.065, sonst 126.175)
            if (EvalSidFreq(S(fp.GetFlightPlanData().GetSidName()), freq, who)) return;
            // Abflugkette der Bahn; ist dort niemand online, entscheidet EuroScope (naechster Sektor)
            const std::string& chain = !m_chainDep[g].empty() ? m_chainDep[g] : m_chainDep[RWY_NONE];
            if (!chain.empty() && EvalFreqChain(chain, freq, who)) return;
            freq.clear(); who.clear();
        }
    }

    std::string next = Trim(S(fp.GetCoordinatedNextController()));
    if (!next.empty())
    {
        if (ControllerFreq(ControllerSelect(next.c_str()), freq, who)) return;
        if (ControllerFreq(ControllerSelectByPositionId(next.c_str()), freq, who)) return;
    }

    for (const std::string& raw : Split(arrival ? m_freqArr : m_freqDep, ", ;"))
    {
        std::string pat = Upper(raw);
        size_t p = pat.find("{APT}");
        if (p != std::string::npos) pat.replace(p, 5, apt);
        for (CController c = ControllerSelectFirst(); c.IsValid(); c = ControllerSelectNext(c))
            if (Glob(pat, Upper(S(c.GetCallsign()))) && ControllerFreq(c, freq, who)) return;
    }
}

// Setzt beim Verlassen der Funktion die Zustaendigkeit (nur wenn wirklich etwas passiert ist)
struct RespOnExit
{
    StripBayPlugin* p; std::string cs, unit; bool on = false;
    ~RespOnExit() { if (on && p->m_entries.count(cs)) p->SetResp(cs, unit); }
};

// Mehr als 20 NM von seinem Platz entfernt? Massgeblich ist der eigene Start- bzw. Zielplatz,
// bei anderen Plaetzen (von Hand erstellte Streifen) der naehere von beiden.
bool StripBayPlugin::FarAway(CFlightPlan& fp)
{
    CRadarTarget rt = fp.GetCorrelatedRadarTarget();
    if (!rt.IsValid()) return false;
    std::string adep = Upper(Trim(S(fp.GetFlightPlanData().GetOrigin())));
    std::string ades = Upper(Trim(S(fp.GetFlightPlanData().GetDestination())));
    const double inf = 1e9;
    double d = inf;
    if (!adep.empty() && m_airports.count(adep)) d = std::min(d, fp.GetDistanceFromOrigin());
    if (!ades.empty() && m_arrAirports.count(ades)) d = std::min(d, fp.GetDistanceToDestination());
    if (d >= inf) d = std::min(fp.GetDistanceFromOrigin(), fp.GetDistanceToDestination());
    // Ausweichlandung: zaehlt die echte Entfernung zu unserem Platz
    if (d > kGoneNm && m_divert.count(S(fp.GetCallsign())))
    {
        double nm = DistToAirportNm(m_divert[S(fp.GetCallsign())], rt);
        if (nm >= 0) d = std::min(d, nm);
    }
    else if (d > kGoneNm)
        for (const std::string& apt : m_arrAirports)
        {
            double nm = DistToAirportNm(apt, rt);
            if (nm >= 0 && nm <= kGoneNm) return false;   // nah an unserem Platz: nicht vergessen (Diversion moeglich)
        }
    return d > kGoneNm;
}

bool StripBayPlugin::QualifiesDeparture(CFlightPlan& fp, bool alreadyShown)
{
    if (!fp.IsValid()) return false;
    std::string adep = Upper(Trim(S(fp.GetFlightPlanData().GetOrigin())));
    if (adep.empty() || !m_airports.count(adep)) return false;

    CRadarTarget rt = fp.GetCorrelatedRadarTarget();
    if (!rt.IsValid()) return false;

    double dist = fp.GetDistanceFromOrigin();
    if (alreadyShown) return dist <= std::min(m_removeNm, kGoneNm);

    int gs = rt.GetPosition().GetReportedGS();
    return dist <= m_captureNm && gs < 50;
}

std::string StripBayPlugin::StandAtPosition(const std::string& apt, CFlightPlan& fp)
{
    CRadarTarget rt = fp.GetCorrelatedRadarTarget();
    if (!rt.IsValid() || rt.GetPosition().GetReportedGS() > 5) return std::string();
    auto inf = m_icao.standInfo.find(apt);
    if (inf == m_icao.standInfo.end()) return std::string();
    CPosition pos = rt.GetPosition().GetPosition();
    std::string best;
    double bestD = 1e18;
    for (const auto& kv : inf->second)
    {
        const StandInfo& si = kv.second;
        if (si.area) continue;
        if (si.poly.size() >= 3 && InPoly(si.poly, pos.m_Latitude, pos.m_Longitude)) return kv.first;
        if (!si.hasPos) continue;
        double d = DistM(si.lat, si.lon, pos.m_Latitude, pos.m_Longitude);
        if (d <= si.radius + 5.0 && d < bestD) { bestD = d; best = kv.first; }
    }
    return best;
}

// Entfernung zu einem unserer Plaetze (Mitte seiner Pisten) in NM, -1 wenn unbekannt
double StripBayPlugin::DistToAirportNm(const std::string& apt, CRadarTarget& rt)
{
    if (!rt.IsValid()) return -1;
    double la = 0, lo = 0; int n = 0;
    for (const RwyGeom& g : m_rwyGeom)
        if (g.apt == apt) { la += g.lat0 + g.lat1; lo += g.lon0 + g.lon1; n += 2; }
    if (!n) return -1;
    CPosition p = rt.GetPosition().GetPosition();
    return DistM(la / n, lo / n, p.m_Latitude, p.m_Longitude) / 1852.0;
}

// Ausweichlandung (Diversion, Rueckkehr): Flugplan-Ziel ist ein anderer Platz (bzw. bei uns nur
// Alternate), der Flieger ist aber im Anflug auf einen unserer Plaetze: nah, tief, langsam und
// sinkend - oder schon gelandet und langsam auf dem Platz. Gilt dann als Ankunft dort.
bool StripBayPlugin::QualifiesDiversion(CFlightPlan& fp, const std::string& cs, bool alreadyShown)
{
    if (!fp.IsValid()) return false;
    CRadarTarget rt = fp.GetCorrelatedRadarTarget();
    if (!rt.IsValid()) return false;
    std::string ades = Upper(Trim(S(fp.GetFlightPlanData().GetDestination())));
    std::string adep = Upper(Trim(S(fp.GetFlightPlanData().GetOrigin())));
    if (m_arrAirports.count(ades)) return false;   // normale Ankunft
    int alt = rt.GetPosition().GetPressureAltitude();
    int gs = rt.GetPosition().GetReportedGS();
    int vs = rt.GetVerticalSpeed();
    for (const std::string& apt : m_arrAirports)
    {
        double nm = DistToAirportNm(apt, rt);
        if (nm < 0) continue;
        bool known = alreadyShown && m_divert.count(cs) && m_divert[cs] == apt;
        // Abflug von hier kurz nach dem Start ist keine Rueckkehr (steigt)
        bool approach = nm <= m_arrivalNm && alt < 6000 && gs > 40 && gs < 230 && vs < -300;
        bool landed = nm <= 3.0 && gs < 60 && alt < 3000 && !(adep == apt && gs < 5);
        if (known ? nm <= std::max(m_arrivalNm + 5.0, 20.0) : (approach || (landed && adep != apt)))
        {
            if (!known) Msg(cs + ": diversion to " + apt + " (flight plan destination " + ades + ").");
            m_divert[cs] = apt;
            return true;
        }
    }
    return false;
}

bool StripBayPlugin::QualifiesArrival(CFlightPlan& fp, bool alreadyShown)
{
    if (!fp.IsValid()) return false;
    std::string ades = Upper(Trim(S(fp.GetFlightPlanData().GetDestination())));
    if (ades.empty() || !m_arrAirports.count(ades)) return false;
    // fertig geparkt: kein neuer Streifen; ein vorhandener bleibt, bis du ihn selbst parkst
    if (ParseGroundState(S(fp.GetGroundState())) == GC_PARK && !alreadyShown) return false;

    CRadarTarget rt = fp.GetCorrelatedRadarTarget();
    if (!rt.IsValid()) return false;

    // Ab m_arrivalNm im Final (Tower), nach der Landung in Taxi In (Ground).
    // Erst deutlich weiter weg wieder entfernen (Durchstarten, Ausweichen).
    double dist = fp.GetDistanceToDestination();
    return dist <= (alreadyShown ? std::max(m_arrivalNm, std::min(m_arrivalNm + 10.0, kGoneNm)) : m_arrivalNm);
}

void StripBayPlugin::Sync()
{
    std::set<std::string> seen, candidates;

    for (CFlightPlan fp = FlightPlanSelectFirst(); fp.IsValid(); fp = FlightPlanSelectNext(fp))
    {
        std::string cs = S(fp.GetCallsign());
        if (cs.empty()) continue;
        bool hidden = m_hidden.count(cs) > 0;
        auto it = m_entries.find(cs);
        if (it != m_entries.end() && it->second.freeStrip) continue;   // Notiz-Streifen mit gleichem Namen
        // Geparkter Flieger gibt einen neuen Flugplan auf (jetzt Abflug von hier):
        // Ground State zurueck auf "kein Status", alter Streifen und alle Merker weg
        if (ParseGroundState(S(fp.GetGroundState())) == GC_PARK)
        {
            std::string adep = Upper(Trim(S(fp.GetFlightPlanData().GetOrigin())));
            std::string key = adep + ">" + Upper(Trim(S(fp.GetFlightPlanData().GetDestination())));
            auto pk = m_parkPlan.find(cs);
            if (pk == m_parkPlan.end()) m_parkPlan[cs] = key;
            else if (pk->second != key)
            {
                m_parkPlan.erase(pk);
                if (!adep.empty() && m_airports.count(adep))
                {
                    ForgetFlight(cs);
                    m_forced.erase(cs);
                    ApplyGroundState(cs, GC_NSTS);
                    MsgAuto(cs + ": new flight plan while parked, ground state reset to no status.");
                    it = m_entries.end();
                    hidden = false;
                }
            }
        }
        else m_parkPlan.erase(cs);
        bool shownDep = it != m_entries.end() && !it->second.arrival;
        bool shownArr = it != m_entries.end() && it->second.arrival;

        // Weiter als 20 NM vom Platz: komplett weg (Streifen, Recover-Liste, Merker)
        if (FarAway(fp))
        {
            if (it != m_entries.end() || hidden || m_forced.count(cs) || m_resp.count(cs) || m_hov.count(cs) ||
                std::find(m_trashOrder.begin(), m_trashOrder.end(), cs) != m_trashOrder.end())
                ForgetFlight(cs);
            continue;
        }

        // Abflug hat Vorrang (z. B. Platzrunden mit gleichem Start- und Zielplatz).
        // Von Hand erstellte Streifen bleiben immer, bis man sie entfernt.
        bool arrival;
        auto forced = m_forced.find(cs);
        if (forced != m_forced.end()) arrival = forced->second;   // auch ausgeblendet im Papierkorb halten
        // Ohne Flugplan (NOFP): kein neuer Streifen (ein vorhandener bleibt)
        else if (it == m_entries.end() && !hidden && !fp.GetFlightPlanData().IsReceived()) continue;
        else if (QualifiesDeparture(fp, shownDep || hidden)) arrival = false;
        else if (QualifiesArrival(fp, shownArr || hidden)) { arrival = true; m_divert.erase(cs); }
        else { m_divert.erase(cs); continue; }

        candidates.insert(cs);
        if (hidden) continue;
        UpdateEntry(fp, arrival);
        seen.insert(cs);
    }

    // Bis zum Stand freigegeben (STD-Haken) und dort angekommen (steht im Stand): automatisch
    // geparkt - macht die zustaendige Stelle (ohne Zustaendigkeit: wer eine Stelle hat)
    {
        std::vector<std::string> park;
        for (const auto& kv : m_entries)
        {
            const Entry& e = kv.second;
            if (e.freeStrip || !e.arrival || !e.data.stdClr || !e.data.atGate || e.data.state == GC_PARK) continue;
            auto r = m_resp.find(kv.first);
            bool mine = (m_respOn && r != m_resp.end()) ? UnitIsMine(r->second) : !MyUnit().empty();
            if (mine) park.push_back(kv.first);
        }
        for (const std::string& c : park)
        {
            ChangeState(c, GC_PARK, BAY_GONE, -1);
            MsgAuto(c + ": at the stand, parked automatically.");
        }
    }
    std::vector<std::string> gone;
    for (const auto& kv : m_entries)
        if (!seen.count(kv.first) && !kv.second.freeStrip) gone.push_back(kv.first);
    for (const std::string& cs : gone) RemoveEntry(cs);
    // Flieger ganz weg (offline): letzten Status vergessen (neu verbunden = neu)
    for (auto it = m_lastGs.begin(); it != m_lastGs.end();)
        if (!m_entries.count(it->first) && !FlightPlanSelect(it->first.c_str()).IsValid()) { m_gsRestore.erase(it->first); it = m_lastGs.erase(it); }
        else ++it;

    for (auto it = m_hidden.begin(); it != m_hidden.end();)
    {
        if (!candidates.count(*it)) { m_hiddenArr.erase(*it); m_parked.erase(*it); it = m_hidden.erase(it); }
        else ++it;
    }

    NormalizeOrder();
    PushBoard();
}

// Jeder Streifen genau einmal, in der Liste seiner Bay (Sicherheitsnetz gegen doppelte Streifen)
void StripBayPlugin::NormalizeOrder()
{
    std::set<std::string> placed;
    for (int b = 0; b < BAY_COUNT; ++b)
    {
        std::vector<std::string>& v = m_order[b];
        std::vector<std::string> keep;
        for (const std::string& cs : v)
        {
            auto it = m_entries.find(cs);
            if (it == m_entries.end() || it->second.bay != b || placed.count(cs)) continue;
            placed.insert(cs);
            keep.push_back(cs);
        }
        v.swap(keep);
    }
    for (const auto& kv : m_entries)
        if (!placed.count(kv.first) && kv.second.bay >= 0 && kv.second.bay < BAY_COUNT) m_order[kv.second.bay].push_back(kv.first);
}

std::string StripBayPlugin::StandOf(CFlightPlan& fp, const std::string& cs, std::string* source)
{
    // Die juengste Information gewinnt, egal woher sie kommt:
    //  - eine Verteilung "GRP/S/..." per Scratchpad (Ereignis, siehe oben)
    //  - eine Aenderung in den Flight-Strip-Annotationen (dort legt GRP den Stand lokal ab)
    // So wird auch ein schon vorher vergebener Stand gefunden und jede spaetere
    // Aenderung uebernommen, auch wenn EuroScope fuer sie kein Ereignis schickt.
    CFlightPlanControllerAssignedData ca = fp.GetControllerAssignedData();

    std::string annStand, annSrc;
    static const int order[] = { 6, 0, 1, 2, 3, 4, 5, 7, 8 };
    for (int i : order)
    {
        annStand = ParseGrpStand(S(ca.GetFlightStripAnnotation(i)));
        if (!annStand.empty()) { annSrc = "Annotation " + std::to_string(i); break; }
    }
    if (annStand.empty())
    {
        annStand = ParseSlashStand(S(ca.GetFlightStripAnnotation(6)));
        if (!annStand.empty()) annSrc = "Annotation 6";
    }
    auto seen = m_standAnn.find(cs);
    if (seen == m_standAnn.end() || seen->second != annStand)
    {
        m_standAnn[cs] = annStand;
        if (!annStand.empty())
        {
            m_standLive[cs] = annStand;
            m_standSrc[cs] = annSrc;
        }
    }

    // Taucht im Scratchpad ein neues GRP/S/... auf, ist das die neueste Verteilung.
    // Ein dort liegengebliebener alter Befehl zaehlt nur einmal.
    std::string spRaw = S(ca.GetScratchPadString());
    std::string sp = IsOwnSync(cs, spRaw) ? m_standSp[cs] : ParseGrpStand(spRaw);
    auto lastSp = m_standSp.find(cs);
    if (lastSp == m_standSp.end() || lastSp->second != sp)
    {
        m_standSp[cs] = sp;
        if (!sp.empty())
        {
            m_standLive[cs] = sp;
            m_standSrc[cs] = "Scratchpad";
        }
    }

    std::string stand = m_standLive[cs];
    if (source) *source = stand.empty() ? std::string() : m_standSrc[cs];
    return stand;
}

void StripBayPlugin::UpdateEntry(CFlightPlan& fp, bool arrival)
{
    std::string cs = S(fp.GetCallsign());
    if (cs.empty()) return;

    // Aus einer Ankunft wird ein Abflug (neuer Flugplan) oder umgekehrt: neu anlegen
    auto old = m_entries.find(cs);
    if (old != m_entries.end() && old->second.arrival != arrival) RemoveEntry(cs);

    bool isNew = (m_entries.find(cs) == m_entries.end());
    Entry& e = m_entries[cs];
    e.arrival = arrival;

    CFlightPlanData d = fp.GetFlightPlanData();
    CFlightPlanControllerAssignedData ca = fp.GetControllerAssignedData();
    CRadarTarget rt = fp.GetCorrelatedRadarTarget();

    StripData& s = e.data;
    s.callsign = cs;
    s.acType = S(d.GetAircraftFPType());
    {
        // Hubschrauber: bekannte Typen oder ein Helipad (H1, H2 ...) als Piste
        static const char* kHeli[] = { "EC20", "EC30", "EC35", "EC45", "EC55", "EC25", "EC75", "EC135", "H120", "H125", "H130",
                                       "H135", "H145", "H155", "H160", "H175", "AS32", "AS50", "AS55", "AS65", "AS3B",
                                       "B06", "B206", "B407", "B412", "B429", "B505", "BK17", "A109", "A119", "A139", "A149",
                                       "A169", "A189", "R22", "R44", "R66", "S76", "S92", "H60", "UH1", "NH90", "CH47", "EH10",
                                       "MI8", "G2CA", "S61", "SA34", "SA36", "AS35", "AW09", "LYNX", "PUMA", "TIGR", nullptr };
        std::string t = Upper(Trim(s.acType));
        s.heli = false;
        for (int i = 0; kHeli[i]; ++i) if (t == kHeli[i]) s.heli = true;
        auto pad = [](const std::string& r) { std::string n = NormRwy(r); return n.size() >= 2 && n[0] == 'H' && isdigit((unsigned char)n[1]); };
        if (pad(S(d.GetDepartureRwy())) || pad(S(d.GetArrivalRwy()))) s.heli = true;
    }
    char wtc = d.GetAircraftWtc();
    s.wtc = (wtc && wtc != ' ') ? std::string(1, wtc) : std::string();
    s.rules = Upper(S(d.GetPlanType()));
    s.adep = S(d.GetOrigin());
    s.ades = S(d.GetDestination());
    s.altn = S(d.GetAlternate());
    s.trackedOther = !fp.GetTrackingControllerIsMe() && !Trim(S(fp.GetTrackingControllerCallsign())).empty();
    // Ausweichlandung: Ziel im Flugplan ist ein anderer Platz, der Flieger kommt aber zu uns
    s.divertFrom.clear();
    {
        auto dv = m_divert.find(cs);
        if (arrival && dv != m_divert.end() && Upper(Trim(s.ades)) != dv->second)
        {
            s.divertFrom = Upper(Trim(s.ades));
            s.ades = dv->second;
        }
    }
    s.sid = S(d.GetSidName());
    s.rwy = S(d.GetDepartureRwy());
    s.arrival = arrival;
    s.spoken = SpokenCallsign(cs, m_icao.airlines);
    {
        auto a = m_icao.airports.find(Upper(Trim(s.ades)));
        s.adesName = a == m_icao.airports.end() ? std::string() : NiceName(a->second);
        auto b = m_icao.airports.find(Upper(Trim(s.adep)));
        s.adepName = b == m_icao.airports.end() ? std::string() : NiceName(b->second);
    }
    s.star = S(d.GetStarName());
    s.arrRwy = S(d.GetArrivalRwy());
    // Pistengruppe aus der Piste im Tag
    s.rwyGroup = arrival ? RunwayGroup(s.ades, s.arrRwy, &s.rwyUnknown) : RunwayGroup(s.adep, s.rwy, &s.rwyUnknown);
    s.stand = StandOf(fp, cs, nullptr);
    // Abflug ohne vergebenen Stand: wie GRP ("Departure stand") aus der Position - der Stand,
    // auf dem der Flieger steht (Kreis bzw. Polygon aus GRpluginStands.txt)
    if (!arrival && s.stand.empty()) s.stand = StandAtPosition(Upper(Trim(s.adep)), fp);
    s.gndStand = !s.stand.empty() && StandInList(m_gndStands, s.stand);
    s.squawkAssigned = S(ca.GetSquawk());
    s.squawkCurrent = rt.IsValid() ? S(rt.GetPosition().GetSquawk()) : std::string();
    s.scratch = S(ca.GetScratchPadString());
    // Sync-Nachrichten (".SB_...", "GRP/S/", ".VSID_") nicht als Scratchpad zeigen (flackert sonst)
    if (IsTransientScratch(s.scratch))
    {
        auto rs = m_realSp.find(cs);
        s.scratch = rs != m_realSp.end() ? rs->second : std::string();
    }
    else if (!m_sync.count(cs) && !m_applyingGs && !IsGsCommand(s.scratch)) m_realSp[cs] = s.scratch;
    s.eobt = PadHHMM(S(d.GetEstimatedDepartureTime()));
    // Verlauf: Ground State und Clearance, wie EuroScope sie gerade zeigt (auch von Lotsen ohne StripBay)
    {
        std::string gsNow = Trim(S(fp.GetGroundState()));
        auto me = m_gsByMeAt.find(cs);
        bool mine = me != m_gsByMeAt.end() && m_tick - me->second <= 2;
        bool first = !m_histLast.count(cs) || !m_histLast[cs].count("GS");
        // ein leerer Status wird nicht notiert (EuroScope verliert ihn manchmal kurz; bewusst zurueckgesetzt = ".SB_NS_")
        if (gsNow.empty()) { if (first) m_histLast[cs]["GS"] = ""; }
        else Hist(cs, "GS", gsNow, "Ground state " + gsNow, first ? -1 : (mine ? 1 : 0));
        bool clr = fp.GetClearenceFlag();
        bool firstC = !m_histLast[cs].count("CLR");
        if (firstC && !clr) m_histLast[cs]["CLR"] = "0";
        else Hist(cs, "CLR", clr ? "1" : "0", clr ? "Clearance given" : "Clearance revoked", -1);
    }

    // TSAT: zuerst das CDM-Plugin (Annotation 0 = "ASRT/TSAC/TOBT/TSAT/TTOT/..."), sonst vACDM
    s.tsatUtc = 0;
    s.tsatSource.clear();
    long long now = (long long)std::time(nullptr);
    std::vector<std::string> cdm = SplitKeep(S(ca.GetFlightStripAnnotation(0)), '/');
    if (cdm.size() >= 5)
    {
        std::string t = PadHHMM(Trim(cdm[3]));
        if (t.size() == 4 || t.size() == 6)
        {
            s.tsatUtc = TimeOfDayToUtc(t, now);
            if (s.tsatUtc > 0) s.tsatSource = "CDM";
        }
    }
    if (s.tsatUtc == 0 && m_vacdm.IsRunning())
    {
        s.tsatUtc = m_vacdm.GetTsat(cs);
        if (s.tsatUtc > 0) s.tsatSource = "vACDM";
    }
    s.tsat = FormatHHMM(s.tsatUtc);

    // CTOT und TTOT fuer den Tower: CDM-Plugin Annotation 0, Feld 4 = TTOT, Feld 8 = CTOT (CTOC),
    // sonst vACDM ("ttot", "ctot")
    long long ttotUtc = 0, ctotUtc = 0;
    s.ctotSource.clear();
    auto hhmm = [&](size_t i) -> long long {
        if (i >= cdm.size()) return 0;
        std::string t = PadHHMM(Trim(cdm[i]));
        return (t.size() == 4 || t.size() == 6) ? TimeOfDayToUtc(t, now) : 0;
    };
    ttotUtc = hhmm(4);
    ctotUtc = hhmm(8);
    if (ctotUtc > 0) s.ctotSource = "CDM";
    if (m_vacdm.IsRunning())
    {
        if (ttotUtc == 0) ttotUtc = m_vacdm.GetTtot(cs);
        if (ctotUtc == 0) { ctotUtc = m_vacdm.GetCtot(cs); if (ctotUtc > 0) s.ctotSource = "vACDM"; }
    }
    s.ttot = FormatHHMM(ttotUtc);
    s.ctot = FormatHHMM(ctotUtc);
    s.ctotUtc = ctotUtc;
    s.route = S(d.GetRoute());
    s.remarks = S(d.GetRemarks());
    {
        std::string gsNew = S(fp.GetGroundState());
        if (m_logOn && !isNew && gsNew != s.groundStateRaw)
            Log("GS " + cs + " " + (s.groundStateRaw.empty() ? std::string("-") : s.groundStateRaw) + " -> " + (gsNew.empty() ? std::string("-") : gsNew));
        s.groundStateRaw = gsNew;
    }

    char comm = ca.GetCommunicationType();
    if (!comm || comm == ' ') comm = d.GetCommunicationType();
    s.comm = (comm && comm != ' ') ? std::string(1, (char)toupper((unsigned char)comm)) : std::string();

    int cfl = ca.GetClearedAltitude();       // 0, 1 und 2 sind Sonderwerte, keine Hoehen
    s.clearedAlt = cfl >= 100 ? cfl : 0;
    s.clearedApproach = (cfl == 1 || cfl == 2) ? cfl : 0;   // 1 = ILS, 2 = Sichtanflug
    NextFrequency(fp, arrival, Upper(Trim(arrival ? s.ades : s.adep)), arrival ? s.arrRwy : s.rwy, s.nextFreq, s.nextCtrl);
    s.finalAlt = fp.GetFinalAltitude();
    s.assignedHeading = ca.GetAssignedHeading();
    s.assignedSpeed = ca.GetAssignedSpeed();
    s.groundSpeed = rt.IsValid() ? rt.GetPosition().GetReportedGS() : 0;
    s.altitude = rt.IsValid() ? rt.GetPosition().GetPressureAltitude() : 0;
    s.clearance = fp.GetClearenceFlag();
    s.state = ParseGroundState(s.groundStateRaw);
    // EuroScope hat den Status verloren (leer), ohne dass ihn jemand zurueckgesetzt hat:
    // letzten Status behalten und wieder setzen
    if (!arrival && s.state == GC_NSTS && Trim(s.groundStateRaw).empty() && !m_onFreq.count(cs) && !m_lineup.count(cs))
    {
        auto lg = m_lastGs.find(cs);
        auto ns = m_nsAt.find(cs);
        if (ns != m_nsAt.end() && (long long)m_tick - ns->second < 60)
        {
            if (lg != m_lastGs.end()) m_lastGs.erase(lg);
            lg = m_lastGs.end();
        }
        if (lg != m_lastGs.end() && lg->second >= GC_STUP && lg->second <= GC_TAXI)
        {
            // nur in StripBay halten, nicht zurueck nach EuroScope schreiben (sonst Ping-Pong mit
            // dem Netz: Streifen und Scratchpad flackern); Meldung einmal je Flug
            s.state = (GroundCmd)lg->second;
            if (!m_gsRestore.count(cs))
            {
                m_gsRestore[cs] = lg->second;
                MsgAuto(cs + ": EuroScope has no ground state (was " + kStateName[lg->second] + "), strip kept in place.");
            }
        }
    }
    // Line-up ueber StripBay: gilt, solange der Flieger noch rollt (TAXI) bzw. kein Status gesetzt ist
    if (!arrival && m_lineup.count(cs))
    {
        if (s.state == GC_TAXI || s.state == GC_NSTS) s.state = GC_LINEUP;
        else if (s.state != GC_LINEUP) m_lineup.erase(cs);
    }
    // On Freq ueber StripBay (EuroScope ohne ONFREQ): gilt, solange kein anderer Status gesetzt ist
    if (!arrival && m_onFreq.count(cs))
    {
        if (s.state == GC_NSTS) s.state = GC_ONFREQ;
        else if (s.state != GC_ONFREQ) m_onFreq.erase(cs);
    }
    if (!arrival && s.state >= GC_STUP && s.state <= GC_DEPA) m_lastGs[cs] = s.state;
    s.stdClr = arrival && m_stdClr.count(cs) > 0;
    {
        auto h = m_hl.find(cs);
        s.hl = h == m_hl.end() ? 0 : h->second;
    }
    s.distDest = arrival ? fp.GetDistanceToDestination() : 0.0;
    if (arrival && !s.divertFrom.empty())
    {
        double nm = DistToAirportNm(s.ades, rt);
        if (nm >= 0) s.distDest = nm;
    }
    s.onGround = arrival && rt.IsValid() && OnGround(s.distDest, s.groundSpeed, rt.GetVerticalSpeed());
    // Am Gate angekommen: am Boden, langsamer als 3 kt und - wenn der Stand in den GRP-Daten eine
    // Lage hat - in seinem Standkreis (mit etwas Spiel). Nur dann laesst sich "geparkt" druecken.
    s.atGate = false;
    if (arrival && rt.IsValid() && s.onGround && s.groundSpeed < 3)
    {
        bool in = true;
        auto inf = m_icao.standInfo.find(Upper(Trim(s.ades)));
        if (!s.stand.empty() && inf != m_icao.standInfo.end())
        {
            auto si = inf->second.find(Upper(s.stand));
            if (si != inf->second.end() && (si->second.hasPos || si->second.poly.size() >= 3))
            {
                CPosition pos = rt.GetPosition().GetPosition();
                in = (si->second.hasPos && DistM(si->second.lat, si->second.lon, pos.m_Latitude, pos.m_Longitude) <=
                                               si->second.radius + 15.0) ||
                     (si->second.poly.size() >= 3 && InPoly(si->second.poly, pos.m_Latitude, pos.m_Longitude));
            }
        }
        s.atGate = in;
    }

    // Piste verlassen: gelandet und zweimal hintereinander neben jeder Piste des Zielplatzes
    // -> Tower gibt automatisch an Ground ab (Taxi In). Nur einmal je Landung.
    if (arrival && rt.IsValid())
    {
        if (!s.onGround) { e.offRwyCount = 0; e.vacated = false; }
        // Hannover: nie automatisch von der Piste (der Tower zieht selbst)
        else if (m_autoVacate && Upper(Trim(s.ades)) != "EDDV" && !e.vacated && !e.released)
        {
            CPosition pos = rt.GetPosition().GetPosition();
            int on = OnRunwayGeom(Upper(Trim(s.ades)), pos.m_Latitude, pos.m_Longitude);
            e.offRwyCount = (on == 0) ? e.offRwyCount + 1 : 0;
            if (e.offRwyCount >= 2)
            {
                e.vacated = true;
                e.released = true;
                e.onRunway = false;
            }
        }
    }

    // Intersection und Handover-Flag (beide von vSID, mit allen Lotsen geteilt)
    {
        auto in = m_int.find(cs);
        s.intersection = (!arrival && in != m_int.end()) ? in->second.first : std::string();
        s.intAssigned = !arrival && in != m_int.end() && in->second.second;
        auto hv = m_hov.find(cs);
        s.handedOver = !arrival && hv != m_hov.end() && hv->second;
    }

    // Hold Short. Mit Line-up oder Startfreigabe ist er erledigt (das macht jede
    // StripBay fuer sich, ohne Nachricht, also ueberall gleich).
    if (!arrival && (s.state == GC_LINEUP || s.state == GC_DEPA)) m_hs.erase(cs);
    {
        auto h = m_hs.find(cs);
        s.holdShort = h == m_hs.end() ? std::string() : h->second;
    }

    s.airborne = false;
    s.hovDue = false;
    if (!arrival && rt.IsValid())
    {
        // Abflug airborne: schneller als 60 kt und mindestens 150 ft ueber der
        // Hoehe beim Rollen. Der Streifen bleibt, bis der Flieger weggeschickt
        // ist (Handover-Flag), dann verschwindet er.
        if (s.groundSpeed < 40) e.groundAlt = s.altitude;
        bool airborne = e.groundAlt >= 0 ? (s.groundSpeed >= 60 && s.altitude >= e.groundAlt + 150)
                                         : (s.groundSpeed >= 100 && fp.GetDistanceFromOrigin() > 3.0);
        s.airborne = airborne;
        // Wegschicken erst ab 2000 ft ueber dem Platz farbig (Hoehe beim Rollen, sonst 2000 ft MSL)
        int base = e.groundAlt >= 0 ? e.groundAlt : 0;
        s.hovDue = airborne && s.altitude >= base + 2000;
        // Hannover: keine Uebergabe von Hand, ueber der Hoehe (z. B. 1500 ft) ist der Abflug weg
        // nur IFR: VFR (Platzrunden, Rundfluege) bleiben sichtbar
        bool autoGone = airborne && m_autoHandoffFt > 0 && s.rules != "V" && Upper(Trim(s.adep)) == m_profileIcao &&
                        s.altitude > m_autoHandoffFt;
        if (airborne && (s.handedOver || autoGone) && !isNew)
        {
            m_forced.erase(cs);
            RemoveEntry(cs);
            return;
        }
    }

    // Auf der Piste ohne Mode C: Transponder-Warnung
    // (Transponder-STBY-Warnung bei Abfluegen auf Wunsch entfernt, 2.0.43)
    s.xpdrOff = false;

    // Pruefung wie vSID (dessen Ergebnis koennen andere Plugins nicht lesen, deshalb dieselben
    // Kriterien aus den EuroScope-Daten): nur IFR-Abfluege, die noch am Boden sind
    s.vsidErr.clear();
    s.vsidErrText.clear();
    if (!arrival && s.rules == "I" && !s.airborne && s.state != GC_DEPA)
    {
        std::string adep = Upper(Trim(s.adep));
        std::string equip, info = S(d.GetAircraftInfo());
        size_t dash = info.find('-');
        if (dash != std::string::npos) equip = Upper(info.substr(dash + 1, info.find('/', dash) == std::string::npos ? std::string::npos : info.find('/', dash) - dash - 1));
        bool pbn = Upper(s.remarks).find("PBN/") != std::string::npos || Upper(info).find("PBN/") != std::string::npos;
        // RNAV fehlt: keiner von A, B, G, R, I und kein PBN (wie vSID bei RNAV-SIDs)
        bool badEquip = equip.size() > 1 && equip.find_first_of("ABGRI") == std::string::npos && !pbn;
        std::string sid = Upper(Trim(s.sid));
        if (sid.empty())
        {
            s.vsidErr = badEquip ? "EQUIP" : "MANUAL";
            s.vsidErrText = badEquip ? "no RNAV equipment (A, B, G, R, I or PBN) - SID by hand"
                                     : "no SID for the route - assign by hand";
        }
        else if (badEquip)
        {
            s.vsidErr = "EQUIP";
            s.vsidErrText = "no RNAV equipment (A, B, G, R, I or PBN) for " + sid;
        }
        else if (!NormRwy(s.rwy).empty())
        {
            // SID passt nicht zur Piste (laut Sektordatei)
            std::vector<std::string> ok = SectorList(SECTOR_ELEMENT_SID, adep, s.rwy);
            if (!ok.empty() && std::find(ok.begin(), ok.end(), sid) == ok.end())
            {
                s.vsidErr = "RWY";
                s.vsidErrText = sid + " is not a SID of runway " + NormRwy(s.rwy);
            }
        }
    }

    // Landefreigabe: CTL-Flag, wie vSID es verteilt. Nach der Landung bleibt der
    // Streifen in Landing, bis der Tower ihn nach Taxi In zieht.
    bool ctl = false;
    if (arrival)
    {
        auto c = m_ctl.find(cs);
        ctl = (c != m_ctl.end() && c->second);
    }
    s.landingCleared = ctl && !e.released;
    // Durchstarten endet mit einer neuen Landefreigabe oder der Landung (jede StripBay fuer sich)
    s.goAround = false;
    if (arrival)
    {
        auto g = m_ga.find(cs);
        if (g != m_ga.end() && g->second && (s.landingCleared || s.onGround)) m_ga.erase(g);
        else s.goAround = g != m_ga.end() && g->second;
    }

    // Uebergaben anderer Lotsen (und eigene) anwenden
    if (arrival)
    {
        auto ap = m_arrPos.find(cs);
        if (ap != m_arrPos.end())
        {
            if (ap->second == "RWY")      { e.onRunway = true;  e.released = false; }
            else if (ap->second == "REL") { e.onRunway = false; e.released = true; }
            else                          { e.onRunway = false; e.released = false; }
            m_arrPos.erase(ap);
        }
    }
    int xferBay = BAY_NONE;
    if (!arrival)
    {
        auto xf = m_xfer.find(cs);
        if (xf != m_xfer.end())
        {
            // gilt nur, solange der Flieger rollt; mit Line-up usw. ist die Uebergabe erledigt
            if (s.state == GC_TAXI) xferBay = xf->second == "TWR" ? (int)BAY_HANDOFF : (int)BAY_TOGND;
            else m_xfer.erase(xf);
        }
    }

    // In welche Bay gehoert der Streifen laut EuroScope?
    int target = xferBay;
    if (e.hasLocal && target == BAY_NONE)
    {
        bool keep = CanonState(s.groundStateRaw) == e.localBase;   // sonst hat jemand den Status geaendert, ES gewinnt
        if (keep) target = e.localBay;
        else
        {
            e.hasLocal = false;
            e.localIntentional = false;
        }
    }
    if (target == BAY_NONE)
    {
        int b = arrival ? ArrivalBayOf(e, s.state) : DepartureBay(s.state);
        if (b == BAY_GONE)
        {
            // PARK von jemand anderem (anderer Lotse, GRP): der Streifen bleibt, bis du selbst
            // "geparkt" drueckst (am Gate, < 3 kt)
            b = (!isNew && (e.bay == BAY_GNDIN || e.bay == BAY_TAXIIN)) ? e.bay : (int)BAY_TAXIIN;
        }
        target = (b >= 0) ? b : (isNew ? (arrival ? (int)BAY_FINAL : (int)BAY_PENDING) : e.bay);
    }

    if (isNew)
    {
        e.bay = target;
        MoveOrder(cs, -1, target, -1);
    }
    else if (target != e.bay)
    {
        MoveOrder(cs, e.bay, target, -1);   // Aenderung von aussen: unten anhaengen
        e.bay = target;
    }
}

void StripBayPlugin::RemoveEntry(const std::string& cs)
{
    auto it = m_entries.find(cs);
    if (it == m_entries.end()) return;
    for (int b = 0; b < BAY_COUNT; ++b)
    {
        std::vector<std::string>& v = m_order[b];
        v.erase(std::remove(v.begin(), v.end(), cs), v.end());
    }
    m_entries.erase(it);
}

void StripBayPlugin::MoveOrder(const std::string& cs, int fromBay, int toBay, int index)
{
    if (fromBay == toBay && index < 0) return;
    // aus allen Bays entfernen, nicht nur aus fromBay: ein Rueckruf von EuroScope (z. B. beim
    // Scratchpad-Senden) kann den Streifen inzwischen schon verschoben haben -> sonst doppelt
    for (int b = 0; b < BAY_COUNT; ++b)
    {
        std::vector<std::string>& v = m_order[b];
        v.erase(std::remove(v.begin(), v.end(), cs), v.end());
    }
    (void)fromBay;
    std::vector<std::string>& to = m_order[toBay];
    if (index < 0 || index > (int)to.size()) to.push_back(cs);
    else to.insert(to.begin() + index, cs);
}

bool StripBayPlugin::ApplyGroundState(const std::string& cs, GroundCmd cmd)
{
    CFlightPlan fp = FlightPlanSelect(cs.c_str());
    if (!fp.IsValid()) return false;
    if (ParseGroundState(S(fp.GetGroundState())) == cmd) return true;

    std::string spBefore = S(fp.GetControllerAssignedData().GetScratchPadString());
    if (IsTransientScratch(spBefore) || IsGsCommand(spBefore)) spBefore.clear();   // nie einen alten Befehl wiederholen
    bool ok = false;
    struct Applying { bool& f; Applying(bool& x) : f(x) { f = true; } ~Applying() { f = false; } } applying(m_applyingGs);
    m_gsByMeAt[cs] = m_tick;

    for (int i = 0; i < 3 && kScratch[cmd][i]; ++i)
    {
        CFlightPlan f = FlightPlanSelect(cs.c_str());
        if (!f.IsValid()) break;
        f.GetControllerAssignedData().SetScratchPadString(kScratch[cmd][i]);
        CFlightPlan check = FlightPlanSelect(cs.c_str());
        if (check.IsValid() && ParseGroundState(S(check.GetGroundState())) == cmd)
        {
            ok = true;
            break;
        }
    }

    // Steht danach etwas anderes im Scratchpad (z. B. der Befehlstext, falls ES
    // ihn nicht kennt), den alten Inhalt wiederherstellen. So bleibt etwa eine
    // eingetragene Parkposition erhalten.
    CFlightPlan after = FlightPlanSelect(cs.c_str());
    if (after.IsValid() && S(after.GetControllerAssignedData().GetScratchPadString()) != spBefore)
        after.GetControllerAssignedData().SetScratchPadString(spBefore.c_str());

    return ok;
}

// Kurz einen Text ins Scratchpad schreiben (wird an alle verteilt) und den
// alten Inhalt wiederherstellen
// Verlauf: eine Zeile anhaengen (hoechstens 40 je Streifen); derselbe Wert derselben Art nicht noch einmal
// (Resync und Echo wiederholen Merker)
void StripBayPlugin::Hist(const std::string& cs, const std::string& kind, const std::string& value, const std::string& text, int by)
{
    auto& last = m_histLast[cs];
    auto l = last.find(kind);
    if (l != last.end() && l->second == value) return;
    last[kind] = value;
    long long t = (long long)std::time(nullptr);
    char hm[8];
    snprintf(hm, sizeof(hm), "%02d%02d", (int)(t % 86400 / 3600), (int)(t % 3600 / 60));
    std::vector<std::string>& v = m_hist[cs];
    v.push_back(std::string(hm) + "  " + text + (by == 1 ? "  (you)" : by == 0 ? "  (other)" : ""));
    if (v.size() > 40) v.erase(v.begin());
}

// Verlauf: einen Merker (".SB_..", ".VSID_..") in Klartext
void StripBayPlugin::HistSync(const std::string& cs, const std::string& msg, bool mine)
{
    if (msg.size() < 8 || msg[0] != '.') return;
    size_t us = msg.rfind('_');
    if (us == std::string::npos || us + 1 >= msg.size()) return;
    const std::string kind = msg.substr(0, us + 1), v = msg.substr(us + 1);
    const bool on = v != "NONE" && v != "FALSE" && v != "0";
    std::string t;
    int by = mine ? 1 : 0;
    if (kind == ".SB_RS_") { t = on ? "Responsible: " + v : std::string("Responsible: nobody"); if (!mine && on) by = -1; }
    else if (kind == ".SB_HO_") t = on ? "Handover offered to " + v : std::string("Handover offer ended");
    else if (kind == ".SB_HX_") { t = "Handover rejected by " + v; by = -1; }
    else if (kind == ".SB_HS_") t = on ? "Hold short " + v : std::string("Hold short removed");
    else if (kind == ".SB_XF_") t = on ? "Moved to handover bay (" + v + ")" : std::string("Back from handover bay");
    else if (kind == ".SB_LU_") t = on ? "Line-up" : "Line-up cancelled";
    else if (kind == ".SB_GA_") t = on ? "Go around" : "Go around cleared";
    else if (kind == ".SB_PB_") t = on ? "Push direction " + v : std::string("Push direction removed");
    else if (kind == ".SB_TX_") { std::string r = v; for (char& c : r) if (c == '-') c = ' '; t = on ? "Taxi route " + r : std::string("Taxi route removed"); }
    else if (kind == ".SB_DI_") t = v == "1" ? "De-icing requested" : v == "2" ? "De-icing done" : "De-icing removed";
    else if (kind == ".SB_TP_") t = on ? "Sequence time " + v.substr(0, 4) : std::string("Sequence time reset");
    else if (kind == ".SB_AP_") t = v == "RWY" ? "Arrival on runway" : v == "REL" ? "Arrival released to ground" : "Arrival back on final";
    else if (kind == ".SB_SC_") t = on ? "Standard clearance marked" : "Standard clearance unmarked";
    else if (kind == ".SB_OF_") t = on ? "On frequency" : "On frequency removed";
    else if (kind == ".SB_HL_") t = on ? "Highlight colour " + v : std::string("Highlight removed");
    else if (kind == ".SB_NS_") t = "Ground state reset on purpose";
    else if (kind == ".VSID_CTL_") t = on ? "Landing clearance" : "Landing clearance cancelled";
    else if (kind == ".VSID_HOV_") t = on ? "Sent to next frequency" : "Sent to next frequency undone";
    else return;   // Stelle an/ab, Resync-Bitte, Stand, Unbekanntes: kein Eintrag
    Hist(cs, kind, v, t, by);
}

// Rollweg setzen (leer = loeschen), geteilt ueber ".SB_TX_A-B-B3"
void StripBayPlugin::SetTaxi(const std::string& cs, const std::string& text)
{
    auto it = m_entries.find(cs);
    if (it == m_entries.end() || it->second.freeStrip) return;
    std::string rt = NormTaxi(text);
    if (rt.empty()) m_taxi.erase(cs);
    else m_taxi[cs] = rt;
    std::string w = rt;
    for (char& c : w) if (c == ' ') c = '-';
    SendScratch(cs, ".SB_TX_" + (w.empty() ? std::string("NONE") : w));
    PushBoard();
}

// Enteisung: 0 keine, 1 angefragt, 2 erledigt
void StripBayPlugin::SetDeice(const std::string& cs, int state)
{
    auto it = m_entries.find(cs);
    if (it == m_entries.end() || it->second.freeStrip) return;
    if (state == 1 || state == 2) m_deice[cs] = state;
    else { m_deice.erase(cs); state = 0; }
    SendScratch(cs, ".SB_DI_" + std::to_string(state));
    PushBoard();
}

// Resync: Rollweg und Enteisung eines Fliegers noch einmal schicken
bool StripBayPlugin::ResyncExtras(const std::string& cs)
{
    bool any = false;
    auto tx = m_taxi.find(cs);
    if (tx != m_taxi.end())
    {
        std::string w = tx->second;
        for (char& c : w) if (c == ' ') c = '-';
        SendScratch(cs, ".SB_TX_" + w);
        any = true;
    }
    auto di = m_deice.find(cs);
    if (di != m_deice.end()) { SendScratch(cs, ".SB_DI_" + std::to_string(di->second)); any = true; }
    return any;
}

// Version einer anderen StripBay am Platz ("4X2X0", "3" = eine 3.x). Andere Hauptversion: Warnung im Chat
// und im Fenster (Uebergaben und Abgleich passen nicht zusammen). Gleiche Hauptversion, aber neuer: Hinweis.
void StripBayPlugin::VersionSeen(const std::string& wire)
{
    std::string v;
    for (char c : wire) v += (c == 'X' || c == 'x') ? '.' : c;
    if (v.empty() || !isdigit((unsigned char)v[0])) return;
    const std::string mine = STRIPBAY_VERSION;
    const int theirMajor = atoi(v.c_str()), myMajor = atoi(mine.c_str());
    const std::string shown = v.find('.') == std::string::npos ? v + ".x" : v;
    if (theirMajor != myMajor)
    {
        m_verWarn = "StripBay " + shown + " in use";
        if (m_verTold.insert(v).second)
        {
            Msg("WARNING: another controller uses StripBay " + shown + ", you use " + mine +
                ". Handovers and sync only work with the same major version (first number) - please use the same version.");
            PlayEventSound("unable");
        }
        PushBoard();
    }
    else if (CompareVersions(v, mine) > 0 && m_verTold.insert(v).second)
        Msg("Another controller uses the newer StripBay " + shown + " (you: " + mine + "). Both work together; new functions need the update.");
}

// Update-Hinweis: einmal nach dem Start bei GitHub nach der neuesten Version sehen
void StripBayPlugin::ProcessUpdate()
{
    if (m_updCheck && !m_updStarted && m_tick >= 8)
    {
        m_updStarted = true;
        m_upd.Start(STRIPBAY_REPO);
    }
    std::string tag;
    if (!m_upd.TryTake(tag)) return;
    const bool asked = m_updAsked;
    m_updAsked = false;
    if (tag.empty() || CompareVersions(tag, STRIPBAY_VERSION) <= 0)
    {
        // nur auf ausdrueckliche Nachfrage (.strips update check) melden
        if (asked) Msg(tag.empty() ? "Update check: github.com not reachable." : "You have the latest version (" STRIPBAY_VERSION ").");
        return;
    }
    m_updAvail = tag;
    Msg("Update available: StripBay " + tag + " (you have " STRIPBAY_VERSION "). Download: https://github.com/" STRIPBAY_REPO
        "/releases/latest   (switch off: .strips update off)");
    PushBoard();
}

// Diagnose fuer Fehlermeldungen: Version, Einstellungen, Stellen, Streifen und die letzten Meldungen in
// StripBay-diag.txt neben der StripBay.dll. Kein Logon-Code, kein Remote-Code.
bool StripBayPlugin::WriteDiag(std::wstring& fileOut)
{
    std::string o = "StripBay diagnosis\r\n==================\r\n";
    time_t tt = time(nullptr);
    char ts[40];
    snprintf(ts, sizeof(ts), "%02d:%02d:%02dZ", (int)(tt % 86400 / 3600), (int)(tt % 3600 / 60), (int)(tt % 60));
    o += std::string("Version:        ") + STRIPBAY_VERSION + "\r\n";
    o += std::string("Time (UTC):     ") + ts + "\r\n";
    CController me = ControllerMyself();
    o += "My callsign:    " + (me.IsValid() ? S(me.GetCallsign()) : std::string("-")) + "\r\n";
    o += "My station:     " + (MyUnit().empty() ? std::string("-") : MyUnit()) + "\r\n";
    o += "Profile:        " + m_profileIcao + (m_profileForced.empty() ? " (auto)" : " (fixed)") + "\r\n";
    {
        std::string a, b;
        for (const std::string& x : m_airports) a += x + " ";
        for (const std::string& x : m_arrAirports) b += x + " ";
        o += "Airports dep:   " + a + "\r\nAirports arr:   " + b + "\r\n";
    }
    WindowSettings ws = m_window.GetSettings();
    o += "Window:         view " + std::to_string(ws.view) + ", area " + std::to_string(ws.rwyFilter) + ", zoom " +
         std::to_string((int)(ws.zoom * 100 + 0.5)) + " %, layout " + std::to_string(ws.layout) + (ws.light ? ", light" : ", dark") +
         (ws.fullscreen ? ", fullscreen" : "") + "\r\n";
    o += std::string("Responsibility: ") + (m_respOn ? "on" : "off") + "\r\n";
    o += std::string("Remote:         ") + (m_net.Running() ? "on" : "off") + "\r\n";
    o += "Version note:   " + (m_verWarn.empty() ? std::string("-") : m_verWarn) + "\r\n";
    o += "Update:         " + (m_updAvail.empty() ? std::string(m_updCheck ? "none found" : "check off") : m_updAvail + " available") + "\r\n";
    o += "\r\nStations with StripBay (announced)\r\n";
    for (const auto& kv : m_sbUnits) o += "  " + kv.first + "\r\n";
    if (m_sbUnits.empty()) o += "  -\r\n";
    o += "\r\nControllers at my airports\r\n";
    for (const std::string& c : m_knownCtrls) o += "  " + c + "\r\n";
    if (m_knownCtrls.empty()) o += "  -\r\n";
    o += "\r\nStrips (callsign, A/D, bay, responsible / effective, offer, taxi route, de-icing)\r\n";
    for (const auto& kv : m_entries)
    {
        const std::string& cs = kv.first;
        auto r = m_resp.find(cs);
        auto of = m_offer.find(cs);
        auto tx = m_taxi.find(cs);
        auto di = m_deice.find(cs);
        o += "  " + cs + (kv.second.freeStrip ? "  note" : kv.second.arrival ? "  A" : "  D") + "  bay " + std::to_string(kv.second.bay) +
             "  resp " + (r == m_resp.end() ? std::string("-") : r->second) + " / " + (EffResp(cs).empty() ? std::string("-") : EffResp(cs)) +
             "  offer " + (of == m_offer.end() ? std::string("-") : of->second) +
             "  taxi " + (tx == m_taxi.end() ? std::string("-") : tx->second) +
             "  deice " + (di == m_deice.end() ? std::string("-") : std::to_string(di->second)) + "\r\n";
    }
    if (m_entries.empty()) o += "  -\r\n";
    o += "\r\nSettings (as saved with EuroScope)\r\n";
    {
        auto add = [&](const std::string& k) {
            const char* v = GetDataFromSettings(k.c_str());
            if (v && *v) o += "  " + k + "=" + v + "\r\n";
        };
        for (const char* k : kGlobalKeys) add(k);
        for (const char* k : kProfileKeys) add(m_profileIcao == "EDDB" || m_profileIcao.empty() ? std::string(k) : std::string(k) + "_" + m_profileIcao);
    }
    // Codes gehoeren nicht in eine Datei, die an eine Fehlermeldung gehaengt wird: den Kopplungscode fuer
    // StripBay Remote und den Hoppie-Logon-Code ueberall durch Sterne ersetzen, ebenso jede Zahl nach "code".
    auto redact = [&](std::string l) {
        for (const std::string* secret : { &m_remoteCode, &m_dlLogon })
            if (secret->size() >= 4)
                for (size_t q = l.find(*secret); q != std::string::npos; q = l.find(*secret, q + 6)) l.replace(q, secret->size(), "******");
        std::string low = Lower(l);
        for (size_t q = low.find("code"); q != std::string::npos; q = low.find("code", q + 4))
        {
            size_t d = q + 4;
            while (d < l.size() && (l[d] == ' ' || l[d] == ':' || l[d] == '=')) ++d;
            while (d < l.size() && isalnum((unsigned char)l[d])) { if (isdigit((unsigned char)l[d])) l[d] = '*'; ++d; }
        }
        return l;
    };
    o += "\r\nLast messages\r\n";
    for (const std::string& m : m_recentMsgs) o += "  " + redact(m) + "\r\n";
    if (m_recentMsgs.empty()) o += "  -\r\n";
    if (m_logOn)
    {
        o += "\r\nLog (last 300 lines)\r\n";
        size_t from = m_log.size() > 300 ? m_log.size() - 300 : 0;
        for (size_t i = from; i < m_log.size(); ++i) o += "  " + redact(m_log[i]) + "\r\n";
    }
    else o += "\r\nLog: off (switch on with .strips log on, reproduce the problem, then run .strips diag again)\r\n";
    std::wstring dir = PluginDir();
    fileOut = (dir.empty() ? std::wstring() : dir + L"\\") + L"StripBay-diag.txt";
    return WriteTextFileW(fileOut, o);
}

void StripBayPlugin::SendScratch(const std::string& cs, const std::string& text)
{
    CFlightPlan fp = FlightPlanSelect(cs.c_str());
    if (!fp.IsValid()) return;
    Log("OUT " + cs + " " + text);
    HistSync(cs, text, true);
    SyncQ& q = m_sync[cs];
    if (!q.busy && q.msgs.empty())
    {
        std::string before = S(fp.GetControllerAssignedData().GetScratchPadString());
        // steht gerade eine Sync-Nachricht (auch eines anderen Lotsen) drin: den letzten echten
        // Inhalt zurueckschreiben, nicht leer (sonst geht der Scratchpad-Text verloren)
        if (IsTransientScratch(before))
        {
            auto rs = m_realSp.find(cs);
            q.base = rs != m_realSp.end() ? rs->second : std::string();
        }
        else q.base = IsGsCommand(before) ? std::string() : before;
    }
    // Noch nicht gesendete Nachricht derselben Art (".SB_HS_", ".SB_RS_" ...) ersetzen:
    // es zaehlt nur der letzte Stand
    bool replaced = false;
    if (text.size() > 7 && text.compare(0, 4, ".SB_") == 0)
        for (size_t i = q.busy ? 1 : 0; i < q.msgs.size(); ++i)
            if (q.msgs[i].compare(0, 7, text, 0, 7) == 0) { q.msgs[i] = text; replaced = true; break; }
    // gleiche Nachricht nicht doppelt hintereinander
    if (!replaced && (q.msgs.empty() || q.msgs.back() != text)) q.msgs.push_back(text);
    if (!q.busy)
    {
        // sofort setzen, zurueckgesetzt wird erst im naechsten Takt
        if (!fp.GetControllerAssignedData().SetScratchPadString(q.msgs.front().c_str()) && m_syncWarned.insert(cs).second)
            MsgAuto(cs + ": StripBay sync not possible - EuroScope does not let you change the scratchpad "
                     "(aircraft tracked by another controller?). Others will not see this change.");
        q.busy = true;
        q.since = m_tick;
    }
}

// Steht im Scratchpad gerade eine eigene Sync-Nachricht (oder laeuft fuer den Flieger noch
// eine Synchronisation)? Dann ist das kein Befehl eines anderen Lotsen.
bool StripBayPlugin::IsOwnSync(const std::string& cs, const std::string& sp) const
{
    auto it = m_sync.find(cs);
    if (it == m_sync.end()) return false;
    // nur die gerade gesetzte Nachricht steht im Scratchpad; spaetere sind noch nicht raus
    return it->second.busy && !it->second.msgs.empty() && it->second.msgs.front() == sp;
}

// Jeden Takt: gesendete Nachrichten (seit mindestens einem Takt im Netz) abschliessen,
// naechste setzen oder den alten Scratchpad-Inhalt zurueck
void StripBayPlugin::ProcessSync()
{
    for (auto it = m_sync.begin(); it != m_sync.end();)
    {
        SyncQ& q = it->second;
        CFlightPlan fp = FlightPlanSelect(it->first.c_str());
        if (!fp.IsValid()) { it = m_sync.erase(it); continue; }
        if (q.busy && m_tick > q.since)
        {
            if (!q.msgs.empty()) q.msgs.pop_front();
            if (!q.msgs.empty())
            {
                fp.GetControllerAssignedData().SetScratchPadString(q.msgs.front().c_str());
                q.since = m_tick;
            }
            else
            {
                std::string cur = S(fp.GetControllerAssignedData().GetScratchPadString());
                if (IsTransientScratch(cur) || cur.empty()) fp.GetControllerAssignedData().SetScratchPadString(q.base.c_str());
                it = m_sync.erase(it);
                continue;
            }
        }
        ++it;
    }
}

// Landefreigabe setzen oder zuruecknehmen, im selben Format wie vSID
void StripBayPlugin::SetCtl(const std::string& cs, bool cleared)
{
    m_ctl[cs] = cleared;
    SendScratch(cs, cleared ? ".VSID_CTL_TRUE" : ".VSID_CTL_FALSE");
}

// Bay einer Ankunft. Auf der Runway liegt sie mit Landefreigabe (CTL) oder wenn
// der Tower sie dorthin gezogen hat. Nach der Landung bleibt sie dort, solange
// die Ansicht Ankuenfte auf der Runway zeigt (Tower, Alle Bays). Ground sieht
// gelandete Ankuenfte in Taxi In.
int StripBayPlugin::ArrivalBayOf(const Entry& e, GroundCmd cmd) const
{
    auto c = m_ctl.find(e.data.callsign);
    bool ctl = (c != m_ctl.end() && c->second) && !e.released;
    bool onRunway = (ctl || e.onRunway) && !e.released;
    bool ground = e.data.onGround || e.released;
    return ArrivalBay(cmd, ground, onRunway, m_window.ShowsArrivalsIn(BAY_RUNWAY));
}

void StripBayPlugin::ChangeState(const std::string& cs, GroundCmd cmd, int targetBay, int index)
{
    Log("STATE " + cs + " -> " + kStateName[cmd] + " (bay " + std::to_string(targetBay) + ")");
    auto it = m_entries.find(cs);
    if (it == m_entries.end()) return;

    CFlightPlan fp = FlightPlanSelect(cs.c_str());
    std::string rawNow = fp.IsValid() ? S(fp.GetGroundState()) : std::string();

    // Ankunft geparkt: Status setzen, Streifen verschwindet
    if (targetBay == BAY_GONE)
    {
        if (fp.IsValid()) ApplyGroundState(cs, cmd);
        bool arr = it->second.arrival;
        RemoveEntry(cs);
        m_hidden.insert(cs);
        // in den Papierkorb, damit ein versehentliches "geparkt" zurueckgeht
        m_hiddenArr[cs] = arr;
        m_parked.insert(cs);
        m_trashOrder.erase(std::remove(m_trashOrder.begin(), m_trashOrder.end(), cs), m_trashOrder.end());
        m_trashOrder.push_back(cs);
        PushBoard();
        return;
    }
    if (targetBay < 0) return;
    if (cmd == GC_NSTS && !it->second.arrival)
    {
        // bewusst zurueck auf "kein Status": nicht wiederherstellen, auch bei den anderen
        if (m_lastGs.erase(cs)) SendScratch(cs, ".SB_NS_TRUE");
        m_gsRestore.erase(cs);
    }
    else if (cmd != GC_NSTS && !it->second.arrival) m_gsRestore.erase(cs);

    MoveOrder(cs, it->second.bay, targetBay, index);
    it->second.bay = targetBay;
    it->second.localIntentional = false;

    // StripBay-eigene Zustaende (On Freq, Line-up) verlassen: Merker zuruecknehmen, auch wenn
    // EuroScope schon den Zielstatus hat (sonst springt der Streifen zurueck, z. B. On Freq -> Pending)
    if (cmd != GC_ONFREQ && m_onFreq.count(cs))
    {
        m_onFreq.erase(cs);
        SendScratch(cs, ".SB_OF_FALSE");
    }
    if (cmd != GC_LINEUP && m_lineup.count(cs))
    {
        m_lineup.erase(cs);
        if (cmd != GC_DEPA) SendScratch(cs, ".SB_LU_FALSE");
    }

    if (ParseGroundState(rawNow) == cmd)
    {
        it->second.hasLocal = false;
        PushBoard();
        return;
    }

    // Vorlaeufig lokal festhalten, damit Rueckmeldungen von EuroScope waehrend
    // des Umschaltens den Streifen nicht zurueckschieben
    it->second.hasLocal = true;
    it->second.localBay = targetBay;
    it->second.localBase = CanonState(rawNow);

    bool ok = fp.IsValid() && ApplyGroundState(cs, cmd);

    it = m_entries.find(cs);
    if (it == m_entries.end())
    {
        PushBoard();
        return;
    }
    if (ok)
    {
        it->second.hasLocal = false;
    }
    else if (cmd == GC_ONFREQ && !it->second.arrival)
    {
        // Manche ES-Versionen kennen ONFREQ nicht: dann verteilt StripBay "On Freq" selbst
        // (".SB_OF_TRUE"), alle mit StripBay sehen den Streifen in ON FREQUENCY
        it->second.localIntentional = true;
        m_onFreq[cs] = true;
        SendScratch(cs, ".SB_OF_TRUE");
    }
    else if (cmd == GC_LINEUP && !it->second.arrival)
    {
        // EuroScope kennt LINEUP nicht (bzw. nimmt es nicht an): StripBay verteilt "Line-up" selbst
        it->second.localIntentional = true;
        it->second.hasLocal = false;
        m_lineup[cs] = true;
        SendScratch(cs, ".SB_LU_TRUE");
    }
    else
    {
        Msg(cs + ": EuroScope did not accept state " + kStateName[cmd] +
            ". The strip is only moved locally (orange corner).");
    }

    CFlightPlan fresh = FlightPlanSelect(cs.c_str());
    if (fresh.IsValid() && m_entries.count(cs)) UpdateEntry(fresh, m_entries[cs].arrival);
    PushBoard();
}

bool StripBayPlugin::DelOnline()
{
    std::set<std::string> apts = m_airports;
    if (!m_profileIcao.empty()) apts.insert(m_profileIcao);
    CController me = ControllerMyself();
    std::string myCs = me.IsValid() ? Upper(S(me.GetCallsign())) : std::string();
    for (const std::string& a : apts)
        if (!myCs.empty() && Glob(Upper(a) + "_*DEL", myCs)) return true;
    for (CController c = ControllerSelectFirst(); c.IsValid(); c = ControllerSelectNext(c))
    {
        std::string cf, cw;
        if (!ControllerFreq(c, cf, cw)) continue;
        for (const std::string& a : apts)
            if (Glob(Upper(a) + "_*DEL", Upper(cw))) return true;
    }
    return false;
}

// Apron besetzt: jemand (nicht ich) auf einer Apron-Frequenz
bool StripBayPlugin::ApronOnline()
{
    if (!m_hasApron) return false;   // Platz ohne Apron (Leipzig, Hannover): Ground macht alles
    std::string a = m_profileIcao;
    for (CController c = ControllerSelectFirst(); c.IsValid(); c = ControllerSelectNext(c))
    {
        std::string cf, cw;
        if (!ControllerFreq(c, cf, cw)) continue;
        if (a == "EDDB" && (cf == "129.605" || cf == "121.855")) return true;   // Berlin Apron Nord / Main Apron
        std::string u = Upper(cw);
        if (!a.empty() && (Glob(a + "_*APN", u) || Glob(a + "_A_GND", u) || Glob(a + "_E_GND", u))) return true;
    }
    return false;
}

bool StripBayPlugin::ApronSideOnline(char side)
{
    if (!m_hasApron) return false;
    std::string a = m_profileIcao;
    for (CController c = ControllerSelectFirst(); c.IsValid(); c = ControllerSelectNext(c))
    {
        std::string cf, cw;
        if (!ControllerFreq(c, cf, cw)) continue;
        std::string u = Upper(cw);
        if (side == 'N' && (cf == "129.605" || u == a + "_E_GND" || Glob(a + "_N_APN", u))) return true;
        if (side == 'S' && (cf == "121.855" || u == a + "_A_GND" || Glob(a + "_S_APN", u))) return true;
    }
    return false;
}

// Ground besetzt: jemand (nicht ich) als <Platz>_*GND (ohne Berlins Main Apron EDDB_A_GND)
bool StripBayPlugin::GndOnline()
{
    std::string a = m_profileIcao;
    if (a.empty()) return true;
    CController me = ControllerMyself();
    std::string myCs = me.IsValid() ? Upper(S(me.GetCallsign())) : std::string();
    for (CController c = ControllerSelectFirst(); c.IsValid(); c = ControllerSelectNext(c))
    {
        std::string cf, cw;
        if (!ControllerFreq(c, cf, cw)) continue;
        std::string u = Upper(cw);
        if (u == myCs || u == a + "_A_GND" || u == a + "_E_GND") continue;   // Apron (Main / Nord)
        if (Glob(a + "_*GND", u)) return true;
    }
    return false;
}

void StripBayPlugin::PushBoard()
{
    Board b;
    std::set<std::string> all = m_airports;
    all.insert(m_arrAirports.begin(), m_arrAirports.end());
    for (const std::string& a : all)
    {
        if (!b.airports.empty()) b.airports += "  ";
        b.airports += a;
    }
    b.transitionAltitude = m_transitionAlt;
    b.hasApron = m_hasApron;
    b.splitGnd = m_splitGnd;
    b.profile = m_profileIcao;
    b.profileForced = !m_profileForced.empty();
    b.autoHandoffFt = m_autoHandoffFt;
    b.icaoLoaded = m_icaoDone && (!m_icao.airlines.empty() || !m_icao.airports.empty());
    b.rwySplit = SplitActive();
    b.delOnline = DelOnline();
    b.opt.splitEnabled = m_split.enabled;
    b.opt.autoVacate = m_autoVacate;
    b.opt.wx = m_wxEnabled;
    b.opt.vacdm = m_vacdmEnabled;
    b.opt.dlOn = m_dlEnabled;
    b.opt.dlCode = !m_dlLogon.empty();
    b.opt.dlAutoLogon = m_dlAutoLogon;
    b.opt.dlSound = m_dlSound;
    b.opt.dlStation = DlStation();
    b.opt.takeoffDepa = m_runwayCmd == GC_DEPA;
    b.opt.nextBtn = m_nextBtn;
    b.vacdmError = m_vacdmEnabled ? m_vacdm.LastError() : std::string();
    b.rwyState = m_rwyState;
    b.opt.colWidths = m_colWidths;
    b.opt.tsatSort = m_tsatSort;
    b.opt.timeline = m_timeline;
    b.opt.tlPast = m_tlPast;
    b.opt.tlFuture = m_tlFuture;
    b.opt.tlDfs = m_tlDfs;
    b.opt.tlNight = m_tlNight;
    b.opt.tlEst = m_tlEst;
    b.opt.offerBlink = m_offerBlink;
    b.opt.offerRemind = m_offerRemind;
    b.opt.respOn = m_respOn;
    // Rollweg: mit Line-up bzw. Startfreigabe und in der Luft erledigt. Er wird dann nur nicht mehr gezeigt
    // (ohne Nachricht und ohne ihn zu loeschen - so bleibt der Stand bei allen Lotsen gleich).
    for (const auto& kv : m_taxi)
    {
        auto e = m_entries.find(kv.first);
        if (e == m_entries.end()) continue;
        const StripData& d = e->second.data;
        if (!d.arrival && (d.state == GC_LINEUP || d.state == GC_DEPA || d.airborne)) continue;
        b.opt.taxi[kv.first] = kv.second;
    }
    for (const auto& kv : m_deice) if (m_entries.count(kv.first)) b.opt.deice[kv.first] = kv.second;
    b.opt.updAvail = m_updAvail;
    b.opt.verWarn = m_verWarn;
    b.opt.updCheck = m_updCheck;
    if (!m_undo.empty())
    {
        static const char* kw[] = { "", "move", "resp", "handover", "remove" };
        b.undoLabel = m_undo.back().cs + " " + kw[m_undo.back().kind];
    }
    b.opt.arrNm = (int)m_arrivalNm;
    b.opt.transitionAlt = m_transitionAlt;
    b.opt.gndStands = m_gndStands;
    b.opt.hsList = m_hsList;
    b.opt.intNorth = m_intNorth;
    b.opt.intSouth = m_intSouth;
    b.apnOnline = ApronOnline();
    b.gndOnline = GndOnline();
    b.topDown = m_topDown;
    b.dlEnabled = m_dlEnabled && !m_dlLogon.empty();
    b.dlConnected = m_hoppie.IsRunning() && m_hoppie.Connected();
    b.dlStation = DlStation();
    b.wxAirport = WxAirport();
    if (!b.wxAirport.empty())
    {
        auto me = m_metarEs.find(b.wxAirport);
        b.metar = (me != m_metarEs.end()) ? me->second : (m_wxEnabled ? m_wx.GetMetar(b.wxAirport) : std::string());
        b.atis = m_wxEnabled ? m_wx.GetAtis(b.wxAirport) : std::string();
    }
    for (int g = RWY_A; g <= RWY_B; ++g)
    {
        b.rwyName[g] = m_split.name[g];
        b.rwyActive[g] = m_split.active[g];
    }
    // Hannover (ein Tower): Nord- und Suedbahn nur fuer den Zeitstrahl benennen, aktive Piste je Bahn
    if (!b.rwySplit && m_profileIcao == "EDDV")
    {
        static const char* kSide[2][2] = { { "09L", "27R" }, { "09R", "27L" } };
        for (int g = RWY_A; g <= RWY_B; ++g)
        {
            b.rwyName[g] = g == RWY_A ? "NORD" : "SUED";
            std::string a;
            for (const char* r : kSide[g - RWY_A])
            {
                bool on = false;
                for (int k = 0; k < 2; ++k)
                {
                    auto it = m_actRwy[k].find("EDDV");
                    if (it != m_actRwy[k].end() && it->second.count(r)) on = true;
                }
                if (on) a += (a.empty() ? "" : " ") + std::string(r);
            }
            b.rwyActive[g] = a;
        }
    }

    {
        auto sw = RunwaySwitchTargets();
        b.rwySwitchCount = (int)sw.size();
        std::set<std::string> to;
        for (const auto& x : sw) to.insert(x.second);
        for (const std::string& x : to) b.rwySwitchTo += (b.rwySwitchTo.empty() ? "" : " ") + x;
    }
    // Stands: wer steht wo, wer ist wohin unterwegs (je Platz einmal)
    std::map<std::string, std::pair<std::map<std::string, std::vector<std::string>>, std::map<std::string, std::vector<std::string>>>> standOcc;
    auto occFor = [&](const std::string& apt) -> std::pair<std::map<std::string, std::vector<std::string>>, std::map<std::string, std::vector<std::string>>>& {
        auto f = standOcc.find(apt);
        if (f == standOcc.end())
        {
            f = standOcc.insert({ apt, {} }).first;
            StandOccupancy(apt, f->second.first, f->second.second);
        }
        return f->second;
    };

    std::string asel;
    CFlightPlan sel = FlightPlanSelectASEL();
    if (sel.IsValid()) asel = S(sel.GetCallsign());

    // Stands: Ankuenfte und Abfluege, die noch am Stand stehen (bis Startup), belegen ihren Stand
    std::map<std::string, std::vector<std::string>> standUse;
    for (const auto& kv : m_entries)
    {
        const Entry& e = kv.second;
        if (e.freeStrip || e.data.stand.empty()) continue;
        if (!e.arrival && !(e.bay == BAY_PENDING || e.bay == BAY_ONFREQ || e.bay == BAY_STARTUP)) continue;
        standUse[Upper(e.arrival ? e.data.ades : e.data.adep) + "|" + Upper(e.data.stand)].push_back(kv.first);
    }

    // Stellen, fuer die ich "zustaendig" bin: meine, mit Top-down auch die unbesetzten unteren
    std::vector<std::string> myUnits;
    {
        std::string me = MyUnit();
        myUnits.push_back(me);
        if (m_topDown && !me.empty())
        {
            bool apn = m_hasApron && b.apnOnline;
            char k = me[0];
            if (k == 'T' && !b.gndOnline)
            {
                myUnits.push_back("GND");
                if (!apn) myUnits.push_back("APN");
                if (!b.delOnline && !apn) myUnits.push_back("DEL");
            }
            if (k == 'G')
            {
                if (!apn) myUnits.push_back("APN");
                if (!b.delOnline && !apn) myUnits.push_back("DEL");
            }
            if (k == 'A' && !b.delOnline) myUnits.push_back("DEL");
        }
    }
    std::set<std::string> pushed;   // jeder Streifen nur einmal auf das Board
    for (int bay = 0; bay < BAY_COUNT; ++bay)
    {
        for (const std::string& cs : m_order[bay])
        {
            auto it = m_entries.find(cs);
            if (it == m_entries.end() || it->second.bay != bay || !pushed.insert(cs).second) continue;
            StripData d = it->second.data;
            d.selected = (cs == asel) && asel != m_aselHidden;   // ins Leere getippt: Auswahl aus
            d.localOnly = it->second.hasLocal && !it->second.localIntentional;
            d.handedOff = (bay == BAY_HANDOFF);
            d.toGround = (bay == BAY_TOGND);
            if (it->second.freeStrip)
            {
                // Notiz- und Designator-Strips: Farbe direkt aus dem Merker (kein Flugplan)
                auto h = m_hl.find(cs);
                d.hl = h == m_hl.end() ? 0 : h->second;
            }
            {
                // Zustaendigkeit, wie sie gilt. Ist der Flieger schon weiter, als meine Stelle ihn fuehrt (ein
                // anderer Lotse hat den Status gesetzt), ist er bei mir erledigt: noch kurz grau, dann weg
                d.resp = EffResp(cs);
                {
                    auto raw = m_resp.find(cs);
                    if (m_respOn && d.resp.empty() && raw != m_resp.end() && UnitIsMine(raw->second) && !m_handedAt.count(cs))
                        m_handedAt[cs] = m_tick;
                }
                // deins: gleiche Stelle, bei "Combined" auch beide Seiten, mit Top-down auch die
                // unbesetzten unteren Stellen, die du mitmachst
                // "APN" und "APNN" gehoeren zusammen (Seite nur, wenn beide eine haben)
                auto same = [](const std::string& a, const std::string& b) {
                    if (a.empty() || b.empty()) return false;
                    size_t n = std::min(a.size(), b.size());
                    return a.compare(0, n, b, 0, n) == 0;
                };
                d.respMine = false;
                for (const std::string& u : myUnits)
                    if (same(d.resp, u)) d.respMine = true;
                d.nextResp = NextRespUnit(cs);
                d.pinned = m_pinned.count(cs) > 0;
                {
                    auto pd = m_pushDir.find(cs);
                    d.pushDir = pd == m_pushDir.end() ? std::string() : pd->second;
                }
                {
                    auto tp = m_tlPlan.find(cs);
                    d.planUtc = tp == m_tlPlan.end() ? 0 : tp->second;
                }
                {
                    auto of = m_offer.find(cs);
                    d.offerTo = of == m_offer.end() ? std::string() : of->second;
                    d.offerForMe = false;
                    if (!d.offerTo.empty() && !m_offerByMe.count(cs))
                        for (const std::string& u : myUnits) if (same(d.offerTo, u)) d.offerForMe = true;
                    auto rj = m_rejectedAt.find(cs);
                    d.offerRejectedBy = (rj != m_rejectedAt.end() && m_tick - rj->second < kRejectShowSec && d.offerTo.empty())
                                            ? m_rejectedBy[cs] : std::string();
                    auto oa = m_offerAt.find(cs);
                    d.offerBlink = d.offerForMe && oa != m_offerAt.end() && m_tick - oa->second >= m_offerBlink;
                }
                d.diverted = m_divert.count(cs) > 0;
                auto ha = m_handedAt.find(cs);
                d.fading = ha != m_handedAt.end() && !d.respMine && !d.offerForMe && m_tick - ha->second < kFadeSec;
                if (!m_respOn)
                {
                    // Zustaendigkeit aus: niemand ist zustaendig - nichts ausblenden, nichts grau, keine Angebote
                    d.resp.clear();
                    d.respMine = false;
                    d.nextResp.clear();
                    d.offerTo.clear();
                    d.offerForMe = false;
                    d.offerBlink = false;
                    d.offerRejectedBy.clear();
                    d.fading = false;
                }
            }
            if (it->second.freeStrip)
            {
                auto h = m_hs.find(cs);
                d.holdShort = h == m_hs.end() ? std::string() : h->second;
            }
            else
            {
                // CPDLC / DCL
                auto dl = m_dl.find(cs);
                if (dl != m_dl.end())
                {
                    const DlState& st = dl->second;
                    d.dlLogon = st.logon;
                    d.dlDcl = st.dcl;
                    d.dlUnread = st.unread;
                    d.dlPending = st.pendingMin >= 0;
                    d.dlPendingText = st.pendingText;
                    d.dlRequest = st.dclRequest;
                    size_t from = st.log.size() > 12 ? st.log.size() - 12 : 0;
                    for (size_t k = from; k < st.log.size(); ++k)
                        d.dlLog.push_back({ DlTime(st.log[k].t), st.log[k].up, CpdlcDisplay(st.log[k].text), st.log[k].status });
                }
                if (b.dlEnabled)
                {
                    if (!d.arrival)
                    {
                        std::string err;
                        d.dlDclText = CpdlcDisplay(DlDclText(d, &err));
                        if (!err.empty()) d.dlDclText = "!" + err;
                    }
                    if (!d.nextFreq.empty()) d.dlContactText = DlUnitName(d.nextFreq, d.nextCtrl) + " " + d.nextFreq;
                }
                // Piste fuer die Richtung nicht aktiv (nur, wenn fuer den Platz ueberhaupt Pisten aktiv sind)
                const std::string& apt = d.arrival ? d.ades : d.adep;
                const std::string r = NormRwy(d.arrival ? d.arrRwy : d.rwy);
                auto& m = m_actRwy[d.arrival ? 1 : 0];
                auto a = m.find(Upper(apt));
                d.rwyInactive = !r.empty() && a != m.end() && !a->second.empty() && !a->second.count(r);
                // Ankunft: Stand laut GRP-Daten besetzt oder durch einen Nachbarn gesperrt; am Boden ohne Stand
                if (d.arrival && m_icao.stands.count(Upper(apt)))
                {
                    if (d.stand.empty()) d.standMissing = d.onGround;
                    else
                    {
                        auto& oc = occFor(Upper(apt));
                        std::string S0 = Upper(d.stand);
                        auto o = oc.first.find(S0);
                        if (o != oc.first.end())
                            for (const std::string& c : o->second) if (c != cs) { d.standBusy = 2; d.standBusyBy = c; break; }
                        auto inf = m_icao.standInfo.find(Upper(apt));
                        if (!d.standBusy && inf != m_icao.standInfo.end())
                            for (const auto& sb : inf->second)
                            {
                                if (std::find(sb.second.blocks.begin(), sb.second.blocks.end(), S0) == sb.second.blocks.end()) continue;
                                bool used = false;
                                for (auto* mp : { &oc.first, &oc.second })
                                {
                                    auto u = mp->find(sb.first);
                                    if (u != mp->end()) for (const std::string& c : u->second) if (c != cs) used = true;
                                }
                                if (used) { d.standBusy = 3; d.standBusyBy = sb.first; break; }
                            }
                    }
                }
                // Stand doppelt belegt
                auto sc = standUse.find(Upper(apt) + "|" + Upper(d.stand));
                if (!d.stand.empty() && sc != standUse.end() && sc->second.size() > 1 &&
                    std::find(sc->second.begin(), sc->second.end(), cs) != sc->second.end())
                {
                    d.standConflict = true;
                    for (const std::string& o : sc->second) if (o != cs) { d.standConflictWith = o; break; }
                }
            }
            b.bays[bay].push_back(d);
        }
    }
    // Papierkorb: entfernte Streifen (solange der Flieger noch in Reichweite ist),
    // weggeschickte Abfluege, entfernte Notiz-Streifen. Neueste zuerst.
    for (auto o = m_trashOrder.rbegin(); o != m_trashOrder.rend(); ++o)
    {
        if (!o->empty() && (*o)[0] == '#') b.trash.push_back({ *o, o->substr(1), "note" });
        else if (m_hidden.count(*o))
        {
            auto ha = m_hiddenArr.find(*o);
            b.trash.push_back({ *o, *o, m_parked.count(*o) ? "parked" : "removed", ha != m_hiddenArr.end() && ha->second });
        }
    }
    for (const auto& kv : m_hov)
    {
        if (!kv.second || m_entries.count(kv.first)) continue;
        CFlightPlan f = FlightPlanSelect(kv.first.c_str());
        if (f.IsValid() && f.GetDistanceFromOrigin() <= m_removeNm) b.trash.push_back({ kv.first, kv.first, "handed over" });
    }
    if (b.trash.size() > 30) b.trash.resize(30);
    // Remote: Ansicht und Bereich gelten fuer alle Geraete dieser Sitzung
    b.view = m_window.GetView();
    b.rwyFilter = m_window.GetRwyFilter();
    b.remoteOn = m_net.Running();
    b.remoteClients = m_hub.Count();
    b.remotePort = m_remotePort;
    b.remoteCode = m_remoteCode;
    b.remoteAddr = m_remoteAddr;
    m_window.SetBoard(b);
    m_hub.Board(b);
}

// vACDM-URL aus der vacdm.txt neben der geladenen vACDM.dll lesen,
// also genau der Server, mit dem auch dein vACDM-Plugin redet
std::string StripBayPlugin::FindVacdmUrl()
{
    HMODULE mod = GetModuleHandleW(L"vACDM.dll");
    if (!mod) return std::string();

    wchar_t path[MAX_PATH] = {};
    if (!GetModuleFileNameW(mod, path, MAX_PATH)) return std::string();
    std::wstring file = path;
    size_t slash = file.find_last_of(L"\\/");
    file = (slash == std::wstring::npos ? std::wstring() : file.substr(0, slash + 1)) + L"vacdm.txt";

    std::string url = "https://app.vacdm.net";   // Standard des vACDM-Plugins
    HANDLE h = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return url;
    std::string text;
    char buf[4096];
    DWORD read = 0;
    while (ReadFile(h, buf, sizeof(buf), &read, nullptr) && read > 0 && text.size() < 65536) text.append(buf, read);
    CloseHandle(h);

    for (const std::string& line : SplitKeep(text, '\n'))
    {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        if (Trim(line.substr(0, eq)) == "SERVER_url")
        {
            std::string v = Trim(line.substr(eq + 1));
            if (!v.empty()) url = v;
        }
    }
    return url;
}

// Wo liegen ICAO_Airlines.txt und ICAO_Airports.txt? Typisch irgendwo im
// Sektorpaket, also in der Naehe dieser DLL, oder unter Dokumente\EuroScope.
void StripBayPlugin::StartIcaoSearch()
{
    std::vector<std::wstring> roots;
    auto dirOf = [](std::wstring p) {
        size_t s = p.find_last_of(L"\\/");
        return s == std::wstring::npos ? std::wstring() : p.substr(0, s);
    };
    if (!m_icaoDir.empty())
    {
        roots.push_back(m_icaoDir);
    }
    else
    {
        wchar_t path[MAX_PATH] = {};
        if (GetModuleFileNameW((HINSTANCE)&__ImageBase, path, MAX_PATH))
        {
            std::wstring d = dirOf(path);
            for (int i = 0; i < 4 && d.size() > 3; ++i)
            {
                roots.push_back(d);
                d = dirOf(d);
            }
        }
        wchar_t env[MAX_PATH] = {};
        if (GetEnvironmentVariableW(L"USERPROFILE", env, MAX_PATH))
        {
            std::wstring up = env;
            roots.push_back(up + L"\\Documents\\EuroScope");
            roots.push_back(up + L"\\OneDrive\\Documents\\EuroScope");
            roots.push_back(up + L"\\OneDrive\\Dokumente\\EuroScope");
        }
        if (GetEnvironmentVariableW(L"APPDATA", env, MAX_PATH)) roots.push_back(std::wstring(env) + L"\\EuroScope");
        if (GetModuleFileNameW(nullptr, path, MAX_PATH)) roots.push_back(dirOf(path));   // EuroScope.exe
    }
    m_icaoLoader.Start(roots);
}

// Stands direkt aus dem geladenen Ground Radar Plugin: GRpluginStands.txt neben GRplugin.dll.
// Hat Vorrang vor einer gefundenen Datei; wird neu gelesen, wenn GRP sie aendert.
void StripBayPlugin::SyncGrpStands()
{
    HMODULE mod = GetModuleHandleW(L"GRplugin.dll");
    if (!mod) return;
    wchar_t path[MAX_PATH] = {};
    if (!GetModuleFileNameW(mod, path, MAX_PATH)) return;
    std::wstring file = path;
    size_t slash = file.find_last_of(L"\\/");
    file = (slash == std::wstring::npos ? std::wstring() : file.substr(0, slash + 1)) + L"GRpluginStands.txt";
    WIN32_FILE_ATTRIBUTE_DATA fa = {};
    if (!GetFileAttributesExW(file.c_str(), GetFileExInfoStandard, &fa)) return;
    std::wstring stamp = file + L"|" + std::to_wstring(fa.ftLastWriteTime.dwHighDateTime) + L"." +
                         std::to_wstring(fa.ftLastWriteTime.dwLowDateTime);
    if (stamp == m_grpStandsStamp) return;
    std::map<std::string, std::vector<std::string>> names;
    std::map<std::string, std::map<std::string, StandInfo>> info;
    if (!LoadStands(file, names, &info)) return;
    m_grpStandsStamp = stamp;
    for (auto& kv : names) m_icao.stands[kv.first] = kv.second;
    for (auto& kv : info) m_icao.standInfo[kv.first] = kv.second;
    bool first = m_icao.standsPath != file;
    m_icao.standsPath = file;
    if (first)
    {
        size_t n = 0;
        for (auto& kv : names) n += kv.second.size();
        MsgAuto("Stands from the Ground Radar plugin: " + std::to_string(n) + " stands (" + std::to_string(names.size()) + " airports).");
    }
    PushBoard();
}

void StripBayPlugin::UpdateVacdm()
{
    if (!m_vacdmEnabled)
    {
        m_vacdm.Stop();
        return;
    }
    std::string url = m_cdmUrl.empty() ? FindVacdmUrl() : m_cdmUrl;
    m_cdmUrlFound = url;
    if (url.empty())
    {
        m_vacdm.Stop();   // kein vACDM geladen und keine URL eingestellt
        return;
    }
    m_vacdm.SetUrl(url);
    m_vacdm.SetAirports(m_airports);
    m_vacdm.Start();
}

GroundCmd StripBayPlugin::CmdForBay(int bay, bool arrival) const
{
    if (arrival) return bay == BAY_TAXIIN ? GC_TXIN : GC_NSTS;
    switch (bay)
    {
    case BAY_PENDING:  return GC_NSTS;
    case BAY_ONFREQ:   return GC_ONFREQ;
    case BAY_STARTUP:  return GC_STUP;
    case BAY_PUSHBACK: return GC_PUSH;
    case BAY_TAXI:     return GC_TAXI;
    // Auf die Runway gezogen: Line-up (oder per .strips takeoff gleich DEPA).
    // Die Startfreigabe markiert man danach auf dem Streifen.
    case BAY_RUNWAY:   return m_runwayCmd;
    default:           return GC_NSTS;
    }
}

// ---------------------------------------------------------------------------
// Rueckmeldungen aus dem Fenster
// ---------------------------------------------------------------------------
// Merker fuer "Rueckgaengig": wo der Streifen stand und wer zustaendig war
void StripBayPlugin::PushUndo(int kind, const std::string& cs)
{
    if (m_undoing) return;
    auto it = m_entries.find(cs);
    if (it == m_entries.end() && kind != UNDO_REMOVE) return;
    UndoRec u;
    u.kind = kind;
    u.cs = cs;
    if (it != m_entries.end())
    {
        u.bay = it->second.bay;
        const auto& ord = m_order[u.bay];
        auto pos = std::find(ord.begin(), ord.end(), cs);
        u.index = pos == ord.end() ? -1 : (int)(pos - ord.begin());
    }
    auto rs = m_resp.find(cs);
    u.resp = rs == m_resp.end() ? std::string() : rs->second;
    m_undo.push_back(u);
    if (m_undo.size() > 10) m_undo.erase(m_undo.begin());
}

void StripBayPlugin::Undo()
{
    if (m_undo.empty()) { Msg("Nothing to undo."); return; }
    UndoRec u = m_undo.back();
    m_undo.pop_back();
    m_undoing = true;
    std::string what;
    switch (u.kind)
    {
    case UNDO_MOVE:
        if (m_entries.count(u.cs))
        {
            StripMoved(u.cs, u.bay, u.index);
            if (!m_entries[u.cs].freeStrip) SetResp(u.cs, u.resp.empty() ? std::string("NONE") : u.resp);
        }
        what = "move";
        break;
    case UNDO_RESP:
        if (m_entries.count(u.cs)) SetResp(u.cs, u.resp.empty() ? std::string("NONE") : u.resp);
        what = "responsibility";
        break;
    case UNDO_OFFER:
        OfferResp(u.cs, "CANCEL");
        what = "handover";
        break;
    case UNDO_REMOVE:
        RestoreStrip(m_trashFree.empty() || std::find_if(m_trashFree.begin(), m_trashFree.end(),
                         [&](const FreeTrash& f) { return f.key == u.cs; }) == m_trashFree.end() ? u.cs : "#" + u.cs);
        what = "remove";
        break;
    }
    m_undoing = false;
    PushBoard();
    Msg(u.cs + ": " + what + " undone.");
}

void StripBayPlugin::StripMoved(const std::string& cs, int bay, int index)
{
    auto it = m_entries.find(cs);
    if (it == m_entries.end() || bay < 0 || bay >= BAY_COUNT) return;
    PushUndo(UNDO_MOVE, cs);
    Entry& e = it->second;
    if (e.freeStrip)
    {
        // Notiz-Streifen: ueberallhin, nur lokal
        MoveOrder(cs, e.bay, bay, index);
        e.bay = bay;
        PushBoard();
        return;
    }
    if (!CanDrop(e.arrival, bay))
    {
        PushBoard();
        return;
    }

    int from = e.bay;
    if (from == bay)
    {
        MoveOrder(cs, from, bay, index);   // nur umsortieren
        PushBoard();
        return;
    }
    // Wer verschiebt, ist zustaendig. In eine Uebergabe-Bay geschoben = der naechsten Stelle geben (GiveTo):
    // mit StripBay dort ein Angebot, sonst direkt
    RespOnExit resp{ this, cs, MyUnit() };
    resp.on = true;
    // Stelle zu einer Uebergabe-Bay: die von HandOver gewaehlte, sonst die naechste laut Kette, sonst nach Piste
    auto handTarget = [&](const char* base) -> std::string {
        if (!m_handTo.empty()) return m_handTo;
        std::string n = NextRespUnit(cs), b = base;
        if (n.compare(0, 3, b) == 0) return n;
        return b + (b == "TWR" ? SideOf(cs) : b == "APN" ? std::string() : (m_splitGnd ? SideOf(cs) : std::string()));
    };
    std::string giveTo;

    // Ankuenfte: Final <-> Runway <-> Taxi In GND <-> Taxi In Apron
    if (e.arrival)
    {
        auto c = m_ctl.find(cs);
        bool ctl = (c != m_ctl.end() && c->second);
        if (bay == BAY_TAXIIN)
        {
            // Ground uebergibt an Apron: Ground State TXIN (Taxi In), fuer alle sichtbar
            e.released = true;
            e.onRunway = false;
            giveTo = handTarget("APN");
            resp.on = false;
            ChangeState(cs, GC_TXIN, BAY_TAXIIN, index);
            if (m_entries.count(cs)) GiveTo(cs, giveTo);
            return;
        }
        if (bay == BAY_GNDIN && e.data.state == GC_TXIN)
        {
            // Uebergabe an Apron zuruecknehmen
            e.released = true;
            if (m_offerByMe.count(cs)) CancelOffer(cs, false);
            ChangeState(cs, GC_NSTS, BAY_GNDIN, index);
            return;
        }
        if (bay == BAY_GNDIN)
        {
            // Tower gibt die Ankunft an Ground ab (fuer alle: ".SB_AP_REL"). Das CTL-Flag
            // bleibt wie bei vSID nach der Landung stehen.
            e.released = true;
            e.onRunway = false;
            giveTo = handTarget("GND");
            resp.on = false;
            SendArrPos(cs, "REL");
        }
        else if (bay == BAY_RUNWAY)
        {
            // Auf die Piste gelegt, die Landefreigabe markiert man auf dem Streifen.
            // Schon gelandet und abgegeben: nicht zurueck auf die Piste.
            if (e.data.onGround && from != BAY_RUNWAY)
            {
                PushBoard();
                return;
            }
            e.onRunway = true;
            e.released = false;
            resp.unit = "TWR" + SideOf(cs);
            SendArrPos(cs, "RWY");
        }
        else   // BAY_FINAL: zurueck in den Anflug, eine Landefreigabe wird zurueckgenommen
        {
            e.onRunway = false;
            e.released = false;
            if (ctl) SetCtl(cs, false);
            resp.unit = "TWR" + SideOf(cs);
            SendArrPos(cs, "FIN");
        }
        auto again = m_entries.find(cs);
        if (again == m_entries.end())
        {
            PushBoard();
            return;
        }
        MoveOrder(cs, from, bay, index);
        again->second.bay = bay;
        CFlightPlan fresh = FlightPlanSelect(cs.c_str());
        if (fresh.IsValid()) UpdateEntry(fresh, true);
        PushBoard();
        if (!giveTo.empty() && m_entries.count(cs)) GiveTo(cs, giveTo);
        return;
    }

    // An Tower (Ground) bzw. an Ground (Apron) uebergeben: der Ground State bleibt TAXI,
    // die Uebergabe geht als ".SB_XF_TWR/GND" an alle (gilt, solange der Flieger rollt)
    if (bay == BAY_HANDOFF || bay == BAY_TOGND)
    {
        if (e.data.state != GC_TAXI)
        {
            // erst rollen lassen: Status TAXI setzen, dann uebergeben
            ChangeState(cs, GC_TAXI, BAY_TAXI, -1);
            auto again = m_entries.find(cs);
            if (again == m_entries.end() || again->second.data.state != GC_TAXI)
            {
                PushBoard();
                return;
            }
        }
        auto it2 = m_entries.find(cs);
        if (it2 == m_entries.end()) return;
        Entry& e2 = it2->second;
        MoveOrder(cs, e2.bay, bay, index);
        e2.bay = bay;
        e2.hasLocal = false;
        e2.localIntentional = false;
        SendXfer(cs, bay == BAY_HANDOFF ? "TWR" : "GND");
        giveTo = handTarget(bay == BAY_HANDOFF ? "TWR" : "GND");
        resp.on = false;
        PushBoard();
        GiveTo(cs, giveTo);
        return;
    }
    // Uebergabe zuruecknehmen (zurueck nach Taxi)
    if (!e.arrival && (from == BAY_HANDOFF || from == BAY_TOGND) && m_xfer.count(cs))
    {
        SendXfer(cs, "NONE");
        if (m_offerByMe.count(cs)) CancelOffer(cs, false);
    }

    // Passt der aktuelle ES-Status schon zur Ziel-Bay (z. B. Landefreigabe
    // zurueckgenommen oder Uebergabe rueckgaengig), muss nichts gesetzt werden
    if ((e.arrival ? ArrivalBayOf(e, e.data.state) : DepartureBay(e.data.state)) == bay)
    {
        MoveOrder(cs, from, bay, index);
        e.bay = bay;
        e.hasLocal = false;
        e.localIntentional = false;
        PushBoard();
        return;
    }

    ChangeState(cs, CmdForBay(bay, e.arrival), bay, index);
}

// ---------------------------------------------------------------------------
// Zustaendigkeit: wer den Flieger gerade hat (DEL, Apron, Ground, Tower; N/S).
// Wie bei vSID ueber ein kurzes Scratchpad ".SB_RS_<UNIT>" an alle mit StripBay.
// Gesetzt wird sie, wenn du einen Flieger bearbeitest (Status, verschieben, Freigabe),
// bei Uebergaben gleich auf die uebernehmende Stelle; von Hand im Feld RESPONSIBLE.
// ---------------------------------------------------------------------------
// Lotsen an deinen Plaetzen (mit Frequenz, ohne dich). Kommt einer neu dazu, verteilt
// StripBay ein paar Sekunden spaeter einmal die Merker deiner Flieger (wie ein spaeter
// "Publish" im GRP), damit der Neue Zustaendigkeit, Hold Short und Uebergaben sieht.
void StripBayPlugin::ScanControllers()
{
    std::set<std::string> now;
    std::set<std::string> apts = m_airports;
    apts.insert(m_arrAirports.begin(), m_arrAirports.end());
    for (CController c = ControllerSelectFirst(); c.IsValid(); c = ControllerSelectNext(c))
    {
        std::string f, w;
        if (!ControllerFreq(c, f, w)) continue;   // nur Lotsen mit Frequenz, nicht ich
        std::string cs = Upper(w);
        for (const std::string& a : apts)
            if (cs.size() > a.size() && cs.compare(0, a.size() + 1, a + "_") == 0) { now.insert(cs); break; }
    }
    bool fresh = false;
    if (m_ctrlScanned)
        for (const std::string& c : now) if (!m_knownCtrls.count(c)) fresh = true;
    m_knownCtrls.swap(now);
    m_ctrlScanned = true;
    if (m_knownCtrls.empty() && !m_verWarn.empty()) { m_verWarn.clear(); m_verTold.clear(); }   // allein am Platz
    if (fresh) { m_resyncAt = m_tick + 5; m_announceAgain = true; }
    // gemeldete StripBay-Stellen, deren Lotse nicht mehr da ist, vergessen
    for (auto it = m_sbUnits.begin(); it != m_sbUnits.end();)
    {
        const std::string b = it->first.substr(0, 3);
        bool on = b == "DEL" ? DelOnline() : b == "APN" ? ApronOnline() : b == "GND" ? GndOnline() : TwrOnline();
        if (on) ++it;
        else it = m_sbUnits.erase(it);
    }
}

// Merker der Flieger, fuer die du zustaendig bist, noch einmal an alle schicken
int StripBayPlugin::Resync()
{
    std::string me = MyUnit();
    int n = 0;
    // On Freq ohne Zustaendigkeit (z. B. noch bei niemandem): jeder schickt es, damit ein
    // gerade eingeloggter Lotse den Status sieht (doppelt schadet nicht)
    for (const auto& kv : m_entries)
    {
        if (kv.second.freeStrip || kv.second.arrival || !m_onFreq.count(kv.first)) continue;
        auto r = m_resp.find(kv.first);
        bool mine = !me.empty() && r != m_resp.end() && r->second.compare(0, me.size(), me) == 0;
        // nur einer soll senden: Delivery, ohne Delivery online jede andere Stelle
        if (r == m_resp.end() && !mine && (me == "DEL" || (!DelOnline() && !me.empty()))) { SendScratch(kv.first, ".SB_OF_TRUE"); ++n; }
    }
    if (me.empty()) return n;
    for (const auto& kv : m_entries)
    {
        if (kv.second.freeStrip) continue;
        const std::string& cs = kv.first;
        auto r = m_resp.find(cs);
        if (!m_respOn)
        {
            // ohne Zustaendigkeit: Merker, die es fuer den Flieger gibt, schicken (doppelt schadet nicht)
            bool any = false;
            auto h = m_hs.find(cs);
            if (h != m_hs.end() && !h->second.empty()) { SendScratch(cs, ".SB_HS_" + h->second); any = true; }
            auto x = m_xfer.find(cs);
            if (x != m_xfer.end()) { SendScratch(cs, ".SB_XF_" + x->second); any = true; }
            auto g = m_ga.find(cs);
            if (g != m_ga.end() && g->second) { SendScratch(cs, ".SB_GA_TRUE"); any = true; }
            if (m_lineup.count(cs)) { SendScratch(cs, ".SB_LU_TRUE"); any = true; }
            if (m_stdClr.count(cs)) { SendScratch(cs, ".SB_SC_TRUE"); any = true; }
            auto pd = m_pushDir.find(cs);
            if (pd != m_pushDir.end()) { SendScratch(cs, ".SB_PB_" + pd->second); any = true; }
            if (ResyncExtras(cs)) any = true;
            if (m_onFreq.count(cs) && r != m_resp.end()) { SendScratch(cs, ".SB_OF_TRUE"); any = true; }   // sonst schon oben
            if (any) ++n;
            continue;
        }
        if (r == m_resp.end() || r->second.compare(0, me.size(), me) != 0) continue;
        SendScratch(cs, ".SB_RS_" + r->second);
        auto h = m_hs.find(cs);
        if (h != m_hs.end() && !h->second.empty()) SendScratch(cs, ".SB_HS_" + h->second);
        auto x = m_xfer.find(cs);
        if (x != m_xfer.end()) SendScratch(cs, ".SB_XF_" + x->second);
        auto g = m_ga.find(cs);
        if (g != m_ga.end() && g->second) SendScratch(cs, ".SB_GA_TRUE");
        if (m_onFreq.count(cs)) SendScratch(cs, ".SB_OF_TRUE");
        if (m_stdClr.count(cs)) SendScratch(cs, ".SB_SC_TRUE");
        if (m_lineup.count(cs)) SendScratch(cs, ".SB_LU_TRUE");
        auto hl = m_hl.find(cs);
        if (hl != m_hl.end()) SendScratch(cs, ".SB_HL_" + std::to_string(hl->second));
        auto of = m_offer.find(cs);
        if (of != m_offer.end() && m_offerByMe.count(cs)) SendScratch(cs, ".SB_HO_" + of->second);
        auto pd = m_pushDir.find(cs);
        if (pd != m_pushDir.end()) SendScratch(cs, ".SB_PB_" + pd->second);
        ResyncExtras(cs);
        auto tp = m_tlPlan.find(cs);
        if (tp != m_tlPlan.end()) SendScratch(cs, ".SB_TP_" + Hhmmss(tp->second));
        auto dv = m_divert.find(cs);
        if (dv != m_divert.end()) SendScratch(cs, ".SB_DV_" + dv->second);
        else if (m_ioManual.count(cs) && m_forced.count(cs)) SendScratch(cs, std::string(".SB_IO_") + (m_forced[cs] ? "ARR" : "DEP"));
        ++n;
    }
    return n;
}

// ---------------------------------------------------------------------------
// Uebergabe (4.0.0). Eine Regel fuer alle Wege (Weiter-Taste, Ziehen, Menue, Zeitstrahl):
//   - Wer einen Streifen bearbeitet, ist zustaendig.
//   - Uebergeben = der naechsten Stelle anbieten. Sie nimmt an (ACCEPT oder indem sie ihn bearbeitet);
//     bis dahin bleibt er bei mir ("-> TWR"). Ablehnen oder Zuruecknehmen: er steht wieder ganz bei mir.
//   - Hat die naechste Stelle kein StripBay (sie hat sich nicht gemeldet), wird sie direkt eingetragen
//     und der Streifen ist bei mir erledigt.
//   - Ein Streifen einer anderen Stelle, der gleich zu mir kommt (grau), laesst sich uebernehmen (Take).
//   - Setzt ein Lotse ohne StripBay den Status weiter, als meine Stelle den Flieger fuehrt, gilt meine
//     Zustaendigkeit nicht mehr (EffResp): der Streifen ist frei und rutscht nirgends zurueck.
// ---------------------------------------------------------------------------
// Tower besetzt: jemand (nicht ich) als <Platz>_*TWR
bool StripBayPlugin::TwrOnline()
{
    std::string a = m_profileIcao;
    if (a.empty()) return true;
    for (CController c = ControllerSelectFirst(); c.IsValid(); c = ControllerSelectNext(c))
    {
        std::string cf, cw;
        if (!ControllerFreq(c, cf, cw)) continue;
        if (Glob(a + "_*TWR", Upper(cw))) return true;
    }
    return false;
}

// Hat diese Stelle StripBay (mit Zustaendigkeit)? "GND" passt zu "GNDN" und umgekehrt, "TWRN" nicht zu "TWRS".
bool StripBayPlugin::UnitHasSb(const std::string& unit) const
{
    for (const auto& kv : m_sbUnits)
    {
        size_t n = std::min(kv.first.size(), unit.size());
        if (n >= 3 && kv.first.compare(0, n, unit, 0, n) == 0) return true;
    }
    return false;
}

// Eigene Stelle melden (bzw. abmelden): beim Login, wenn sich Station oder Bereich aendern, wenn ein
// Lotse neu dazukommt und wenn die Zustaendigkeit ein- oder ausgeschaltet wird
void StripBayPlugin::Announce()
{
    std::string want = m_respOn ? MyUnit() : std::string();
    if (want == m_announced && !m_announceAgain && m_verSent) return;
    if (m_tick < 3) return;
    CController me = ControllerMyself();
    if (!me.IsValid() || !me.IsController()) return;
    for (const auto& kv : m_entries)
    {
        if (kv.second.freeStrip) continue;
        CFlightPlan f = FlightPlanSelect(kv.first.c_str());
        // nur ueber einen Flieger, dessen Scratchpad ich setzen darf (nicht von anderen getrackt)
        if (!f.IsValid() || (!f.GetTrackingControllerIsMe() && !Trim(S(f.GetTrackingControllerCallsign())).empty())) continue;
        if (!m_announced.empty() && m_announced != want) SendScratch(kv.first, ".SB_UX_" + m_announced);
        if (!want.empty()) SendScratch(kv.first, ".SB_UN_" + want);
        SendScratch(kv.first, ".SB_VN_" + VersionWire());   // eigene Version (andere Hauptversion = Warnung bei allen)
        m_verSent = true;
        m_announced = want;
        m_announceAgain = false;
        return;
    }
}

// Zustaendigkeit, wie sie gilt. Ist der Flieger schon weiter, als die eingetragene Stelle ihn fuehren
// kann, gilt der Eintrag nicht mehr (leer = frei). Jede StripBay rechnet das aus denselben Daten gleich
// aus, es braucht keine Nachricht.
std::string StripBayPlugin::EffResp(const std::string& cs) const
{
    auto r = m_resp.find(cs);
    if (r == m_resp.end() || r->second.empty()) return std::string();
    auto e = m_entries.find(cs);
    if (e == m_entries.end() || e->second.freeStrip) return r->second;
    const std::string base = r->second.substr(0, 3);
    const int bay = e->second.bay;
    if (!e->second.arrival)
    {
        // Delivery fuehrt bis zum Startup, Apron und Ground bis zum Rollhalt
        if (base == "DEL" && (bay == BAY_PUSHBACK || bay == BAY_TAXI || bay == BAY_TOGND || bay == BAY_HANDOFF || bay == BAY_RUNWAY))
            return std::string();
        if ((base == "APN" || base == "GND") && bay == BAY_RUNWAY) return std::string();
    }
    else if (base == "TWR" && bay == BAY_TAXIIN) return std::string();   // Tower fuehrt bis Taxi In (Ground)
    // Gemischter Betrieb (4.1.0): Die eingetragene Stelle ist besetzt, nutzt aber kein StripBay - sie wird nie
    // uebergeben. Fuer alle Stellen NACH ihr ist der Streifen deshalb frei (normal zu sehen und direkt zu
    // bearbeiten, ohne erst zu uebernehmen). Fuer die Stellen vor ihr bleibt er abgegeben.
    {
        StripBayPlugin* self = const_cast<StripBayPlugin*>(this);
        const std::string me = MyUnit();
        if (m_respOn && !me.empty() && !self->UnitIsMine(r->second) && !UnitHasSb(r->second))
        {
            auto rank = [&](const std::string& u) {
                const std::string b = u.substr(0, 3);
                if (!e->second.arrival) return b == "DEL" ? 0 : b == "APN" ? 1 : b == "GND" ? 2 : 3;
                return b == "TWR" ? 0 : b == "GND" ? 1 : 2;
            };
            if (rank(me) > rank(r->second)) return std::string();
        }
    }
    return r->second;
}

// An eine Stelle geben: eigene bzw. mitgemachte Stelle = sofort; Stelle mit StripBay = Angebot, bis zur
// Annahme bleibe ich zustaendig; sonst (Lotse ohne StripBay) direkt eintragen.
void StripBayPlugin::GiveTo(const std::string& cs, const std::string& unit)
{
    if (!m_respOn || !IsRespUnit(unit)) return;
    auto e = m_entries.find(cs);
    if (e == m_entries.end() || e->second.freeStrip) return;
    if (UnitIsMine(unit) || !UnitHasSb(unit))
    {
        CancelOffer(cs, false);
        SetResp(cs, unit);
        return;
    }
    std::string me = MyUnit();
    if (!me.empty() && !UnitIsMine(EffResp(cs))) SetResp(cs, me);
    auto of = m_offer.find(cs);
    if (of != m_offer.end() && of->second == unit && m_offerByMe.count(cs)) { PushBoard(); return; }
    m_offer[cs] = unit;
    m_offerByMe.insert(cs);
    m_offerAt[cs] = m_tick;
    m_offerReminded.erase(cs);
    m_rejectedAt.erase(cs);
    m_rejectedBy.erase(cs);
    SendScratch(cs, ".SB_HO_" + unit);
    PushBoard();
}

// Uebergeben an die naechste Stelle (unit leer) bzw. an eine bestimmte. Wo es eine Uebergabe-Bay gibt
// (rollender Abflug an Tower/Ground, gelandete Ankunft an Ground/Apron), gehoert sie dazu.
void StripBayPlugin::HandOver(const std::string& cs, const std::string& unitIn)
{
    auto it = m_entries.find(cs);
    if (it == m_entries.end() || it->second.freeStrip) return;
    if (!m_respOn) { Msg("Responsibility is off - no handovers. On: .strips responsibility on"); PushBoard(); return; }
    std::string unit = unitIn.empty() ? NextRespUnit(cs) : unitIn;
    if (!IsRespUnit(unit)) { Msg(cs + ": no next station to hand over to."); PushBoard(); return; }
    const Entry& e = it->second;
    const std::string base = unit.substr(0, 3);
    int bay = -1;
    if (!e.arrival)
    {
        if (e.data.state == GC_TAXI && base == "TWR") bay = BAY_HANDOFF;
        else if (e.data.state == GC_TAXI && base == "GND" && MyUnit().compare(0, 3, "APN") == 0) bay = BAY_TOGND;
    }
    else
    {
        if (base == "GND" && (e.bay == BAY_RUNWAY || e.bay == BAY_FINAL) && (e.data.onGround || e.released)) bay = BAY_GNDIN;
        else if (base == "APN" && e.bay == BAY_GNDIN && m_hasApron) bay = BAY_TAXIIN;
    }
    if (bay >= 0 && bay != e.bay)
    {
        m_handTo = unit;
        StripMoved(cs, bay, -1);
        m_handTo.clear();
        return;
    }
    PushUndo(UNDO_OFFER, cs);
    GiveTo(cs, unit);
}

// Eigenes Angebot zuruecknehmen (revertBay: Abflug aus der Uebergabe-Bay zurueck nach Taxi)
void StripBayPlugin::CancelOffer(const std::string& cs, bool revertBay)
{
    if (m_offer.erase(cs)) SendScratch(cs, ".SB_HO_NONE");
    m_offerByMe.erase(cs);
    m_offerAt.erase(cs);
    m_offerReminded.erase(cs);
    if (revertBay && m_xfer.count(cs))
    {
        SendXfer(cs, "NONE");
        CFlightPlan fp = FlightPlanSelect(cs.c_str());
        auto e = m_entries.find(cs);
        if (fp.IsValid() && e != m_entries.end()) UpdateEntry(fp, e->second.arrival);
    }
}

// Uebernehmen: einen Streifen einer anderen Stelle (grau) zu mir holen; ein Angebot an mich = annehmen
void StripBayPlugin::TakeOver(const std::string& cs)
{
    auto e = m_entries.find(cs);
    if (e == m_entries.end() || e->second.freeStrip) return;
    if (!m_respOn) { Msg("Responsibility is off. On: .strips responsibility on"); return; }
    std::string me = MyUnit();
    if (me.empty()) { Msg("Choose your station first (Delivery, Apron, Ground, Tower)."); return; }
    auto of = m_offer.find(cs);
    if (of != m_offer.end() && !m_offerByMe.count(cs) && UnitIsMine(of->second)) { AcceptOffer(cs); return; }
    PushUndo(UNDO_RESP, cs);
    SetResp(cs, me);
}

// Naechste Stelle fuer einen Streifen (wer ihn nach mir bekommt), nach Richtung und wer online ist:
// Abflug: DEL -> Apron (Apron-Stand) bzw. Ground -> Tower; Ankunft: Tower -> Ground -> Apron.
// Mit Seite (Nord/Sued) nach der Piste, wo es die gibt. Leer = keine.
std::string StripBayPlugin::NextRespUnit(const std::string& cs)
{
    auto it = m_entries.find(cs);
    if (it == m_entries.end() || it->second.freeStrip) return std::string();
    const StripData& d = it->second.data;
    std::string me = MyUnit();
    std::string cur = EffResp(cs);
    // von meiner Stelle aus, wenn der Streifen meiner ist (auch per Top-down) oder niemandem gehoert
    std::string from = (!cur.empty() && !UnitIsMine(cur)) ? cur : (!me.empty() ? me : cur);
    std::string base = from.substr(0, 3);
    if (base.empty())
    {
        // keine eigene Stelle (All Bays): aus der Bay ableiten
        int bay = it->second.bay;
        bool apnHere = m_hasApron && ApronOnline() && !d.gndStand;
        if (!d.arrival)
            base = (bay == BAY_PENDING || bay == BAY_ONFREQ) ? "DEL"
                 : (bay == BAY_STARTUP || bay == BAY_PUSHBACK) ? (apnHere ? "APN" : "GND")
                 : (bay == BAY_TAXI || bay == BAY_TOGND) ? "GND" : "";
        else
            base = (bay == BAY_FINAL || bay == BAY_RUNWAY) ? "TWR" : (bay == BAY_GNDIN) ? "GND" : "";
    }
    std::string side = d.rwyGroup == RWY_A ? "N" : d.rwyGroup == RWY_B ? "S" : "";
    if (!SplitActive()) side.clear();
    std::string gside = m_splitGnd ? side : std::string();
    bool apn = m_hasApron && ApronOnline(), gnd = GndOnline();
    // Apron der Seite: ist das eigene nicht da, macht das andere mit (Berlin: Main Apron fuer Apron Nord)
    std::string aside = gside;
    if (apn && m_profileIcao == "EDDB" && (gside == "N" || gside == "S"))
    {
        char me1 = gside[0], other = me1 == 'N' ? 'S' : 'N';
        if (!ApronSideOnline(me1) && ApronSideOnline(other)) aside = std::string(1, other);
    }
    std::string next;
    if (!d.arrival)
    {
        if (base == "DEL") next = (apn && !d.gndStand) ? "APN" + aside : gnd ? "GND" + gside : "TWR" + side;
        else if (base == "APN") next = gnd ? "GND" + gside : "TWR" + side;
        else if (base == "GND") next = "TWR" + side;
    }
    else
    {
        if (base == "TWR") next = gnd ? "GND" + gside : (apn ? "APN" + aside : std::string());
        else if (base == "GND") next = (apn && !d.gndStand) ? "APN" + aside : std::string();
    }
    return IsRespUnit(next) ? next : std::string();
}

// Gehoert diese Stelle mir (eigene Stelle, mit Top-down auch die mitgemachten)?
bool StripBayPlugin::UnitIsMine(const std::string& u)
{
    std::string me = MyUnit();
    if (u.empty() || me.empty()) return false;
    std::vector<std::string> mine = { me };
    if (m_topDown)
    {
        bool apn = m_hasApron && ApronOnline(), del = DelOnline(), gnd = GndOnline();
        char k = me[0];
        if (k == 'T' && !gnd) { mine.push_back("GND"); if (!apn) mine.push_back("APN"); if (!del && !apn) mine.push_back("DEL"); }
        if (k == 'G') { if (!apn) mine.push_back("APN"); if (!del && !apn) mine.push_back("DEL"); }
        if (k == 'A' && !del) mine.push_back("DEL");
    }
    for (const std::string& m : mine)
    {
        size_t n = std::min(m.size(), u.size());
        if (u.compare(0, n, m, 0, n) == 0) return true;
    }
    return false;
}

// Feld oben rechts bzw. Befehl: an eine Stelle uebergeben, CANCEL = Angebot zuruecknehmen, ACCEPT, TAKE,
// NONE = freigeben
void StripBayPlugin::OfferResp(const std::string& cs, const std::string& unit)
{
    auto e = m_entries.find(cs);
    if (e == m_entries.end() || e->second.freeStrip) return;
    if (!m_respOn) { Msg("Responsibility is off - no handovers. On: .strips responsibility on"); PushBoard(); return; }
    if (unit == "CANCEL") { CancelOffer(cs, true); PushBoard(); return; }
    if (unit == "ACCEPT") { AcceptOffer(cs); return; }
    if (unit == "TAKE") { TakeOver(cs); return; }
    if (unit == "NONE") { PushUndo(UNDO_RESP, cs); CancelOffer(cs, true); SetResp(cs, unit); return; }
    if (!IsRespUnit(unit)) return;
    if (UnitIsMine(unit)) { TakeOver(cs); return; }
    HandOver(cs, unit);
}

void StripBayPlugin::AcceptOffer(const std::string& cs)
{
    if (!m_respOn) return;
    auto of = m_offer.find(cs);
    if (of == m_offer.end()) return;
    std::string u = of->second;
    // angeboten "GND", ich bin "GNDN": meine genaue Stelle eintragen
    std::string me = MyUnit();
    if (!me.empty() && me.compare(0, 3, u, 0, 3) == 0 && me.size() > u.size()) u = me;
    SetResp(cs, u);   // loescht das Angebot und meldet .SB_HO_NONE
}

// Angebot ablehnen: geht an alle (".SB_HX_<meine Stelle>"), beim Anbietenden rot
void StripBayPlugin::RejectOffer(const std::string& cs)
{
    if (!m_respOn) return;
    auto of = m_offer.find(cs);
    if (of == m_offer.end() || m_offerByMe.count(cs)) return;
    std::string me = MyUnit();
    m_offer.erase(of);
    m_offerAt.erase(cs);
    m_offerReminded.erase(cs);
    SendScratch(cs, ".SB_HX_" + (me.empty() ? std::string("X") : me));
    PushBoard();
}

// Richtung von Hand setzen: bleibt wie ein von Hand erstellter Streifen stehen
void StripBayPlugin::SetDirection(const std::string& cs, bool arrival, bool send)
{
    CFlightPlan fp = FlightPlanSelect(cs.c_str());
    if (!fp.IsValid()) return;
    auto e = m_entries.find(cs);
    if (e != m_entries.end() && e->second.freeStrip) return;
    m_forced[cs] = arrival;
    m_ioManual.insert(cs);
    m_hidden.erase(cs);
    m_hiddenArr.erase(cs);
    if (send) SendScratch(cs, std::string(".SB_IO_") + (arrival ? "ARR" : "DEP"));
    UpdateEntry(fp, arrival);
    PushBoard();
}

// Diversion von Hand: Ankunft an apt (leer = aufheben; Streifen bleibt dann, wie er ist)
void StripBayPlugin::SetDivert(const std::string& cs, const std::string& apt, bool send)
{
    CFlightPlan fp = FlightPlanSelect(cs.c_str());
    if (apt.empty())
    {
        if (!m_divert.erase(cs)) return;
        if (send) SendScratch(cs, ".SB_DV_NONE");
        if (fp.IsValid() && m_entries.count(cs)) UpdateEntry(fp, m_entries[cs].arrival);
        PushBoard();
        return;
    }
    if (!fp.IsValid()) return;
    m_divert[cs] = apt;
    if (send) SendScratch(cs, ".SB_DV_" + apt);
    SetDirection(cs, true, false);
}

bool StripBayPlugin::IsRespUnit(const std::string& u)
{
    static const char* units[] = { "DEL", "APN", "APNN", "APNS", "GND", "GNDN", "GNDS", "TWR", "TWRN", "TWRS" };
    for (const char* x : units) if (u == x) return true;
    return false;
}

// Deine Stelle aus Ansicht und Bereich (All Bays: keine)
std::string StripBayPlugin::MyUnit() const
{
    int f = m_window.GetRwyFilter();
    std::string side = !SplitActive() ? std::string() : f == RWY_A ? "N" : f == RWY_B ? "S" : "";
    std::string gside = m_splitGnd ? side : std::string();   // Leipzig: ein Ground
    switch (m_window.GetView())
    {
    case VIEW_DEL: return "DEL";
    case VIEW_APN: return "APN" + gside;
    case VIEW_GND: return "GND" + gside;
    case VIEW_TWR: return "TWR" + side;
    default:       return std::string();
    }
}

// Seite eines Fliegers (nach seiner Piste): "N", "S" oder leer
std::string StripBayPlugin::SideOf(const std::string& cs) const
{
    if (!m_split.enabled) return std::string();
    auto it = m_entries.find(cs);
    if (it == m_entries.end()) return std::string();
    int g = it->second.data.rwyGroup;
    return g == RWY_A ? "N" : g == RWY_B ? "S" : "";
}

// Zustaendigkeit an/aus. Aus: StripBay traegt niemanden mehr als zustaendig ein (auch nicht, wer einen
// Streifen bearbeitet), keine Angebote, Anforderungen und grauen Vorankuendigungen; die Streifen stehen
// nur nach Ground State in den Spalten. Eigene Zustaendigkeiten werden beim Ausschalten freigegeben.
void StripBayPlugin::SetRespOn(bool on)
{
    if (on == m_respOn)
    {
        Msg(std::string("Responsibility already ") + (on ? "on." : "off."));
        return;
    }
    int released = 0;
    if (!on)
    {
        std::vector<std::string> mine, offers;
        for (const auto& kv : m_resp)
            if (UnitIsMine(kv.second)) mine.push_back(kv.first);
        for (const std::string& c : m_offerByMe) offers.push_back(c);
        for (const std::string& c : offers)
            if (m_offer.erase(c)) SendScratch(c, ".SB_HO_NONE");
        m_offerByMe.clear();
        for (const std::string& c : mine)
        {
            SetResp(c, "NONE");
            ++released;
        }
        m_handedAt.clear();
        m_rejectedAt.clear();
        m_rejectedBy.clear();
    }
    m_respOn = on;
    SaveDataToSettings("Responsibility", "StripBay Zustaendigkeit (1 an, 0 aus)", on ? "1" : "0");
    if (on)
        Msg("Responsibility on: whoever works a strip becomes responsible, handovers as before.");
    else
        Msg("Responsibility off: nobody is entered as responsible, strips follow the ground state only" +
            (released ? " (" + std::to_string(released) + " of yours released)." : std::string(".")));
    PushBoard();
}

void StripBayPlugin::SetResp(const std::string& cs, const std::string& unit)
{
    if (unit.empty()) return;
    if (!m_respOn && unit != "NONE") return;   // Zustaendigkeit aus: niemanden eintragen
    if (unit == "NONE")
    {
        if (!m_resp.count(cs)) return;
        m_resp.erase(cs);
        m_handedAt.erase(cs);
    }
    else
    {
        if (!IsRespUnit(unit)) return;
        auto it = m_resp.find(cs);
        if (it != m_resp.end() && it->second == unit) return;
        // eingetragen ist meine Stelle mit Seite ("APNN"), ich arbeite ohne Seite ("APN", Combined): dieselbe Stelle
        if (it != m_resp.end() && unit == MyUnit() && it->second.size() > unit.size() && it->second.compare(0, unit.size(), unit) == 0 &&
            !EffResp(cs).empty())
            return;
        // ich gebe ab: bei mir noch eine Weile grau zu sehen, dann weg
        std::string me = MyUnit();
        size_t n = std::min(me.size(), unit.size());
        if (!me.empty() && unit.compare(0, n, me, 0, n) != 0 && !UnitIsMine(unit)) m_handedAt[cs] = m_tick;
        else m_handedAt.erase(cs);
        m_resp[cs] = unit;
    }
    SendScratch(cs, ".SB_RS_" + unit);
    // offenes Angebot ist damit erledigt (angenommen oder ueberholt)
    if (m_offer.erase(cs)) { m_offerByMe.erase(cs); m_offerAt.erase(cs); m_offerReminded.erase(cs); SendScratch(cs, ".SB_HO_NONE"); }
    PushBoard();
}

void StripBayPlugin::SendXfer(const std::string& cs, const std::string& v)
{
    if (v == "TWR" || v == "GND") m_xfer[cs] = v;
    else m_xfer.erase(cs);
    SendScratch(cs, ".SB_XF_" + v);
}

void StripBayPlugin::SendArrPos(const std::string& cs, const std::string& v)
{
    m_arrPos.erase(cs);   // eigene Aenderung ist schon im Eintrag
    SendScratch(cs, ".SB_AP_" + v);
}

// Freigabe auf dem Streifen in der Runway-Bay antippen:
//  Abflug:  Line-up <-> Startfreigabe (Ground State LINEUP <-> DEPA, fuer alle sichtbar)
//  Ankunft: Landefreigabe an/aus (CTL-Flag, verteilt wie vSID)
void StripBayPlugin::ToggleRunwayClearance(const std::string& cs)
{
    auto it = m_entries.find(cs);
    if (it == m_entries.end() || it->second.freeStrip) return;
    RespOnExit resp{ this, cs, MyUnit() };
    resp.on = true;
    Entry& e = it->second;
    if (e.arrival)
    {
        auto c = m_ctl.find(cs);
        bool ctl = (c != m_ctl.end() && c->second) && !e.released;
        // Liegt der Streifen auf der Runway, bleibt er dort, auch wenn die Freigabe weg ist
        if (e.bay == BAY_RUNWAY) e.onRunway = true;
        e.released = false;
        SetCtl(cs, !ctl);
        CFlightPlan fresh = FlightPlanSelect(cs.c_str());
        if (fresh.IsValid() && m_entries.count(cs)) UpdateEntry(fresh, true);
        PushBoard();
        return;
    }
    GroundCmd want = (e.data.state == GC_DEPA) ? GC_LINEUP : GC_DEPA;
    ChangeState(cs, want, BAY_RUNWAY, -1);
}

// ---------------------------------------------------------------------------
// CPDLC und DCL ueber Hoppie
// ---------------------------------------------------------------------------
std::string StripBayPlugin::DlStation() const
{
    if (!m_dlStation.empty()) return m_dlStation;
    if (!m_profileIcao.empty()) return m_profileIcao;
    return WxAirport();
}

// Rang einer Station nach dem Callsign: DEL 0, Apron 1, Ground 2, Tower 3, sonst 9
static int StationRank(const std::string& csUpper, double freq)
{
    if (csUpper.find("_DEL") != std::string::npos) return 0;
    if (csUpper.find("_APN") != std::string::npos || csUpper.find("_A_GND") != std::string::npos ||
        csUpper.find("_E_GND") != std::string::npos) return 1;
    if (csUpper.compare(0, 5, "EDDB_") == 0 && (std::fabs(freq - 129.605) < 0.003 || std::fabs(freq - 121.855) < 0.003)) return 1;
    if (csUpper.find("_GND") != std::string::npos) return 2;
    if (csUpper.find("_TWR") != std::string::npos) return 3;
    return 9;
}

// CPDLC (Hoppie): nur die unterste Station am Platz verbindet (DEL vor Apron vor Ground vor Tower,
// bei gleichem Rang der alphabetisch erste Callsign). Liefert die Station, die statt mir verbindet.
std::string StripBayPlugin::DlLowerStation()
{
    CController me = ControllerMyself();
    if (!me.IsValid()) return std::string();
    std::string myCs = Upper(S(me.GetCallsign()));
    int myRank = StationRank(myCs, me.GetPrimaryFrequency());
    std::set<std::string> apts = m_airports;
    if (!m_profileIcao.empty()) apts.insert(m_profileIcao);
    std::string best;
    int bestRank = myRank;
    for (CController c = ControllerSelectFirst(); c.IsValid(); c = ControllerSelectNext(c))
    {
        std::string cf, cw;
        if (!ControllerFreq(c, cf, cw)) continue;
        std::string u = Upper(cw);
        bool here = false;
        for (const std::string& a : apts) if (u.size() > a.size() && u.compare(0, a.size() + 1, a + "_") == 0) here = true;
        if (!here) continue;
        int rk = StationRank(u, atof(cf.c_str()));
        if (rk > 3) continue;
        bool take = rk < bestRank || (rk == bestRank && (best.empty() ? u < myCs : u < best));
        if (take)
        {
            bestRank = rk;
            best = u;
        }
    }
    return best;
}

void StripBayPlugin::UpdateDatalink()
{
    if (!m_dlEnabled || m_dlLogon.empty() || DlStation().empty())
    {
        m_hoppie.Stop();
        return;
    }
    // nur die unterste Station verbindet, alle anderen nicht
    std::string lower = DlLowerStation();
    if (lower != m_dlBlockedBy)
    {
        m_dlBlockedBy = lower;
        if (!lower.empty()) MsgAuto("CPDLC: not connecting - " + lower + " is the lower station and handles CPDLC.");
        else MsgAuto("CPDLC: you are the lowest station, connecting.");
    }
    if (!lower.empty())
    {
        m_hoppie.Stop();
        return;
    }
    m_hoppie.Configure(m_dlLogon, DlStation());
    m_hoppie.Start();
}

void StripBayPlugin::DlLog(const std::string& cs, bool up, const std::string& text, int min, int mrn, const std::string& ra)
{
    DlState& d = m_dl[cs];
    d.log.push_back({ (long long)std::time(nullptr), up, text, min, mrn, ra, std::string() });
    if (d.log.size() > 40) d.log.erase(d.log.begin());
}

int StripBayPlugin::DlSend(const std::string& cs, const std::string& ra, const std::string& text, int mrn, bool telex)
{
    if (!m_hoppie.IsRunning())
    {
        Msg("CPDLC is not connected (.strips cpdlc).");
        return -1;
    }
    if (telex)
    {
        m_hoppie.Send(cs, "telex", text);
        DlLog(cs, true, text, -1, -1, std::string());
        m_hoppie.PollSoon();
        return -1;
    }
    int min = ++m_dlMin;
    if (m_dlMin > 9999) m_dlMin = 0;
    std::string packet = "/data2/" + std::to_string(min) + "/" + (mrn >= 0 ? std::to_string(mrn) : std::string()) + "/" +
                         ra + "/" + text;
    m_hoppie.Send(cs, "cpdlc", packet);
    DlLog(cs, true, text, min, mrn, ra);
    m_hoppie.PollSoon();
    return min;
}

// "BERLIN GROUND" usw. fuer CONTACT / MONITOR
std::string StripBayPlugin::DlUnitName(const std::string& freq, const std::string& who) const
{
    if (freq == "120.340" || freq == "120.630") return "BREMEN RADAR";
    if (freq == "129.505" || freq == "121.705") return "BERLIN GROUND";
    if (freq == "129.605" || freq == "121.855") return "BERLIN APRON";
    if (freq == "122.800") return "ADVISORY";
    std::string w = Upper(who);
    if (w.size() > 5 && w.find('_') == 4)
    {
        std::string apt = w.substr(0, 4);
        std::string city = apt == "EDWW" || apt == "EDBB" ? "BREMEN" : apt;
        if (const AirportProfile* p = FindProfile(apt)) city = p->city;
        auto ends = [&](const char* x) { size_t n = strlen(x); return w.size() >= n && w.compare(w.size() - n, n, x) == 0; };
        if (ends("_DEL")) return city + " DELIVERY";
        if (ends("_A_GND") || ends("_E_GND") || ends("_APN")) return city + " APRON";
        if (ends("_GND")) return city + " GROUND";
        if (ends("_TWR")) return city + " TOWER";
        if (ends("_APP") || ends("_DEP") || ends("_CTR")) return city + " RADAR";
        return w;
    }
    return "ATC";
}

std::string StripBayPlugin::DlDclText(const StripData& s, std::string* error)
{
    std::string err;
    if (s.arrival) err = "not a departure";
    std::string rwy = Trim(s.rwy), sid = Trim(s.sid), sq = Trim(s.squawkAssigned), ades = Trim(s.ades);
    if (ades.empty()) err += (err.empty() ? "" : ", ") + std::string("destination missing");
    if (rwy.empty()) err += (err.empty() ? "" : ", ") + std::string("runway missing");
    if (sid.empty()) err += (err.empty() ? "" : ", ") + std::string("SID missing");
    if (sq.empty() || sq == "0000" || sq == "2000" || sq == "2200" || sq == "1200")
        err += (err.empty() ? "" : ", ") + std::string("squawk missing");
    if (s.clearedAlt <= 2) err += (err.empty() ? "" : ", ") + std::string("CFL missing");
    if (error) *error = err;
    if (!err.empty()) return std::string();

    char alt[16];
    if (s.clearedAlt > m_transitionAlt) snprintf(alt, sizeof(alt), "FL%03d", s.clearedAlt / 100);
    else snprintf(alt, sizeof(alt), "%dFT", s.clearedAlt);
    // Frequenz dessen, der die PDC beantwortet (eigene Primaerfrequenz); ohne sie die naechste
    std::string freq;
    {
        CController me = ControllerMyself();
        char b[16] = {};
        if (me.IsValid() && me.GetPrimaryFrequency() > 100.0 && me.GetPrimaryFrequency() < 199.0)
            snprintf(b, sizeof(b), "%.3f", me.GetPrimaryFrequency());
        freq = b;
    }
    if (freq.empty()) freq = s.nextFreq;
    std::string atis;
    if (m_wxEnabled)
        for (char c : m_wx.GetAtis(Upper(Trim(s.adep))))
            if (isalpha((unsigned char)c)) { atis = std::string(1, (char)toupper((unsigned char)c)); break; }

    // PDC wie an den Plaetzen der FIR Bremen (TopSky-Format ueber Hoppie):
    // CLD 1215 261101 EDDB PDC 001 @EWG4AB@ CLRD TO @EGLL@ OFF @24R@ VIA @KLF2X@ CLIMB @5000FT@
    // SQUAWK @1000@ ADT @1230@ NEXT FREQ @120.340@ ATIS @C@
    time_t now = time(nullptr);
    struct tm u = {};
#ifdef _WIN32
    gmtime_s(&u, &now);
#else
    gmtime_r(&now, &u);
#endif
    char head[64];
    snprintf(head, sizeof(head), "CLD %02d%02d %02d%02d%02d %s PDC %03d ", u.tm_hour, u.tm_min, u.tm_year % 100,
             u.tm_mon + 1, u.tm_mday, Upper(Trim(s.adep)).c_str(), m_pdcSeq % 1000);
    std::string t = std::string(head) + "@" + Upper(s.callsign) + "@ CLRD TO @" + Upper(ades) + "@ OFF @" + Upper(rwy) +
                    "@ VIA @" + Upper(sid) + "@ CLIMB @" + alt + "@ SQUAWK @" + sq + "@";
    if (!s.ctot.empty()) t += " CTOT @" + s.ctot + "@";
    else if (!s.tsat.empty()) t += " ADT @" + s.tsat + "@";
    if (!freq.empty()) t += " NEXT FREQ @" + freq + "@";
    if (!atis.empty()) t += " ATIS @" + atis + "@";
    return t;
}

// Toene: Tone.h (gemeinsam mit StripBay Remote)
// Ton je Ereignis: request (CPDLC-Anfrage/Nachricht), wilco, unable, offer (Uebergabe-Erinnerung)
void StripBayPlugin::PlayEventSound(const std::string& ev)
{
    if (ev != "offer" && !m_dlSound) return;   // CPDLC-Toene haengen am CPDLC-Tonschalter
    auto it = m_sounds.find(ev);
    std::string tone = it == m_sounds.end() ? std::string("triple") : it->second;
    PlayTone(tone);
    m_hub.Sound(tone);   // auch auf dem Remote-Geraet
}

// ---------------------------------------------------------------------------
// StripBay Remote: Server fuer ein weiteres Windows-Geraet im selben Netz
// ---------------------------------------------------------------------------
void StripBayPlugin::RemoteSetup()
{
    if (const char* v = GetDataFromSettings("RemotePort")) if (*v) m_remotePort = std::max(1024, std::min(65000, atoi(v)));
    if (const char* v = GetDataFromSettings("RemoteCode")) m_remoteCode = Trim(S(v));
    if (m_remoteCode.size() != 6)
    {
        unsigned seed = (unsigned)GetTickCount() ^ (unsigned)time(nullptr) ^ (unsigned)(uintptr_t)this;
        seed = seed * 1103515245u + 12345u;
        char b[8];
        snprintf(b, sizeof(b), "%06u", (seed >> 4) % 1000000u);
        m_remoteCode = b;
        SaveDataToSettings("RemoteCode", "StripBay Remote Kopplungscode", m_remoteCode.c_str());
    }
    m_hub.host = this;
    m_hub.code = m_remoteCode;
    m_hub.version = STRIPBAY_VERSION;
    m_hub.name = RemoteServerNet::ComputerName();
    m_hub.send = [this](int c, const std::string& f) { m_net.Send(c, f); };
    m_hub.drop = [this](int c) { m_net.Drop(c); };
    m_hub.log = [this](const std::string& t) { Msg(t); };
    m_hub.changed = [this]() { PushBoard(); };
    m_hub.setViewFilter = [this](int v, int f) {
        // Ansicht / Bereich vom Geraet: gilt fuer die Sitzung (auch das Fenster hier)
        if (v >= 0 && v < VIEW_COUNT && v != m_window.GetView()) m_window.SetView(v);
        if (f != m_window.GetRwyFilter()) m_window.SetRwyFilter(f);
        WindowStateChanged();
        PushBoard();
    };
    if (const char* v = GetDataFromSettings("Remote")) if (*v) m_remoteOn = atoi(v) != 0;
    if (m_remoteOn) RemoteStart();
}

bool StripBayPlugin::RemoteStart()
{
    std::string err;
    m_hub.code = m_remoteCode;
    if (!m_net.Start(m_remotePort, &m_hub, err))
    {
        Msg("Remote: could not start (" + err + "). Port: .strips remote port NUMBER");
        PushBoard();
        return false;
    }
    std::vector<std::string> a = RemoteServerNet::LocalAddresses();
    m_remoteAddr.clear();
    for (size_t i = 0; i < a.size() && i < 2; ++i) m_remoteAddr += (m_remoteAddr.empty() ? "" : ", ") + a[i];
    PushBoard();
    return true;
}

void StripBayPlugin::RemoteStop()
{
    m_hub.DropAll();
    m_net.Stop();
    PushBoard();
}

void StripBayPlugin::RemoteStatus()
{
    if (!m_net.Running())
    {
        Msg("Remote: off. Start: .strips remote on (then open StripBay Remote on the other PC).");
        return;
    }
    std::string who;
    for (const std::string& n : m_hub.Names()) who += (who.empty() ? "" : ", ") + n;
    Msg("Remote: on, port " + std::to_string(m_remotePort) + ", this PC " + (m_remoteAddr.empty() ? std::string("?") : m_remoteAddr) +
        ", code " + m_remoteCode + (who.empty() ? std::string(", no device connected.") : ", connected: " + who + "."));
    if (who.empty())
        Msg("Remote: device cannot connect? Run REMOTE-FIREWALL.bat (next to INSTALL.bat) once on this PC - it opens the Windows firewall for StripBay Remote.");
}

void StripBayPlugin::DlProcess()
{
    // Verbindung im ES-Chat melden (verbunden, Fehler von Hoppie wie "illegal logon code")
    {
        std::string st = m_hoppie.Status();
        if (st != m_dlLastStatus)
        {
            m_dlLastStatus = st;
            MsgAuto("CPDLC: " + st);
        }
    }
    for (const HoppieSent& r : m_hoppie.TakeSent())
        if (!r.ok) MsgAuto("CPDLC: message to " + r.to + " not sent: " + r.error);

    bool changed = false, beep = false;
    std::string sound;   // Ereignis-Ton: unable vor request vor wilco
    for (const HoppieMsg& m : m_hoppie.TakeIncoming())
    {
        if (m.from.empty()) continue;
        const std::string cs = m.from;
        if (m.type == "cpdlc")
        {
            changed = true;
            int min = -1, mrn = -1;
            std::string ra, text;
            if (!ParseCpdlc(m.packet, min, mrn, ra, text)) text = m.packet;
            std::string t = Upper(CpdlcDisplay(text));
            DlState& d = m_dl[cs];
            // Antwort auf eine eigene Nachricht?
            // (Index statt Zeiger: DlLog haengt an und kann den Verlauf umkopieren)
            int mineIdx = -1;
            if (mrn >= 0)
                for (size_t k = 0; k < d.log.size(); ++k)
                    if (d.log[k].up && d.log[k].min == mrn) mineIdx = (int)k;
            size_t before = d.log.size();
            DlLog(cs, false, text, min, mrn, ra);
            if (mineIdx >= 0 && d.log.size() == before) --mineIdx;   // aeltester Eintrag ist rausgefallen
            DlMsg* mine = mineIdx >= 0 ? &d.log[mineIdx] : nullptr;
            std::string first = t.substr(0, t.find(' '));
            if (mine)
            {
                mine->status = first;
                bool yes = first == "WILCO" || first == "ROGER" || first == "ACCEPT" || first == "ACCEPTED" || first == "AFFIRM";
                bool no = first == "UNABLE" || first == "REJECT" || first == "NEGATIVE";
                if (mrn == d.dclMin && yes)
                {
                    d.dcl = 3;
                    CFlightPlan fp = FlightPlanSelect(cs.c_str());
                    if (fp.IsValid() && !fp.GetClearenceFlag()) { m_clrViaDl = true; ToggleClearance(cs); m_clrViaDl = false; }
                    MsgAuto(cs + ": DCL confirmed (" + first + "), clearance flag set.");
                    if (sound.empty()) sound = "wilco";
                }
                else if (mrn == d.dclMin && no)
                {
                    d.dcl = 4;
                    ++d.unread;
                    beep = true;
                    sound = "unable";
                    MsgAuto(cs + ": DCL rejected (" + first + ").");
                }
                else if (no) { ++d.unread; beep = true; sound = "unable"; }
                continue;
            }
            if (t.find("LOGON") != std::string::npos && t.find("REQUEST") != std::string::npos)
            {
                // Nur PDC: CPDLC-Logons werden abgelehnt (Clearance per PDC-Telex, sonst Sprechfunk)
                if (m_pdcOnly)
                {
                    DlSend(cs, "NE", "UNABLE @PDC ONLY@", min);
                    d.logon = 0;
                    continue;
                }
                if (m_dlAutoLogon)
                {
                    DlSend(cs, "NE", "LOGON ACCEPTED", min);
                    d.logon = 2;
                }
                else
                {
                    d.logon = 1;
                    d.logonMin = min;
                    ++d.unread;
                    beep = true;
                }
                continue;
            }
            if (t.compare(0, 6, "LOGOFF") == 0)
            {
                d.logon = 0;
                continue;
            }
            if (IsDclRequest(t))
            {
                d.dcl = 1;
                d.dclRequest = CpdlcDisplay(text);
                ++d.unread;
                beep = true;
                continue;
            }
            ++d.unread;
            beep = true;
            if (ra != "N" && ra != "NE")
            {
                d.pendingMin = min;
                d.pendingText = t;
            }
        }
        else if (m.type == "telex")
        {
            changed = true;
            DlState& d = m_dl[cs];
            DlLog(cs, false, m.packet, -1, -1, std::string());
            std::string t = Upper(m.packet);
            if (IsDclRequest(t))
            {
                d.dcl = 1;
                d.dclRequest = CpdlcDisplay(m.packet);
            }
            ++d.unread;
            beep = true;
        }
    }
    if (beep && sound.empty()) sound = "request";
    if (!sound.empty()) PlayEventSound(sound);
    if (changed) PushBoard();
}

void StripBayPlugin::Datalink(const std::string& cs, int action, const std::string& text)
{
    if (cs.empty()) return;
    DlState& d = m_dl[cs];
    auto it = m_entries.find(cs);
    switch (action)
    {
    case DL_READ:
        d.unread = 0;
        break;
    case DL_ACCEPT_LOGON:
        DlSend(cs, "NE", "LOGON ACCEPTED", d.logonMin);
        d.logon = 2;
        break;
    case DL_REJECT_LOGON:
        DlSend(cs, "NE", "UNABLE", d.logonMin);
        d.logon = 0;
        break;
    case DL_SEND_DCL:
    {
        if (it == m_entries.end()) return;
        std::string err, t = DlDclText(it->second.data, &err);
        if (!err.empty())
        {
            Msg(cs + ": DCL not sent, " + err + ".");
            return;
        }
        int min = DlSend(cs, "WU", t, -1);
        if (min < 0) return;
        ++m_pdcSeq;
        d.dclMin = min;
        d.dcl = 2;
        break;
    }
    case DL_DCL_VOICE:
        DlSend(cs, "NE", "UNABLE @CALL ON VOICE@", -1);
        d.dcl = 0;
        break;
    case DL_CONTACT:
    case DL_MONITOR:
    {
        if (it == m_entries.end() || it->second.data.nextFreq.empty()) return;
        const StripData& sd = it->second.data;
        DlSend(cs, "WU", std::string(action == DL_CONTACT ? "CONTACT @" : "MONITOR @") +
                             DlUnitName(sd.nextFreq, sd.nextCtrl) + "@ @" + sd.nextFreq + "@", -1);
        break;
    }
    case DL_FREE:
    {
        std::string t = Upper(Trim(text));
        if (t.empty()) return;
        // ohne CPDLC-Logon als Telex (kommt im ACARS-Menue an)
        DlSend(cs, "R", t, -1, d.logon != 2);
        break;
    }
    case DL_STANDBY:
    case DL_UNABLE:
    case DL_ROGER:
    case DL_AFFIRM:
    case DL_NEGATIVE:
    {
        static const char* words[] = { "STANDBY", "UNABLE", "ROGER", "AFFIRM", "NEGATIVE" };
        DlSend(cs, "NE", words[action - DL_STANDBY], d.pendingMin);
        if (action != DL_STANDBY)
        {
            d.pendingMin = -1;
            d.pendingText.clear();
        }
        break;
    }
    case DL_LOGOFF:
        DlSend(cs, "NE", "LOGOFF", -1);
        d.logon = 0;
        break;
    default:
        return;
    }
    PushBoard();
}

// Befehl aus dem Fenster (Einstellungen) wie aus der Kommandozeile ausfuehren
void StripBayPlugin::Command(const std::string& line)
{
    OnCompileCommand(line.c_str());
    PushBoard();
}

// Notiz-Streifen in eine Pistenspalte gelegt: er gehoert dann zu dieser Piste
void StripBayPlugin::SetStripGroup(const std::string& cs, int group)
{
    auto it = m_entries.find(cs);
    if (it == m_entries.end() || !it->second.freeStrip) return;
    it->second.data.rwyGroup = (group == RWY_A || group == RWY_B) ? group : RWY_NONE;
    PushBoard();
}

// Durchstarten: Landefreigabe zuruecknehmen, Streifen zurueck nach Final, Marke G/A fuer alle
// (".SB_GA_TRUE"). Nochmal antippen nimmt die Marke zurueck.
void StripBayPlugin::GoAround(const std::string& cs)
{
    auto it = m_entries.find(cs);
    if (it == m_entries.end() || it->second.freeStrip || !it->second.arrival) return;
    Entry& e = it->second;
    auto g = m_ga.find(cs);
    if (g != m_ga.end() && g->second)
    {
        m_ga[cs] = false;
        SendScratch(cs, ".SB_GA_FALSE");
    }
    else
    {
        auto c = m_ctl.find(cs);
        if (c != m_ctl.end() && c->second) SetCtl(cs, false);
        m_ga[cs] = true;
        SendScratch(cs, ".SB_GA_TRUE");
        e.onRunway = false;
        e.released = false;
        if (e.bay != BAY_FINAL)
        {
            MoveOrder(cs, e.bay, BAY_FINAL, 0);
            e.bay = BAY_FINAL;
        }
        Msg(cs + ": go-around (G/A), landing clearance revoked.");
    }
    CFlightPlan fresh = FlightPlanSelect(cs.c_str());
    if (fresh.IsValid() && m_entries.count(cs)) UpdateEntry(fresh, true);
    PushBoard();
}

// Intersection setzen, im Format von vSID an alle verteilt (auch an dein vSID)
void StripBayPlugin::SetIntersection(const std::string& cs, const std::string& text, bool assigned)
{
    std::string name;
    for (char c : text)
        if (isalnum((unsigned char)c) && name.size() < 3) name += (char)toupper((unsigned char)c);
    m_int[cs] = { name, assigned && !name.empty() };
    SendScratch(cs, name.empty() ? std::string(".VSID_INT_NONE_FALSE")
                                 : ".VSID_INT_" + name + (assigned ? "_TRUE" : "_FALSE"));
    CFlightPlan fresh = FlightPlanSelect(cs.c_str());
    auto it = m_entries.find(cs);
    if (fresh.IsValid() && it != m_entries.end()) UpdateEntry(fresh, it->second.arrival);
    PushBoard();
}

// Weggeschickt: Handover-Flag von vSID setzen bzw. zuruecknehmen. Ist der
// Abflug in der Luft, verschwindet sein Streifen danach.
void StripBayPlugin::ToggleHandover(const std::string& cs)
{
    auto it = m_entries.find(cs);
    if (it == m_entries.end() || it->second.arrival || it->second.freeStrip) return;
    bool now = !it->second.data.handedOver;
    m_hov[cs] = now;
    SendScratch(cs, now ? ".VSID_HOV_TRUE" : ".VSID_HOV_FALSE");
    CFlightPlan fresh = FlightPlanSelect(cs.c_str());
    if (fresh.IsValid() && m_entries.count(cs)) UpdateEntry(fresh, false);
    PushBoard();
    if (now && !m_entries.count(cs))
        Msg(cs + " handed over. Undo: .strips handover undo " + cs);
}

// Hold Short setzen oder loeschen. Andere StripBay-Nutzer bekommen ihn ueber
// ein kurzes Scratchpad ".SB_HS_..." (danach steht der alte Inhalt wieder da).
void StripBayPlugin::SetHoldShort(const std::string& cs, const std::string& text)
{
    std::string v;
    for (char c : text)
        if (isalnum((unsigned char)c) && v.size() < 6) v += (char)toupper((unsigned char)c);
    if (v.empty()) m_hs.erase(cs);
    else m_hs[cs] = v;
    SendScratch(cs, ".SB_HS_" + (v.empty() ? std::string("NONE") : v));
    CFlightPlan fresh = FlightPlanSelect(cs.c_str());
    auto it = m_entries.find(cs);
    if (fresh.IsValid() && it != m_entries.end()) UpdateEntry(fresh, it->second.arrival);
    PushBoard();
}

// ---------------------------------------------------------------------------
// Detailmenue: Auswahllisten und Werte setzen
// ---------------------------------------------------------------------------
// SIDs, STARs (fuer Platz und Piste) bzw. Pisten eines Platzes aus der Sektordatei
std::vector<std::string> StripBayPlugin::SectorList(int type, const std::string& apt, const std::string& rwy)
{
    std::set<std::string> out;
    std::string r = NormRwy(rwy);
    for (CSectorElement el = SectorFileElementSelectFirst(type); el.IsValid(); el = SectorFileElementSelectNext(el, type))
    {
        std::vector<std::string> ap = Split(S(el.GetAirportName()), " \t");
        if (ap.empty() || Upper(ap[0]) != apt) continue;
        if (type == SECTOR_ELEMENT_RUNWAY)
        {
            for (int i = 0; i < 2; ++i)
            {
                std::string n = NormRwy(S(el.GetRunwayName(i)));
                if (!n.empty()) out.insert(n);
            }
        }
        else
        {
            if (!r.empty() && NormRwy(S(el.GetRunwayName(0))) != r) continue;
            std::string n = Upper(Trim(S(el.GetName())));
            if (!n.empty()) out.insert(n);
        }
    }
    return std::vector<std::string>(out.begin(), out.end());
}

// Pisten zur Auswahl: nur die festen Pisten des Platzes. Beim geteilten Platz (EDDB) die
// Pisten der beiden Gruppen, die es in der Sektordatei gibt (06/24, nicht die alten 07/25);
// ohne Sektordaten die aktuellen Bezeichner. Aktive Betriebsrichtung zuerst.
std::vector<std::string> StripBayPlugin::RunwayOptions(const std::string& apt)
{
    std::vector<std::string> sector = SectorList(SECTOR_ELEMENT_RUNWAY, apt, "");
    std::vector<std::string> o;
    if (!m_split.airport.empty() && apt == m_split.airport)
    {
        std::set<std::string> all;
        for (int g = RWY_A; g <= RWY_B; ++g) all.insert(m_split.rwys[g].begin(), m_split.rwys[g].end());
        for (const std::string& r : sector) if (all.count(r)) o.push_back(r);
        // Aeltere Sektorpakete fuehren dieselbe Bahn doppelt (07/25 und 06/24):
        // gibt es die neuen Bezeichner, fallen die alten weg
        if (apt == "EDDB")
        {
            bool modern = std::any_of(o.begin(), o.end(), [](const std::string& r) { return r.compare(0, 2, "06") == 0 || r.compare(0, 2, "24") == 0; });
            if (modern)
                o.erase(std::remove_if(o.begin(), o.end(), [](const std::string& r) { return r.compare(0, 2, "07") == 0 || r.compare(0, 2, "25") == 0; }), o.end());
        }
        if (o.empty())
        {
            if (apt == m_profileIcao && !m_fixedRwys.empty()) o = m_fixedRwys;
            else o.assign(all.begin(), all.end());
        }
    }
    else
    {
        o = sector;
        if (o.empty() && apt == m_profileIcao) o = m_fixedRwys;
    }
    // Hannover: Hubschrauber auf Helipad 2 und 3
    if (apt == "EDDV")
        for (const char* h : { "H2", "H3" })
            if (std::find(o.begin(), o.end(), h) == o.end()) o.push_back(h);
    // aktive Pisten (Abflug oder Landung) nach vorn
    std::set<std::string> act;
    for (int i = 0; i < 2; ++i)
    {
        auto a = m_actRwy[i].find(apt);
        if (a != m_actRwy[i].end()) act.insert(a->second.begin(), a->second.end());
    }
    std::stable_partition(o.begin(), o.end(), [&](const std::string& r) { return act.count(r) > 0; });
    return o;
}

// Abstand zweier Punkte in Metern (fuer "Stand besetzt")
static double DistM(double la1, double lo1, double la2, double lo2)
{
    const double r = 6371000.0, k = 3.14159265358979 / 180.0;
    double dla = (la2 - la1) * k, dlo = (lo2 - lo1) * k;
    double a = sin(dla / 2) * sin(dla / 2) + cos(la1 * k) * cos(la2 * k) * sin(dlo / 2) * sin(dlo / 2);
    return 2 * r * atan2(sqrt(a), sqrt(1 - a));
}
static bool InPoly(const std::vector<std::pair<double, double>>& poly, double la, double lo)
{
    bool in = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
        if (((poly[i].first > la) != (poly[j].first > la)) &&
            (lo < (poly[j].second - poly[i].second) * (la - poly[i].first) / (poly[j].first - poly[i].first) + poly[i].second))
            in = !in;
    return in;
}

// Stand-Lage wie im Ground Radar Plugin: besetzt (Flieger steht drauf), vergeben (an einen
// anderen Flieger), gesperrt (BLOCKS eines besetzten oder vergebenen Nachbarn)
// Wer steht wo (Position im Standkreis/-polygon, langsam) und welche Ankunft hat welchen Stand
void StripBayPlugin::StandOccupancy(const std::string& apt, std::map<std::string, std::vector<std::string>>& occ,
                                    std::map<std::string, std::vector<std::string>>& asg)
{
    occ.clear();
    asg.clear();
    auto inf = m_icao.standInfo.find(apt);
    if (!m_icao.stands.count(apt)) return;
    for (CFlightPlan fp = FlightPlanSelectFirst(); fp.IsValid(); fp = FlightPlanSelectNext(fp))
    {
        std::string c2 = S(fp.GetCallsign());
        if (c2.empty()) continue;
        CRadarTarget rt = fp.GetCorrelatedRadarTarget();
        if (rt.IsValid() && inf != m_icao.standInfo.end() && rt.GetPosition().GetReportedGS() < 10)
        {
            CPosition pos = rt.GetPosition().GetPosition();
            for (const auto& kv : inf->second)
            {
                const StandInfo& si = kv.second;
                if (si.area) continue;
                bool in = (si.hasPos && DistM(si.lat, si.lon, pos.m_Latitude, pos.m_Longitude) <= si.radius) ||
                          (si.poly.size() >= 3 && InPoly(si.poly, pos.m_Latitude, pos.m_Longitude));
                if (in) occ[kv.first].push_back(c2);
            }
        }
        if (Upper(Trim(S(fp.GetFlightPlanData().GetDestination()))) != apt) continue;
        if (ParseGroundState(S(fp.GetGroundState())) == GC_PARK) continue;
        std::string stand;
        auto live = m_standLive.find(c2);
        if (live != m_standLive.end()) stand = Upper(live->second);
        if (stand.empty())
        {
            CFlightPlanControllerAssignedData ca = fp.GetControllerAssignedData();
            for (int i : { 6, 0, 1, 2, 3, 4, 5, 7, 8 })
            {
                stand = ParseGrpStand(S(ca.GetFlightStripAnnotation(i)));
                if (!stand.empty()) break;
            }
            if (stand.empty()) stand = ParseSlashStand(S(ca.GetFlightStripAnnotation(6)));
        }
        if (stand.empty()) continue;
        bool area = inf != m_icao.standInfo.end() && inf->second.count(stand) && inf->second.at(stand).area;
        if (!area) asg[stand].push_back(c2);
    }
}

std::map<std::string, int> StripBayPlugin::StandStatus(const std::string& cs)
{
    std::map<std::string, int> st;
    auto it = m_entries.find(cs);
    std::string apt;
    if (it != m_entries.end()) apt = Upper(Trim(it->second.arrival ? it->second.data.ades : it->second.data.adep));
    if (apt.empty()) apt = m_profileIcao;
    std::map<std::string, std::vector<std::string>> occ, asg;
    StandOccupancy(apt, occ, asg);
    auto other = [&](const std::vector<std::string>& v) { for (const std::string& c : v) if (c != cs) return true; return false; };
    for (const auto& kv : occ) if (other(kv.second)) st[kv.first] = 2;
    for (const auto& kv : asg) if (other(kv.second) && !st.count(kv.first)) st[kv.first] = 1;
    // auch Staende anderer Streifen (Abfluege, gerade vergebene) am selben Platz zaehlen als vergeben
    for (const auto& kv : m_entries)
    {
        if (kv.first == cs || kv.second.freeStrip || kv.second.data.stand.empty()) continue;
        const StripData& od = kv.second.data;
        if (Upper(Trim(kv.second.arrival ? od.ades : od.adep)) != apt) continue;
        std::string sn = Upper(od.stand);
        if (!st.count(sn)) st[sn] = 1;
    }
    // Nachbarn sperren
    auto inf = m_icao.standInfo.find(apt);
    if (inf != m_icao.standInfo.end())
    {
        std::map<std::string, int> base = st;
        for (const auto& kv : base)
        {
            auto si = inf->second.find(kv.first);
            if (si == inf->second.end()) continue;
            for (const std::string& b : si->second.blocks)
                if (!st.count(b)) st[b] = 3;
        }
    }
    return st;
}

// Betriebsrichtung gewechselt: Abfluege vor dem Rollen, deren Piste nicht mehr aktiv ist ->
// aktive Piste derselben Gruppe (Berlin: Nordbahn bleibt Nordbahn), sonst die einzige aktive
std::vector<std::pair<std::string, std::string>> StripBayPlugin::RunwaySwitchTargets()
{
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& kv : m_entries)
    {
        const Entry& e = kv.second;
        if (e.freeStrip || e.arrival) continue;
        GroundCmd st = e.data.state;
        if (!(st == GC_NSTS || st == GC_ONFREQ || st == GC_STUP || st == GC_PUSH)) continue;
        std::string apt = Upper(Trim(e.data.adep)), r = NormRwy(e.data.rwy);
        if (r.empty()) continue;
        auto a = m_actRwy[0].find(apt);
        if (a == m_actRwy[0].end() || a->second.empty() || a->second.count(r)) continue;
        std::string target;
        int g = RunwayGroup(apt, r, nullptr);
        if (g == RWY_A || g == RWY_B)
        {
            for (const std::string& x : a->second) if (m_split.rwys[g].count(x)) { target = x; break; }
        }
        else if (a->second.size() == 1) target = *a->second.begin();
        if (!target.empty()) out.push_back({ kv.first, target });
    }
    return out;
}

// Freier Stand nach den GRP-Daten: nicht besetzt/vergeben/gesperrt, nicht MANUAL,
// Wirbelschleppenkategorie passt; hoechste Prioritaet zuerst, sonst in Listenreihenfolge
std::string StripBayPlugin::PickFreeStand(const std::string& cs, const std::string& avoid)
{
    auto it = m_entries.find(cs);
    if (it == m_entries.end()) return std::string();
    std::string apt = Upper(Trim(it->second.arrival ? it->second.data.ades : it->second.data.adep));
    auto names = m_icao.stands.find(apt);
    auto inf = m_icao.standInfo.find(apt);
    if (names == m_icao.stands.end() || inf == m_icao.standInfo.end()) return std::string();
    std::map<std::string, int> st = StandStatus(cs);
    std::string wtc = Upper(Trim(it->second.data.wtc));
    std::string airline = Upper(cs.substr(0, 3));
    bool icaoAirline = airline.size() == 3 && isalpha((unsigned char)airline[0]) && isalpha((unsigned char)airline[1]) &&
                       isalpha((unsigned char)airline[2]) && cs.size() > 3 && isdigit((unsigned char)cs[3]);
    std::string best;
    int bestPrio = -1000;
    for (const std::string& n : names->second)
    {
        auto si = inf->second.find(n);
        if (si == inf->second.end() || si->second.manual || si->second.area) continue;
        if (st.count(n) || n == avoid) continue;
        if (!wtc.empty() && !si->second.wtc.empty() && si->second.wtc.find(wtc[0]) == std::string::npos) continue;
        // wie GRP: Stands mit Airline-Liste (CALLSIGN:) nur fuer diese Airlines, die dann bevorzugt
        int prio = si->second.priority;
        if (!si->second.airlines.empty())
        {
            if (!icaoAirline || std::find(si->second.airlines.begin(), si->second.airlines.end(), airline) ==
                                    si->second.airlines.end()) continue;
            prio += 10;
        }
        if (prio > bestPrio) { bestPrio = prio; best = n; }
    }
    return best;
}

// Funktion des Ground Radar Plugins fuer diesen Flieger aufrufen, als haette man sie in dessen
// Stand-Menue gewaehlt (GRP wirkt auf den angewaehlten Flieger). false = GRP nicht da / kein Radarschirm.
bool StripBayPlugin::GrpStandCall(const std::string& cs, int fn, const std::string& item)
{
    if (fn <= 0 || m_screens.empty() || !GetModuleHandleW(L"GRplugin.dll")) return false;
    CFlightPlan fp = FlightPlanSelect(cs.c_str());
    if (!fp.IsValid()) return false;
    auto e = m_entries.find(cs);
    bool arr = e != m_entries.end() && e->second.arrival;
    SetASELAircraft(fp);
    POINT pt = { 0, 0 };
    RECT area = { 0, 0, 0, 0 };
    // Tag-Item des GRP: 1 = Departure stand, 2 = Arrival stand
    m_screens.front()->StartTagFunction(cs.c_str(), "Ground Radar plugin", arr ? 2 : 1, item.c_str(), "Ground Radar plugin",
                                        fn, pt, area);
    return true;
}

void StripBayPlugin::AutoStand(const std::string& cs)
{
    CFlightPlan fp = FlightPlanSelect(cs.c_str());
    if (!fp.IsValid() || !m_entries.count(cs)) return;
    std::string before = StandOf(fp, cs, nullptr);
    // 1. "Auto" des Ground Radar Plugins (wie im GRP-Stand-Menue); GRP waehlt nach seinen Regeln
    // 2. GRP nicht da oder antwortet nicht: StripBay waehlt selbst aus GRpluginStands.txt
    bool grp = !m_screens.empty() && m_grpAutoFn > 0 && GetModuleHandleW(L"GRplugin.dll");
    // das Ground Radar Plugin selbst ("Auto" in dessen Stand-Menue, wirkt auf den angewaehlten
    //    Flieger). GRP verteilt die Zuweisung oft erst im naechsten Takt -> ein paar Sekunden warten.
    if (grp)
    {
        GrpStandCall(cs, m_grpAutoFn, "");
        CFlightPlan f2 = FlightPlanSelect(cs.c_str());
        std::string after = f2.IsValid() ? StandOf(f2, cs, nullptr) : std::string();
        if (!after.empty() && after != before)
        {
            m_standSrc[cs] = "GRP auto";
            UpdateEntry(f2, m_entries[cs].arrival);
            PushBoard();
            Msg(cs + ": stand " + after + " assigned by GRP.");
            return;
        }
        m_autoPending[cs] = { before, m_tick + 3 };
        return;
    }
    AutoStandFallback(cs, before, m_screens.empty() ? "StripBay is not allowed on a radar screen"
                                  : m_grpAutoFn <= 0 ? "GRP call switched off (.strips grpauto 0)"
                                                     : "Ground Radar plugin not loaded");
}

// Squawk automatisch ueber TopSky (Funktion "Assign squawk", wie vSID: 667), sonst CCAMS (871).
// Die Plugins vergeben den Code teils erst nach einer Server-Abfrage -> bis 5 s warten.
void StripBayPlugin::AutoSquawk(const std::string& cs)
{
    CFlightPlan fp = FlightPlanSelect(cs.c_str());
    if (!fp.IsValid() || !m_entries.count(cs)) return;
    if (m_screens.empty())
    {
        Msg(cs + ": squawk auto needs StripBay allowed on a radar screen (Plug-ins dialog, like the clearance flag).");
        return;
    }
    bool ts = GetModuleHandleW(L"TopSky.dll") != nullptr;
    bool cc = GetModuleHandleW(L"CCAMS.dll") != nullptr;
    const char* plugin = (ts || !cc) ? "TopSky plugin" : "CCAMS";
    int fn = (ts || !cc) ? m_sqkFnTs : 871;
    std::string before = S(fp.GetControllerAssignedData().GetSquawk());
    POINT pt = { 0, 0 };
    RECT area = { 0, 0, 0, 0 };
    m_screens.front()->StartTagFunction(cs.c_str(), plugin, TAG_ITEM_TYPE_CALLSIGN, cs.c_str(), plugin, fn, pt, area);
    CFlightPlan f2 = FlightPlanSelect(cs.c_str());
    std::string after = f2.IsValid() ? S(f2.GetControllerAssignedData().GetSquawk()) : std::string();
    if (!after.empty() && after != before)
    {
        Msg(cs + ": squawk " + after + " (" + plugin + ").");
        UpdateEntry(f2, m_entries[cs].arrival);
        PushBoard();
        return;
    }
    m_sqkPending[cs] = { before, m_tick + 5, plugin };
}

void StripBayPlugin::ProcessAutoSquawks()
{
    for (auto it = m_sqkPending.begin(); it != m_sqkPending.end();)
    {
        const std::string cs = it->first;
        CFlightPlan fp = FlightPlanSelect(cs.c_str());
        if (!fp.IsValid() || !m_entries.count(cs)) { it = m_sqkPending.erase(it); continue; }
        std::string now = S(fp.GetControllerAssignedData().GetSquawk());
        if (!now.empty() && now != it->second.before)
        {
            Msg(cs + ": squawk " + now + " (" + it->second.plugin + ").");
            UpdateEntry(fp, m_entries[cs].arrival);
            it = m_sqkPending.erase(it);
            PushBoard();
            continue;
        }
        if (m_tick >= it->second.deadline)
        {
            MsgAuto(cs + ": no squawk assigned - " + it->second.plugin +
                " did not answer (is it loaded? is the aircraft tracked by someone else?). Type the code on the keypad instead.");
            it = m_sqkPending.erase(it);
            continue;
        }
        ++it;
    }
}

// GRP hat nicht (rechtzeitig) zugewiesen: selbst einen freien Stand aus den GRP-Daten waehlen
// und wie GRP verteilen. "Auto" heisst wie im GRP: ein neuer Stand (der bisherige nur, wenn
// sonst keiner frei ist).
void StripBayPlugin::AutoStandFallback(const std::string& cs, const std::string& before, const std::string& why)
{
    if (!m_entries.count(cs)) return;
    std::string pick = PickFreeStand(cs, Upper(before));
    if (pick.empty()) pick = PickFreeStand(cs, std::string());
    if (pick.empty())
    {
        std::string apt = Upper(Trim(m_entries[cs].arrival ? m_entries[cs].data.ades : m_entries[cs].data.adep));
        Msg(cs + ": no stand assigned - " + why + ", and " +
            (m_icao.standInfo.count(apt) ? std::string("no free matching stand in GRpluginStands.txt.")
                                         : "no stand data for " + apt + " (GRpluginStands.txt not found, see .strips icao)."));
        return;
    }
    SetField(cs, F_STAND, pick);
    m_standSrc[cs] = "StripBay auto";
    Msg(cs + ": stand " + pick + " assigned (free in the GRP stand data; " + why + ").");
}

// wartende Auto-Zuweisungen: hat GRP inzwischen einen Stand vergeben?
void StripBayPlugin::ProcessAutoStands()
{
    for (auto it = m_autoPending.begin(); it != m_autoPending.end();)
    {
        const std::string cs = it->first;
        CFlightPlan fp = FlightPlanSelect(cs.c_str());
        if (!fp.IsValid() || !m_entries.count(cs)) { it = m_autoPending.erase(it); continue; }
        std::string now = StandOf(fp, cs, nullptr);
        if (!now.empty() && now != it->second.before)
        {
            m_standSrc[cs] = "GRP auto";
            UpdateEntry(fp, m_entries[cs].arrival);
            Msg(cs + ": stand " + now + " assigned by GRP.");
            it = m_autoPending.erase(it);
            PushBoard();
            continue;
        }
        if (m_tick >= it->second.deadline)
        {
            std::string before = it->second.before;
            it = m_autoPending.erase(it);
            AutoStandFallback(cs, before, "GRP did not assign one");
            continue;
        }
        ++it;
    }
}

std::vector<std::string> StripBayPlugin::FieldOptions(const std::string& cs, int field)
{
    std::vector<std::string> o;
    auto it = m_entries.find(cs);
    if (it == m_entries.end()) return o;
    if (field == F_HISTORY)
    {
        auto h = m_hist.find(cs);
        if (h == m_hist.end() || h->second.empty()) return { "(nothing recorded yet)" };
        return std::vector<std::string>(h->second.rbegin(), h->second.rend());   // neueste zuerst
    }
    const StripData& d = it->second.data;
    bool arr = it->second.arrival;
    std::string apt = Upper(Trim(arr ? d.ades : d.adep));
    if (it->second.freeStrip) apt = m_profileIcao.empty() ? WxAirport() : m_profileIcao;
    switch (field)
    {
    case F_RWY:   o = RunwayOptions(apt); break;
    // SIDs/STARs nur fuer die Piste des Streifens (wie die Abflugliste); ohne Piste alle
    case F_SID:   o = SectorList(SECTOR_ELEMENT_SID, Upper(Trim(d.adep)), d.rwy);
                  if (o.empty() && NormRwy(d.rwy).empty()) o = SectorList(SECTOR_ELEMENT_SID, Upper(Trim(d.adep)), ""); break;
    case F_STAR:  o = SectorList(SECTOR_ELEMENT_STAR, Upper(Trim(d.ades)), d.arrRwy);
                  if (o.empty() && NormRwy(d.arrRwy).empty()) o = SectorList(SECTOR_ELEMENT_STAR, Upper(Trim(d.ades)), ""); break;
    case F_STAND:
    {
        auto st = m_icao.stands.find(apt);
        if (st != m_icao.stands.end()) o = st->second;
        break;
    }
    case F_CFL:
        if (arr) { o.push_back("ILS"); o.push_back("VIS"); }
        // nur volle 1000 ft (bis zur Transition Altitude in Fuss, darueber Flight Level)
        for (int a = 1000; a <= m_transitionAlt; a += 1000) o.push_back(std::to_string(a));
        for (int fl = (m_transitionAlt + 1000) / 1000 * 10; fl <= 400; fl += 10)
        {
            char b[8];
            snprintf(b, sizeof(b), "FL%03d", fl);
            o.push_back(b);
        }
        break;
    case F_RFL:
        for (int fl = 50; fl <= 450; fl += 10)
        {
            char b[8];
            snprintf(b, sizeof(b), "FL%03d", fl);
            o.push_back(b);
        }
        break;
    case F_HS:
    {
        // Schnellauswahl: Pisten des Platzes, die Intersection, dann die Haltepunkte
        std::set<std::string> seen;
        for (const std::string& r : SectorList(SECTOR_ELEMENT_RUNWAY, apt, ""))
            if (seen.insert(r).second) o.push_back(r);
        if (!d.intersection.empty() && seen.insert(d.intersection).second) o.push_back(d.intersection);
        for (const std::string& h : Split(m_hsList, " ,;"))
            if (seen.insert(Upper(h)).second) o.push_back(Upper(h));
        // Rollhalte der Piste (Intersections), bei unbekannter Piste beider Pisten
        if (!d.freeStrip || true)
        {
            int ig = IntersectionGroup(m_profileIcao, d.rwy, d.rwyGroup);
            std::string li = ig == RWY_A ? m_intNorth : ig == RWY_B ? m_intSouth : m_intNorth + " " + m_intSouth;
            for (const std::string& h : Split(li, " ,;"))
                if (seen.insert(Upper(h)).second) o.push_back(Upper(h));
        }
        break;
    }
    case F_RESP:
        o = { "DEL Delivery", "APN Apron", "APNN Apron North", "APNS Apron South", "GND Ground", "GNDN Ground North",
              "GNDS Ground South", "TWR Tower", "TWRN Tower North", "TWRS Tower South", "NONE (nobody)" };
        if (!SplitActive())
            o.erase(std::remove_if(o.begin(), o.end(), [](const std::string& x) { return x.size() > 4 && (x[3] == 'N' || x[3] == 'S') && x[4] == ' '; }), o.end());
        if (!m_hasApron)
            o.erase(std::remove_if(o.begin(), o.end(), [](const std::string& x) { return x.compare(0, 3, "APN") == 0; }), o.end());
        if (!m_splitGnd)
            o.erase(std::remove_if(o.begin(), o.end(), [](const std::string& x) {
                return (x.compare(0, 4, "GNDN") == 0 || x.compare(0, 4, "GNDS") == 0 || x.compare(0, 4, "APNN") == 0 || x.compare(0, 4, "APNS") == 0); }), o.end());
        {
            // die naechste Stelle ganz vorn
            std::string nx = NextRespUnit(cs);
            auto f = std::find_if(o.begin(), o.end(), [&](const std::string& x) { return !nx.empty() && x.compare(0, nx.size() + 1, nx + " ") == 0; });
            if (f != o.end()) { std::string v = *f; o.erase(f); o.insert(o.begin(), v); }
        }
        {
            auto of = m_offer.find(cs);
            if (of != m_offer.end())
            {
                if (UnitIsMine(of->second)) { o.insert(o.begin(), "REJECT handover"); o.insert(o.begin(), "ACCEPT handover"); }
                else o.insert(o.begin(), "CANCEL handover to " + of->second);
            }
            else
            {
                // bei einer anderen Stelle (oder frei): uebernehmen
                std::string rs = EffResp(cs), me = MyUnit();
                if (!me.empty() && !UnitIsMine(rs))
                    o.insert(o.begin(), rs.empty() ? std::string("TAKE over") : "TAKE over from " + rs);
            }
        }
        break;
    case F_RULES: o = { "I", "V" }; break;
    case F_COMM:  o = { "V", "R", "T" }; break;
    case F_STATE:
    {
        // Wert vorn (fuer EuroScope), Klartext dahinter
        if (arr)
        {
            o = { "NSTS No state", "TXIN Taxi in" };
            if (d.atGate) o.push_back("PARK Parked");   // nur am Gate, < 3 kt
        }
        else
        {
            o = { "NSTS Pending", "ONFREQ On freq", "ST-UP Startup", "PUSH Pushback", "TAXI Taxi out" };
            // Line-up / Takeoff nur fuer den Tower (und die Gesamtsicht)
            int v = m_window.GetView();
            if (v == VIEW_TWR || v == VIEW_ALL) { o.push_back("LINEUP Line-up"); o.push_back("DEPA Takeoff"); }
        }
        break;
    }
    case F_FREQ:
    {
        // automatisch, die Berliner Frequenzen und alle Lotsen online
        std::set<std::string> seen;
        o.push_back("AUTO");
        for (const char* f : { "120.340", "120.630", "129.505", "121.705", "129.605", "121.855", "122.800" })
        {
            o.push_back(std::string(f) + " " + FreqName(f));
            seen.insert(f);
        }
        for (CController c = ControllerSelectFirst(); c.IsValid(); c = ControllerSelectNext(c))
        {
            std::string cf, cw;
            if (ControllerFreq(c, cf, cw) && !seen.count(cf)) { o.push_back(cf + " " + cw); seen.insert(cf); }
        }
        break;
    }
    default: break;
    }
    return o;
}

// Erster Routenpunkt "SID/RWY" bzw. "ADEP/RWY" (so setzt auch vSID SID und Piste),
// letzter Punkt "STAR/RWY" bzw. "ADES/RWY"
static bool IsRwyToken(const std::string& t, std::string* head)
{
    size_t sl = t.find('/');
    if (sl == std::string::npos) return false;
    std::string r = t.substr(sl + 1);
    if (r.size() < 2 || r.size() > 3 || !isdigit((unsigned char)r[0]) || !isdigit((unsigned char)r[1])) return false;
    if (head) *head = t.substr(0, sl);
    return true;
}

static std::string WithDepToken(const std::string& route, const std::string& curSid, const std::string& token)
{
    std::vector<std::string> t = Split(route, " ");
    if (!t.empty() && (IsRwyToken(t[0], nullptr) || (!curSid.empty() && Upper(t[0]) == Upper(curSid)))) t.erase(t.begin());
    std::string r = token;
    for (const std::string& x : t) r += " " + x;
    return r;
}

static std::string WithArrToken(const std::string& route, const std::string& curStar, const std::string& token)
{
    std::vector<std::string> t = Split(route, " ");
    if (!t.empty() && (IsRwyToken(t.back(), nullptr) || (!curStar.empty() && Upper(t.back()) == Upper(curStar)))) t.pop_back();
    std::string r;
    for (const std::string& x : t) r += x + " ";
    return r + token;
}

// "5000" -> 5000, "FL070" / "F70" / "70" (ueber TA) -> 7000, "ILS" -> 1, "VIS" -> 2, -1 ungueltig
static int ParseAlt(const std::string& v, int ta)
{
    std::string u = Upper(Trim(v));
    if (u == "ILS") return 1;
    if (u == "VIS" || u == "VISUAL") return 2;
    bool fl = false;
    if (u.compare(0, 2, "FL") == 0) { u = u.substr(2); fl = true; }
    else if (!u.empty() && u[0] == 'F') { u = u.substr(1); fl = true; }
    if (u.empty()) return -1;
    for (char c : u)
        if (!isdigit((unsigned char)c)) return -1;
    int n = atoi(u.c_str());
    if (fl || n < 1000) n *= 100;
    if (n < 0 || n > 60000) return -1;
    (void)ta;
    return n;
}

void StripBayPlugin::SetField(const std::string& cs, int field, const std::string& valueIn)
{
    auto it = m_entries.find(cs);
    if (it == m_entries.end()) return;
    if (field == F_HISTORY) { PushBoard(); return; }   // nur lesen
    if (field == F_HS)
    {
        SetHoldShort(cs, valueIn);
        return;
    }
    if (field == F_TAXI)
    {
        if (!it->second.freeStrip) SetTaxi(cs, valueIn);
        return;
    }
    if (it->second.freeStrip) return;
    CFlightPlan fp = FlightPlanSelect(cs.c_str());
    if (!fp.IsValid()) return;
    bool arr = it->second.arrival;
    std::string v = Trim(valueIn);
    std::string vu = Upper(v);
    CFlightPlanData d = fp.GetFlightPlanData();
    CFlightPlanControllerAssignedData ca = fp.GetControllerAssignedData();
    bool ok = true, amend = false;
    switch (field)
    {
    case F_RWY:
    {
        std::string rwy = NormRwy(vu);
        if (rwy.empty()) { ok = false; break; }
        {
            // nur feste Pisten des Platzes
            std::vector<std::string> allowed = RunwayOptions(Upper(Trim(S(arr ? d.GetDestination() : d.GetOrigin()))));
            if (!allowed.empty() && std::find(allowed.begin(), allowed.end(), rwy) == allowed.end()) { ok = false; break; }
        }
        if (!arr)
        {
            // SID behalten, wenn es sie fuer die neue Piste gibt, sonst waehlt EuroScope
            std::string sid = Upper(Trim(S(d.GetSidName())));
            std::vector<std::string> sids = SectorList(SECTOR_ELEMENT_SID, Upper(Trim(S(d.GetOrigin()))), rwy);
            bool keep = !sid.empty() && std::find(sids.begin(), sids.end(), sid) != sids.end();
            std::string token = (keep ? sid : Upper(Trim(S(d.GetOrigin())))) + "/" + rwy;
            ok = d.SetRoute(WithDepToken(S(d.GetRoute()), S(d.GetSidName()), token).c_str());
        }
        else
        {
            std::string star = Upper(Trim(S(d.GetStarName())));
            std::vector<std::string> stars = SectorList(SECTOR_ELEMENT_STAR, Upper(Trim(S(d.GetDestination()))), rwy);
            bool keep = !star.empty() && std::find(stars.begin(), stars.end(), star) != stars.end();
            std::string token = (keep ? star : Upper(Trim(S(d.GetDestination())))) + "/" + rwy;
            ok = d.SetRoute(WithArrToken(S(d.GetRoute()), S(d.GetStarName()), token).c_str());
        }
        amend = true;
        break;
    }
    case F_SID:
    {
        std::string rwy = NormRwy(S(d.GetDepartureRwy()));
        std::string token = (vu.empty() ? Upper(Trim(S(d.GetOrigin()))) : vu) + (rwy.empty() ? "" : "/" + rwy);
        ok = d.SetRoute(WithDepToken(S(d.GetRoute()), S(d.GetSidName()), token).c_str());
        amend = true;
        break;
    }
    case F_STAR:
    {
        std::string rwy = NormRwy(S(d.GetArrivalRwy()));
        std::string token = (vu.empty() ? Upper(Trim(S(d.GetDestination()))) : vu) + (rwy.empty() ? "" : "/" + rwy);
        ok = d.SetRoute(WithArrToken(S(d.GetRoute()), S(d.GetStarName()), token).c_str());
        amend = true;
        break;
    }
    case F_SQUAWK:
    {
        if (vu == "AUTO") { AutoSquawk(cs); return; }
        ok = vu.size() == 4 && vu.find_first_not_of("01234567") == std::string::npos && ca.SetSquawk(vu.c_str());
        break;
    }
    case F_CFL:
    {
        int a = vu.empty() ? 0 : ParseAlt(vu, m_transitionAlt);
        ok = a >= 0 && ca.SetClearedAltitude(a);
        break;
    }
    case F_RFL:
    {
        int a = ParseAlt(vu, m_transitionAlt);
        ok = a > 2 && ca.SetFinalAltitude(a);
        break;
    }
    case F_RULES: ok = (vu == "I" || vu == "V") && d.SetPlanType(vu.c_str()); amend = true; break;
    case F_ALTN:  ok = d.SetAlternate(vu.c_str()); amend = true; break;
    case F_ADES:  ok = vu.size() == 4 && d.SetDestination(vu.c_str()); amend = true; break;
    case F_ADEP:  ok = vu.size() == 4 && d.SetOrigin(vu.c_str()); amend = true; break;
    case F_EOBT:  ok = vu.size() == 4 && d.SetEstimatedDepartureTime(vu.c_str()); amend = true; break;
    case F_ROUTE: ok = d.SetRoute(vu.c_str()); amend = true; break;
    case F_REMARKS: ok = d.SetRemarks(v.c_str()); amend = true; break;
    case F_TYPE:
    {
        // im Aircraft-Info-String nur den Typ ersetzen ("B738/M-SDE3FGHIRWY/LB1")
        std::string info = S(d.GetAircraftInfo());
        std::string cur = Upper(Trim(S(d.GetAircraftFPType())));
        std::vector<std::string> parts = SplitKeep(info, '/');
        bool done = false;
        for (std::string& p : parts)
        {
            std::string head = p.substr(0, p.find('-'));
            if (!done && !cur.empty() && Upper(head) == cur) { p = vu + p.substr(head.size()); done = true; }
        }
        if (!done || vu.empty()) { ok = false; break; }
        std::string out;
        for (size_t i = 0; i < parts.size(); ++i) out += (i ? "/" : "") + parts[i];
        ok = d.SetAircraftInfo(out.c_str());
        amend = true;
        break;
    }
    case F_COMM:  ok = !vu.empty() && ca.SetCommunicationType(vu[0]); break;
    case F_HDG:   ok = ca.SetAssignedHeading(vu.empty() ? 0 : atoi(vu.c_str())); break;
    case F_SPD:   ok = ca.SetAssignedSpeed(vu.empty() ? 0 : atoi(vu.c_str())); break;
    case F_STAND:
        if (vu == "AUTO") { AutoStand(cs); return; }
        // Genau wie das Ground Radar Plugin selbst (GRP 1.5, Auswahl im Stand-Menue):
        //  - Stand in der Flight-Strip-Annotation 6 als "s/<stand>/s" ablegen; das liest das
        //    eigene GRP jeden Takt und zeigt den Stand an
        //  - "GRP/S/<stand>" ins Scratchpad: so verteilt GRP an alle anderen Lotsen
        {
            std::string ann = S(ca.GetFlightStripAnnotation(6));
            size_t a = ann.find("s/");
            while (a != std::string::npos)
            {
                size_t b = ann.find("/s", a + 2);
                if (b == std::string::npos) break;
                ann.erase(a, b + 2 - a);
                a = ann.find("s/");
            }
            if (!vu.empty()) ann += "s/" + vu + "/s";
            ca.SetFlightStripAnnotation(6, ann.c_str());
            m_standAnn[cs] = vu;   // eigene Aenderung nicht als neue Information werten
        }
        m_standLive[cs] = vu;
        m_standSrc[cs] = vu.empty() ? std::string() : std::string("StripBay");
        SendScratch(cs, "GRP/S/" + vu);
        m_standSp[cs] = vu;   // den eigenen Befehl nicht noch einmal auswerten
        if (!vu.empty()) Msg(cs + ": stand " + vu + " assigned.");
        break;
    case F_FREQ:
        if (vu.empty() || vu == "AUTO") m_freqOverride.erase(cs);
        else
        {
            std::string f = NormFreq(Split(vu, " ").front());
            if (f.empty()) ok = false;
            else m_freqOverride[cs] = f;
        }
        break;
    case F_HS:
        SetHoldShort(cs, vu);
        return;
    case F_RESP:
    {
        std::vector<std::string> tk = Split(vu, " ");
        std::string u = tk.empty() ? std::string("NONE") : tk[0];
        if (u == "REJECT") { RejectOffer(cs); return; }
        if (u != "NONE" && u != "CANCEL" && u != "ACCEPT" && u != "TAKE" && !IsRespUnit(u)) { ok = false; break; }
        OfferResp(cs, u);
        return;
    }
    case F_STATE:
    {
        GroundCmd c = ParseGroundState(Split(vu, " ").empty() ? vu : Split(vu, " ")[0]);
        if (c == GC_UNKNOWN) { ok = false; break; }
        SetGroundState(cs, c);
        return;
    }
    default: ok = false; break;
    }
    if (ok && amend) ok = d.AmendFlightPlan();
    if (!ok)
        Msg(cs + ": value could not be set (" + v + "). Is another controller tracking the aircraft, or is the input invalid?");
    CFlightPlan fresh = FlightPlanSelect(cs.c_str());
    if (fresh.IsValid() && m_entries.count(cs)) UpdateEntry(fresh, arr);
    PushBoard();
}

void StripBayPlugin::StripTapped(const std::string& cs)
{
    CFlightPlan fp = FlightPlanSelect(cs.c_str());
    if (fp.IsValid()) SetASELAircraft(fp);
    m_aselHidden.clear();
    PushBoard();
}

void StripBayPlugin::SetGroundState(const std::string& cs, int groundCmd)
{
    if (groundCmd < 0 || groundCmd >= GC_COUNT) return;
    auto it = m_entries.find(cs);
    if (it == m_entries.end() || it->second.freeStrip) return;

    // Gleiche Bay: Position bleibt. Andere Bay: unten anhaengen.
    GroundCmd cmd = (GroundCmd)groundCmd;
    // Geparkt (Streifen weg) nur am Gate und unter 3 kt
    if (cmd == GC_PARK && it->second.arrival && !it->second.data.atGate)
    {
        Msg(cs + ": not parked - only at the gate and below 3 kt.");
        PushBoard();
        return;
    }
    int target = it->second.arrival ? ArrivalBayOf(it->second, cmd) : DepartureBay(cmd);
    if (target == BAY_NONE) return;
    // Ankunft an Apron (TXIN) uebergeben -> Apron; sonst bist du zustaendig
    RespOnExit resp{ this, cs, (it->second.arrival && cmd == GC_TXIN) ? std::string("APN") : MyUnit() };
    resp.on = cmd != GC_PARK;
    ChangeState(cs, cmd, target, -1);
}

void StripBayPlugin::ToggleClearance(const std::string& cs)
{
    CFlightPlan fp = FlightPlanSelect(cs.c_str());
    if (!fp.IsValid()) return;
    RespOnExit resp{ this, cs, MyUnit() };
    resp.on = true;

    bool want = !fp.GetClearenceFlag();
    std::string spBefore = S(fp.GetControllerAssignedData().GetScratchPadString());

    // 1. Weg: dokumentierte Scratchpad-Kommandos CLEA / NOTC
    fp.GetControllerAssignedData().SetScratchPadString(want ? "CLEA" : "NOTC");
    CFlightPlan check = FlightPlanSelect(cs.c_str());
    bool now = check.IsValid() && check.GetClearenceFlag();
    if (check.IsValid() && S(check.GetControllerAssignedData().GetScratchPadString()) != spBefore)
        check.GetControllerAssignedData().SetScratchPadString(spBefore.c_str());

    // 2. Weg: eingebaute Tag-Funktion ueber einen Radarschirm
    if (now != want && !m_screens.empty())
    {
        CFlightPlan f = FlightPlanSelect(cs.c_str());
        SetASELAircraft(f);
        POINT pt = { 0, 0 };
        RECT area = { 0, 0, 0, 0 };
        m_screens.front()->StartTagFunction(cs.c_str(), nullptr, TAG_ITEM_TYPE_CLEARENCE, "", nullptr,
                                            TAG_ITEM_FUNCTION_SET_CLEARED_FLAG, pt, area);
        CFlightPlan c2 = FlightPlanSelect(cs.c_str());
        now = c2.IsValid() && c2.GetClearenceFlag();
    }

    if (now != want)
        Msg(cs + ": clearance flag could not be changed. See README, section Clearance-Flag.");

    // Freigabe per Sprechfunk erteilt: Flieger ist auf der Frequenz -> Ground State ON FREQ
    // (nicht bei DCL per CPDLC, dort meldet er sich erst spaeter)
    if (now && want && !m_clrViaDl)
    {
        auto e = m_entries.find(cs);
        if (e != m_entries.end() && !e->second.arrival && !e->second.freeStrip && e->second.data.state == GC_NSTS)
            ChangeState(cs, GC_ONFREQ, BAY_ONFREQ, -1);
    }

    CFlightPlan fresh = FlightPlanSelect(cs.c_str());
    if (fresh.IsValid() && m_entries.count(cs)) UpdateEntry(fresh, m_entries[cs].arrival);
    PushBoard();
}

// Text aus dem Scratchpad-Editor ins EuroScope-Scratchpad schreiben
void StripBayPlugin::SetScratchpad(const std::string& cs, const std::string& text)
{
    CFlightPlan fp = FlightPlanSelect(cs.c_str());
    if (!fp.IsValid()) return;
    // laeuft gerade eine Synchronisation, kommt der neue Text danach (statt des alten)
    auto sq = m_sync.find(cs);
    if (sq != m_sync.end())
    {
        sq->second.base = text;
        PushBoard();
        return;
    }
    if (!fp.GetControllerAssignedData().SetScratchPadString(text.c_str()))
        Msg(cs + ": scratchpad could not be set. Is another controller tracking the aircraft?");
    auto it = m_entries.find(cs);
    CFlightPlan fresh = FlightPlanSelect(cs.c_str());
    if (it != m_entries.end() && fresh.IsValid()) UpdateEntry(fresh, it->second.arrival);
    PushBoard();
}

void StripBayPlugin::RemoveStrip(const std::string& cs)
{
    auto it = m_entries.find(cs);
    if (it == m_entries.end()) return;
    PushUndo(UNDO_REMOVE, cs);
    if (it->second.freeStrip)
    {
        m_trashFree.push_back({ cs, it->second.bay, it->second.data.designator });
        RemoveEntry(cs);
        m_trashOrder.erase(std::remove(m_trashOrder.begin(), m_trashOrder.end(), "#" + cs), m_trashOrder.end());
        m_trashOrder.push_back("#" + cs);
    }
    else
    {
        m_hiddenArr[cs] = it->second.arrival;
        RemoveEntry(cs);
        m_pinned.erase(cs);
        m_hidden.insert(cs);
        m_trashOrder.erase(std::remove(m_trashOrder.begin(), m_trashOrder.end(), cs), m_trashOrder.end());
        m_trashOrder.push_back(cs);
    }
    PushBoard();
}

// Streifen erstellen. Gibt es einen Flugplan mit diesem Callsign, entsteht ein
// echter Streifen (bleibt, bis man ihn entfernt), sonst ein Notiz-Streifen.
void StripBayPlugin::CreateStrip(const std::string& text, bool asNote, int bay)
{
    std::string t = Upper(Trim(text));
    if (t.empty()) return;
    if (!asNote)
    {
        CFlightPlan fp = FlightPlanSelect(t.c_str());
        if (fp.IsValid())
        {
            std::string ades = Upper(Trim(S(fp.GetFlightPlanData().GetDestination())));
            std::string adep = Upper(Trim(S(fp.GetFlightPlanData().GetOrigin())));
            bool arrival = m_arrAirports.count(ades) && !m_airports.count(adep);
            m_hidden.erase(t);
            m_hiddenArr.erase(t);
            m_forced[t] = arrival;
            m_pinned.insert(t);   // von Hand erstellt: immer zu sehen, auch ausserhalb der eigenen Bays
            UpdateEntry(fp, arrival);
            PushBoard();
            Msg(t + ": strip created.");
            return;
        }
    }
    // Notiz-Streifen: eindeutiger Name, auch gegenueber Flugplaenen
    std::string key = t;
    while (m_entries.count(key) || FlightPlanSelect(key.c_str()).IsValid()) key += "*";
    if (bay < 0 || bay >= BAY_COUNT) bay = BAY_PENDING;
    Entry& e = m_entries[key];
    e.freeStrip = true;
    e.bay = bay;
    e.data.callsign = key;
    e.data.freeStrip = true;
    m_order[bay].push_back(key);
    PushBoard();
    if (!asNote) Msg(t + ": no flight plan found, note strip created.");
}

// Aus dem Papierkorb zurueckholen: "*" = alle, "#TEXT" = Notiz-Streifen, sonst Callsign
void StripBayPlugin::RestoreStrip(const std::string& id)
{
    if (id == "*")
    {
        m_restoringAll = true;
        for (const std::string& o : std::vector<std::string>(m_trashOrder)) RestoreStrip(o);
        for (const auto& kv : std::map<std::string, bool>(m_hov))
            if (kv.second && !m_entries.count(kv.first)) RestoreStrip(kv.first);
        m_restoringAll = false;
        PushBoard();
        return;
    }
    m_trashOrder.erase(std::remove(m_trashOrder.begin(), m_trashOrder.end(), id), m_trashOrder.end());
    if (!id.empty() && id[0] == '#')
    {
        std::string key = id.substr(1);
        for (size_t i = 0; i < m_trashFree.size(); ++i)
        {
            if (m_trashFree[i].key != key) continue;
            int bay = m_trashFree[i].bay;
            bool desig = m_trashFree[i].designator;
            m_trashFree.erase(m_trashFree.begin() + i);
            if (m_entries.count(key)) break;
            Entry& e = m_entries[key];
            e.freeStrip = true;
            e.bay = bay;
            e.data.callsign = key;
            e.data.freeStrip = true;
            e.data.designator = desig;
            m_order[bay].push_back(key);
            break;
        }
        PushBoard();
        return;
    }
    CFlightPlan fp = FlightPlanSelect(id.c_str());
    if (m_hidden.count(id))
    {
        bool arrival = m_hiddenArr.count(id) ? m_hiddenArr[id] : false;
        m_hidden.erase(id);
        m_hiddenArr.erase(id);
        // versehentlich geparkt: zurueck auf Taxi In
        if (m_parked.erase(id) && fp.IsValid()) ApplyGroundState(id, GC_TXIN);
        // zurueckgeholt = bleibt sicher stehen (wie von Hand erstellt), bis man ihn wieder
        // entfernt oder er weiter als 20 NM weg ist
        if (fp.IsValid()) { m_forced[id] = arrival; UpdateEntry(fp, arrival); }
    }
    else
    {
        auto hv = m_hov.find(id);
        if (hv != m_hov.end() && hv->second)
        {
            hv->second = false;
            SendScratch(id, ".VSID_HOV_FALSE");
            CFlightPlan f2 = FlightPlanSelect(id.c_str());
            if (f2.IsValid()) { m_forced[id] = false; UpdateEntry(f2, false); }
        }
    }
    if (!m_restoringAll)
        Msg(m_entries.count(id) ? id + " recovered." : id + ": could not be recovered (no flight plan or out of range).");
    PushBoard();
}

void StripBayPlugin::WindowStateChanged()
{
    SaveWindowSettings();
    // Ansicht gewechselt: Ankuenfte sofort neu einsortieren (Landing nur, wo sichtbar)
    if (m_window.GetView() != m_lastView)
    {
        m_lastView = m_window.GetView();
        Sync();
    }
    if (m_window.GetRwyFilter() != m_lastRwyFilter)
    {
        m_lastRwyFilter = m_window.GetRwyFilter();
        PushBoard();
    }
}

// ---------------------------------------------------------------------------
// Einstellungen (landen in den EuroScope-Settings, wenn du sie speicherst)
// ---------------------------------------------------------------------------
void StripBayPlugin::LoadSettings(WindowSettings& ws)
{
    if (const char* v = GetDataFromSettings("Window"))
    {
        long l, t, r, b;
        int full = 0, top = 0, vis = 1;
        if (sscanf(v, "%ld,%ld,%ld,%ld,%d,%d,%d", &l, &t, &r, &b, &full, &top, &vis) >= 4)
        {
            ws.normalRect.left = l;
            ws.normalRect.top = t;
            ws.normalRect.right = r;
            ws.normalRect.bottom = b;
            ws.fullscreen = full != 0;
            ws.topmost = top != 0;
            ws.visible = vis != 0;
        }
    }
    if (const char* v = GetDataFromSettings("Zoom"))
    {
        int pct = atoi(v);
        if (pct >= 60 && pct <= 250) ws.zoom = pct / 100.0;
    }
    if (const char* v = GetDataFromSettings("Layout"))
    {
        int l = atoi(v);
        if (l >= 0 && l <= 2) ws.layout = l;
    }
    if (const char* v = GetDataFromSettings("View"))
    {
        int vw = atoi(v);
        ws.view = (vw >= 0 && vw < VIEW_COUNT) ? vw : VIEW_ALL;
    }
    if (const char* v = GetDataFromSettings("Theme"))
        ws.light = Lower(Trim(S(v))) == "hell";
    if (const char* v = GetDataFromSettings("ViewPresets"))
    {
        std::string p = Trim(S(v));
        if (p.size() >= 2 && p[1] == ':') { ws.perView = p[0] != '0'; ws.presets = p.substr(2); }
    }
    if (const char* v = GetDataFromSettings("RunwayFilter"))
    {
        int f = atoi(v);
        ws.rwyFilter = (f >= 0 && f <= 2) ? f : 0;
    }
    if (const char* v = GetDataFromSettings("RunwaySplitOn"))
        m_split.enabled = atoi(v) != 0;
    if (const char* v = GetDataFromSettings("ArrivalRange"))
    {
        int nm = atoi(v);
        if (nm >= 3 && nm <= 100) m_arrivalNm = std::min((double)nm, kGoneNm);
    }
    if (const char* v = GetDataFromSettings("Airports"))
    {
        std::string a = Upper(Trim(S(v)));
        if (!a.empty() && a != "AUTO")
        {
            m_autoAirports = false;
            m_airports.clear();
            for (const std::string& p : Split(a, ", ;")) m_airports.insert(p);
            m_arrAirports = m_airports;
        }
    }

    if (const char* v = GetDataFromSettings("IcaoDir"))
    {
        std::string d = Trim(S(v));
        if (!d.empty() && Lower(d) != "auto")
        {
            int n = MultiByteToWideChar(CP_ACP, 0, d.c_str(), -1, nullptr, 0);
            std::wstring w(n, L'\0');
            MultiByteToWideChar(CP_ACP, 0, d.c_str(), -1, &w[0], n);
            w.resize(n - 1);
            m_icaoDir = w;
        }
    }
    if (const char* v = GetDataFromSettings("Wx"))
        m_wxEnabled = atoi(v) != 0;
    if (const char* v = GetDataFromSettings("Cdm"))
        m_vacdmEnabled = atoi(v) != 0;
    if (const char* v = GetDataFromSettings("DlOn")) m_dlEnabled = atoi(v) != 0;
    if (const char* v = GetDataFromSettings("DlCode")) m_dlLogon = Trim(S(v));
    if (const char* v = GetDataFromSettings("DlStation")) m_dlStation = Upper(Trim(S(v)));
    if (const char* v = GetDataFromSettings("DlAuto")) m_dlAutoLogon = atoi(v) != 0;
    if (const char* v = GetDataFromSettings("DlSound")) m_dlSound = atoi(v) != 0;
    for (auto& kv : m_sounds)
        if (const char* v = GetDataFromSettings(("Sound_" + kv.first).c_str())) if (*v) kv.second = Lower(S(v));
    if (const char* v = GetDataFromSettings("TsatSort")) if (*v) m_tsatSort = atoi(v) != 0;
    if (const char* v = GetDataFromSettings("Timeline")) if (*v) m_timeline = atoi(v) != 0;
    // 4.0.0: der Zeitstrahl hat nur noch das DFS-Schema (Tag / Nacht); "TimelineTheme" wird nicht mehr gelesen
    if (const char* v = GetDataFromSettings("TimelineNight")) if (*v) m_tlNight = atoi(v) != 0;
    if (const char* v = GetDataFromSettings("TimelineEstimate")) if (*v) m_tlEst = atoi(v) != 0;
    if (const char* v = GetDataFromSettings("TimelineRange"))
    {
        std::vector<std::string> r = Split(S(v), " ,;");
        if (r.size() == 2)
        {
            m_tlPast = std::max(1, std::min(15, atoi(r[0].c_str())));
            m_tlFuture = std::max(3, std::min(60, atoi(r[1].c_str())));
        }
    }
    if (const char* v = GetDataFromSettings("Responsibility"))
        if (*v) m_respOn = atoi(v) != 0;
    if (const char* v = GetDataFromSettings("UpdateCheck"))
        if (*v) m_updCheck = atoi(v) != 0;
    {
        // 2.0.85: Blinken erst nach 30 s, Ton spaeter (Standard 60 s). Alter Standard (30 s, ohne OfferBlink) -> 60 s.
        const char* vb = GetDataFromSettings("OfferBlink");
        bool hasBlink = vb && *vb;
        if (hasBlink) m_offerBlink = std::max(0, std::min(300, atoi(vb)));
        if (const char* v = GetDataFromSettings("OfferRemind"))
            if (*v)
            {
                m_offerRemind = std::max(0, std::min(600, atoi(v)));
                if (!hasBlink && m_offerRemind == 30) m_offerRemind = 60;
            }
        if (m_offerRemind > 0 && m_offerRemind <= m_offerBlink) m_offerRemind = m_offerBlink + 30;
    }
    if (const char* v = GetDataFromSettings("ColWidths"))
        for (const std::string& kv : Split(S(v), ";"))
        {
            size_t eq = kv.find('=');
            if (eq != std::string::npos) m_colWidths[kv.substr(0, eq)] = std::max(30, std::min(400, atoi(kv.c_str() + eq + 1)));
        }
    if (const char* v = GetDataFromSettings("AutoVacate"))
        m_autoVacate = atoi(v) != 0;
    if (const char* v = GetDataFromSettings("CdmUrl"))
    {
        std::string u = Trim(S(v));
        m_cdmUrl = (Lower(u) == "auto") ? std::string() : u;
    }
    if (const char* v = GetDataFromSettings("RunwayDropState"))
        m_runwayCmd = (Upper(S(v)) == "DEPA") ? GC_DEPA : GC_LINEUP;
    if (const char* v = GetDataFromSettings("NextFreqDep")) m_freqDep = Trim(S(v));
    if (const char* v = GetDataFromSettings("NextFreqArr")) m_freqArr = Trim(S(v));
    // Platz-Einstellungen (Ketten, Haltepunkte, Aufteilung) des aktuellen Profils
    LoadProfileOptions();
    if (const char* v = GetDataFromSettings("GrpAutoFn")) { int n = atoi(v); if (n >= 0 && n < 1000) m_grpAutoFn = n; }
    if (const char* v = GetDataFromSettings("TopDown")) m_topDown = strcmp(v, "0") != 0;
    if (const char* v = GetDataFromSettings("NextButton")) m_nextBtn = strcmp(v, "0") != 0;
    if (const char* v = GetDataFromSettings("SqkFnTopSky")) { int n = atoi(v); if (n > 0 && n < 100000) m_sqkFnTs = n; }
    // Transition Altitude ist fest 5000 ft (Deutschland), keine Einstellung mehr
    if (const char* v = GetDataFromSettings("Range"))   // alt: daraus nur noch die Erfassung
    {
        int cap = atoi(v);
        if (cap > 0 && cap <= 30) m_captureNm = cap;
    }
    if (const char* v = GetDataFromSettings("Range2"))
    {
        int cap = 0, rem = 0;
        if (sscanf(v, "%d,%d", &cap, &rem) == 2 && cap > 0 && rem >= cap)
        {
            m_captureNm = std::min((double)cap, kGoneNm);
            m_removeNm = std::min((double)rem, kGoneNm);
        }
    }
}

void StripBayPlugin::SaveWindowSettings()
{
    WindowSettings ws = m_window.GetSettings();
    char buf[160];
    snprintf(buf, sizeof(buf), "%ld,%ld,%ld,%ld,%d,%d,%d", (long)ws.normalRect.left, (long)ws.normalRect.top,
             (long)ws.normalRect.right, (long)ws.normalRect.bottom, ws.fullscreen ? 1 : 0, ws.topmost ? 1 : 0,
             ws.visible ? 1 : 0);
    SaveDataToSettings("Window", "StripBay Fensterposition", buf);
    snprintf(buf, sizeof(buf), "%d", (int)(ws.zoom * 100 + 0.5));
    SaveDataToSettings("Zoom", "StripBay Zoom in Prozent", buf);
    snprintf(buf, sizeof(buf), "%d", ws.layout);
    SaveDataToSettings("Layout", "StripBay Anordnung (0 auto, 1 Spalten, 2 Raster)", buf);
    snprintf(buf, sizeof(buf), "%d", ws.view);
    SaveDataToSettings("View", "StripBay Ansicht (0 alle, 1 Delivery, 2 Ground, 3 Tower, 4 Apron)", buf);
    SaveDataToSettings("Theme", "StripBay Farbschema (hell/dunkel)", ws.light ? "hell" : "dunkel");
    SaveDataToSettings("ViewPresets", "StripBay Groesse/Anordnung/Farbschema je Station",
                       ((ws.perView ? "1:" : "0:") + ws.presets).c_str());
    snprintf(buf, sizeof(buf), "%d", ws.rwyFilter);
    SaveDataToSettings("RunwayFilter", "StripBay Pistenanzeige (0 beide, 1 erste, 2 zweite Gruppe)", buf);
}

void StripBayPlugin::SaveOptions()
{
    std::string apts = "AUTO";
    if (!m_autoAirports)
    {
        apts.clear();
        for (const std::string& a : m_airports)
        {
            if (!apts.empty()) apts += ",";
            apts += a;
        }
    }
    SaveDataToSettings("Airports", "StripBay Flughaefen", apts.c_str());
    SaveDataToSettings("Profile", "StripBay Platz-Profil (leer = automatisch)", m_profileForced.c_str());
    SaveDataToSettings("RunwayDropState", "StripBay Status beim Ziehen auf die Runway", m_runwayCmd == GC_DEPA ? "DEPA" : "LINEUP");
    SaveDataToSettings("NextFreqDep", "StripBay naechste Frequenz Abfluege (Ersatzmuster)", m_freqDep.c_str());
    SaveDataToSettings("NextFreqArr", "StripBay naechste Frequenz Ankuenfte (Ersatzmuster)", m_freqArr.c_str());
    SaveDataToSettings(PKey("FreqDep0v3").c_str(), "StripBay Frequenzkette Abfluege Piste unbekannt", m_chainDep[RWY_NONE].c_str());
    SaveDataToSettings(PKey("FreqDep1v3").c_str(), "StripBay Frequenzkette Abfluege erste Piste", m_chainDep[RWY_A].c_str());
    SaveDataToSettings(PKey("FreqDep2v3").c_str(), "StripBay Frequenzkette Abfluege zweite Piste", m_chainDep[RWY_B].c_str());
    SaveDataToSettings(PKey("FreqArr1v2").c_str(), "StripBay Frequenzkette Ankuenfte erste Piste", m_chainArr[RWY_A].c_str());
    SaveDataToSettings(PKey("FreqArr2v2").c_str(), "StripBay Frequenzkette Ankuenfte zweite Piste", m_chainArr[RWY_B].c_str());
    SaveDataToSettings(PKey("FreqChainArr0").c_str(), "StripBay Frequenzkette Ankuenfte Piste unbekannt", m_chainArr[RWY_NONE].c_str());
    SaveDataToSettings(PKey("FreqStand2").c_str(), "StripBay Ankuenfte: Stands=Frequenz", m_freqStand.c_str());
    SaveDataToSettings(PKey("FreqDelv3").c_str(), "StripBay Delivery: Stands=Kette", m_freqDel.c_str());
    SaveDataToSettings(PKey("FreqTwr0").c_str(), "StripBay Tower (eine Bahn)", m_chainTwr[RWY_NONE].c_str());
    SaveDataToSettings(PKey("FreqTwr1").c_str(), "StripBay Tower erste Bahn", m_chainTwr[RWY_A].c_str());
    SaveDataToSettings(PKey("FreqTwr2").c_str(), "StripBay Tower zweite Bahn", m_chainTwr[RWY_B].c_str());
    SaveDataToSettings(PKey("FreqSid").c_str(), "StripBay Abflug nach SID", m_freqSid.c_str());
    SaveDataToSettings(PKey("HoldShortList2").c_str(), "StripBay Hold-Short-Schnellauswahl", m_hsList.c_str());
    SaveDataToSettings("GrpAutoFn", "StripBay GRP Funktion Auto", std::to_string(m_grpAutoFn).c_str());
    SaveDataToSettings("TopDown", "StripBay Top-down", m_topDown ? "1" : "0");
    SaveDataToSettings("NextButton", "StripBay Weiter-Taste", m_nextBtn ? "1" : "0");
    SaveDataToSettings("SqkFnTopSky", "StripBay TopSky Funktion Squawk", std::to_string(m_sqkFnTs).c_str());
    SaveDataToSettings(PKey("IntNorth").c_str(), "StripBay Intersections erste Piste", m_intNorth.c_str());
    SaveDataToSettings(PKey("IntSouth").c_str(), "StripBay Intersections zweite Piste", m_intSouth.c_str());
    SaveDataToSettings(PKey("GndStands").c_str(), "StripBay Stands ohne Apron (Startup bei Ground)", m_gndStands.c_str());
    char buf[64];
    snprintf(buf, sizeof(buf), "%d", (int)m_arrivalNm);
    SaveDataToSettings("ArrivalRange", "StripBay Ankuenfte im Final ab NM", buf);
    snprintf(buf, sizeof(buf), "%d", m_transitionAlt);
    SaveDataToSettings("TransitionAltitude", "StripBay Transition Altitude", buf);
    snprintf(buf, sizeof(buf), "%d,%d", (int)m_captureNm, (int)m_removeNm);
    SaveDataToSettings("Range2", "StripBay Erfassen / Sicherheitsgrenze in NM", buf);
    SaveDataToSettings("Cdm", "StripBay vACDM-Abfrage an/aus", m_vacdmEnabled ? "1" : "0");
    SaveDataToSettings("DlOn", "StripBay CPDLC (Hoppie) an/aus", m_dlEnabled ? "1" : "0");
    SaveDataToSettings("DlCode", "StripBay Hoppie Logon-Code", m_dlLogon.c_str());
    SaveDataToSettings("DlStation", "StripBay CPDLC-Station (leer = Platz)", m_dlStation.c_str());
    SaveDataToSettings("DlAuto", "StripBay CPDLC-Logon automatisch annehmen", m_dlAutoLogon ? "1" : "0");
    SaveDataToSettings("DlSound", "StripBay CPDLC-Ton", m_dlSound ? "1" : "0");
    SaveDataToSettings("AutoVacate", "StripBay Ankuenfte nach Verlassen der Piste nach Taxi In", m_autoVacate ? "1" : "0");
    SaveDataToSettings("CdmUrl", "StripBay vACDM-Server", m_cdmUrl.empty() ? "auto" : m_cdmUrl.c_str());
}

// ---------------------------------------------------------------------------
// Befehle: .strips ...
// ---------------------------------------------------------------------------
bool StripBayPlugin::OnCompileCommand(const char* sCommandLine)
{
    std::vector<std::string> t = Split(Trim(S(sCommandLine)), " \t");
    if (t.empty()) return false;
    std::string cmd = Lower(t[0]);
    if (cmd != ".strips" && cmd != ".sb") return false;
    struct InCmd { int& n; InCmd(int& x) : n(x) { ++n; } ~InCmd() { --n; } } inCmd(m_inCommand);

    std::string sub = t.size() > 1 ? Lower(t[1]) : "help";
    std::string arg = t.size() > 2 ? t[2] : "";
    // alles nach dem Befehlswort, Gross/Klein wie eingegeben (fuer Texte und Listen)
    std::string tail;
    {
        std::string line = Trim(S(sCommandLine));
        size_t p0 = line.find_first_of(" \t");
        if (p0 != std::string::npos && t.size() > 1)
        {
            size_t p1 = line.find(t[1], p0);
            if (p1 != std::string::npos) tail = Trim(line.substr(p1 + t[1].size()));
        }
    }

    // Einheitliches Schema: .strips <command> [value], Schalter mit on | off.
    // Die alten (deutschen) Namen bleiben als Alias erhalten.
    {
        std::string la = Lower(arg);
        if (la == "an" || la == "ein") arg = "on";
        else if (la == "aus") arg = "off";
    }
    if (sub == "window")
    {
        sub = (Lower(arg) == "hide" || Lower(arg) == "off") ? "hide" : "show";
    }
    else if (sub == "station") sub = "view";
    else if (sub == "area")
    {
        // .strips area combined | north | south
        std::string la = Lower(arg);
        sub = "piste";
        if (la == "combined" || la == "both" || la.empty()) arg = "beide";
    }
    else if (sub == "runways" || sub == "split") sub = "piste";
    else if (sub == "weather") sub = "wx";
    else if (sub == "airports") sub = "apt";
    else if (sub == "arrivals") sub = "arr";
    else if (sub == "handover")
    {
        // .strips handover undo CALLSIGN
        if (Lower(arg) == "undo") arg = t.size() > 3 ? t[3] : "";
        sub = "hov";
    }
    else if (sub == "ontop") sub = "top";
    else if (sub == "notiz") sub = "note";
    else if (sub == "neu") sub = "new";
    else if (sub == "zurueck" || sub == "recover" || sub == "zur\xFC" "ck" || sub == "zur\xC3\xBC" "ck") sub = "restore";
    else if (sub == "settings" || sub == "einstellungen" || sub == "options")
    {
        m_window.OpenSettings();
        return true;
    }

    if (sub == "show")
    {
        m_window.Show(true);
        SaveWindowSettings();
    }
    else if (sub == "hide")
    {
        m_window.Show(false);
        SaveWindowSettings();
    }
    else if (sub == "full" || sub == "fullscreen")
    {
        bool cur = m_window.GetSettings().fullscreen;
        std::string la = Lower(arg);
        if (!((la == "on" && cur) || (la == "off" && !cur))) m_window.ToggleFullscreen();
        SaveWindowSettings();
    }
    else if (sub == "top")
    {
        std::string la = Lower(arg);
        bool on = la == "on" ? true : la == "off" ? false : !m_window.GetSettings().topmost;
        m_window.SetTopmost(on);
        SaveWindowSettings();
        Msg(on ? "Window now stays on top." : "Window no longer stays on top.");
    }
    else if (sub == "zoom")
    {
        int pct = atoi(arg.c_str());
        if (pct < 60 || pct > 250)
        {
            Msg("Zoom in percent, 60 to 250. Example: .strips zoom 130");
            return true;
        }
        m_window.SetZoom(pct / 100.0);
        SaveWindowSettings();
    }
    else if (sub == "view" || sub == "ansicht")
    {
        std::string a = Lower(arg);
        int v = -1;
        if (a == "del" || a == "delivery") v = VIEW_DEL;
        else if (a == "gnd" || a == "ground") v = VIEW_GND;
        else if (a == "twr" || a == "tower") v = VIEW_TWR;
        else if (a == "apn" || a == "apron" || a == "vorfeld") v = VIEW_APN;
        else if (a == "alle" || a == "all" || a == "voll") v = VIEW_ALL;
        if (v < 0)
        {
            Msg("Usage: .strips station del | apn | gnd | twr | all");
            return true;
        }
        m_window.SetView(v);
        WindowStateChanged();
    }
    else if (sub == "hell" || sub == "light" || sub == "dunkel" || sub == "dark" || sub == "theme")
    {
        bool light = (sub == "hell" || sub == "light");
        if (sub == "theme") light = (Lower(arg) == "hell" || Lower(arg) == "light");
        m_window.SetLight(light);
        SaveWindowSettings();
        Msg(light ? "Light theme (DFS style)." : "Dark theme.");
    }
    else if (sub == "perstation" || sub == "perview")
    {
        // perstation on|off: Groesse, Anordnung und Farbschema je Station merken
        std::string a = Lower(arg);
        if (a != "on" && a != "off" && a != "an" && a != "aus") { Msg("Usage: .strips perstation on|off"); return true; }
        m_window.SetPerView(a == "on" || a == "an");
        SaveWindowSettings();
        Msg(m_window.PerView() ? "Layout per station on: size, layout and theme are remembered for each station."
                               : "Layout per station off: one size, layout and theme for all stations.");
    }
    else if (sub == "search" || sub == "filter" || sub == "find")
    {
        // search TEXT: nur passende Streifen zeigen (ohne Text: Filter aus)
        std::string f;
        for (size_t i = 2; i < t.size(); ++i) f += (f.empty() ? "" : " ") + t[i];
        m_window.SetFilter(f);
        return true;
    }
    else if (sub == "layout")
    {
        std::string a = Lower(arg);
        int l = -1;
        if (a == "auto") l = 0;
        else if (a == "spalten" || a == "cols" || a == "columns") l = 1;
        else if (a == "raster" || a == "grid") l = 2;
        if (l < 0)
        {
            Msg("Usage: .strips layout auto | columns | grid");
            return true;
        }
        m_window.SetLayout(l);
        SaveWindowSettings();
    }
    else if (sub == "apt" || sub == "airport" || sub == "airports")
    {
        std::string a = Upper(arg);
        for (size_t i = 3; i < t.size(); ++i) a += "," + Upper(t[i]);
        if (a.empty() || a == "AUTO")
        {
            m_autoAirports = true;
            m_airports.clear();
            m_arrAirports.clear();
            RefreshAirports();
            Msg("Airports automatic (active airports from the runway dialog).");
        }
        else
        {
            m_autoAirports = false;
            m_airports.clear();
            for (const std::string& p : Split(a, ", ;")) m_airports.insert(p);
            m_arrAirports = m_airports;
            m_vacdm.SetAirports(m_airports);
            Msg("Airports fixed: " + a);
        }
        SaveOptions();
        Sync();
    }
    else if (sub == "takeoff")
    {
        std::string a = Upper(arg);
        if (a != "DEPA" && a != "LINEUP")
        {
            Msg("Usage: .strips takeoff DEPA  or  .strips takeoff LINEUP");
            return true;
        }
        m_runwayCmd = (a == "DEPA") ? GC_DEPA : GC_LINEUP;
        SaveOptions();
        Msg("A departure dragged onto the runway now gets " + a + ".");
    }
    else if (sub == "wx" || sub == "wetter" || sub == "metar" || sub == "atis")
    {
        std::string a = Lower(arg);
        if (a == "aus" || a == "off" || a == "an" || a == "on")
        {
            m_wxEnabled = (a == "an" || a == "on");
            SaveDataToSettings("Wx", "StripBay METAR/ATIS-Abfrage an/aus", m_wxEnabled ? "1" : "0");
            UpdateWx();
            PushBoard();
        }
        std::string apt = WxAirport();
        Msg("METAR/ATIS for " + (apt.empty() ? std::string("(no airport)") : apt) +
            (m_wxEnabled ? "" : " (download off, METAR from EuroScope only)"));
        if (m_wxEnabled) Msg(m_wx.Status());
        Msg("  .strips weather off  stops the VATSIM download, .strips weather on  turns it back on.");
    }
    else if (sub == "neu" || sub == "new" || sub == "notiz" || sub == "note")
    {
        std::string text = tail;
        if (text.empty())
        {
            Msg("Usage: .strips new CALLSIGN   or   .strips note TEXT   (note strip without flight plan)");
            return true;
        }
        CreateStrip(text, sub == "notiz" || sub == "note", BAY_PENDING);
    }
    else if (sub == "zurueck" || sub == "zur\xFC" "ck" || sub == "restore")
    {
        std::string a = Upper(arg);
        RestoreStrip(a.empty() || a == "ALLE" || a == "ALL" ? "*" : a);
        Msg(a.empty() || a == "ALLE" || a == "ALL" ? "All strips recovered."
                                                   : a + " restored.");
    }
    else if (sub == "hs" || sub == "holdshort")
    {
        std::string rest = Upper(tail);
        if (Lower(rest) == "reset") { const AirportProfile* pr = FindProfile(m_profileIcao); m_hsList = pr ? pr->hsList : ""; }
        else if (!rest.empty()) m_hsList = rest;
        SaveOptions();
        Msg("Hold short quick list (plus the runways): " + m_hsList);
        Msg("  Change: .strips hs V1 V3 P1 ...   default: .strips hs reset");
    }
    else if (sub == "intersections" || sub == "int")
    {
        // .strips intersections north|south LIST | reset
        std::string a = Lower(arg);
        std::string rest = Upper(Trim(tail.size() > arg.size() ? tail.substr(arg.size()) : std::string()));
        int g = ParseRunwayFilterArg(arg);
        if (a == "reset")
        {
            const AirportProfile* pr = FindProfile(m_profileIcao);
            m_intNorth = pr ? pr->intNorth : "";
            m_intSouth = pr ? pr->intSouth : "";
        }
        else if (g == RWY_A && !rest.empty()) m_intNorth = rest;
        else if (g == RWY_B && !rest.empty()) m_intSouth = rest;
        SaveOptions();
        PushBoard();
        Msg("Intersections north: " + m_intNorth);
        Msg("Intersections south: " + m_intSouth);
        Msg("  Change: .strips intersections north L1 L2 K1 ...   default: .strips intersections reset");
    }
    else if (sub == "gndstands" || sub == "gndstand")
    {
        // .strips gndstands W*,G*,A1*   Stands ohne Apron (GAT, Apron 1, W): Startup bei Ground
        std::string rest = Upper(tail);
        if (Lower(rest) == "reset") { const AirportProfile* pr = FindProfile(m_profileIcao); m_gndStands = pr ? pr->gndStands : ""; }
        else if (!rest.empty()) m_gndStands = rest;
        SaveOptions();
        Sync();
        Msg("Stands without apron (ground gives startup, STARTUP bay at ground): " + m_gndStands);
        Msg("  Change: .strips gndstands W*,G*   (* = any)   default: .strips gndstands reset");
    }
    else if (sub == "hov")
    {
        // Weggeschickt zuruecknehmen: Streifen kommt zurueck, solange der Flieger in Reichweite ist
        std::string c = Upper(arg);
        if (c.empty() || !FlightPlanSelect(c.c_str()).IsValid())
        {
            Msg("Usage: .strips handover undo CALLSIGN");
            return true;
        }
        m_hov[c] = false;
        SendScratch(c, ".VSID_HOV_FALSE");
        CFlightPlan fp = FlightPlanSelect(c.c_str());
        if (fp.IsValid() && QualifiesDeparture(fp, true)) UpdateEntry(fp, false);
        PushBoard();
        Msg(c + ": handover undone.");
    }
    else if (sub == "freq")
    {
        // .strips freq                       Status
        // .strips freq dep KETTE             Abfluege am geteilten Platz
        // .strips freq nord|sued|1|2 KETTE   Ankuenfte je Bahn (auch Name der Gruppe)
        // .strips freq reset                 Berlin-Standard
        std::string a = Lower(arg);
        std::string rest;
        for (size_t i = 3; i < t.size(); ++i) rest += (rest.empty() ? "" : " ") + t[i];
        if (a == "reset" || a == "berlin")
        {
            SetFreqDefaults();
            m_freqDep = "{APT}_*DEP,{APT}_*APP";
            m_freqArr = "{APT}_*GND";
            SaveOptions();
            Sync();
        }
        else if (!a.empty())
        {
            std::string* target = nullptr;
            std::string* target2 = nullptr;
            if (a == "dep")
            {
                // ".strips freq dep nord|sued KETTE" je Bahn, ".strips freq dep KETTE" fuer beide
                size_t sp = rest.find(' ');
                int g = ParseRunwayFilterArg(rest.substr(0, sp));
                if ((g == RWY_A || g == RWY_B) && sp != std::string::npos)
                {
                    target = &m_chainDep[g];
                    if (g == RWY_A) target2 = &m_chainDep[RWY_NONE];
                    rest = Trim(rest.substr(sp + 1));
                }
                else { target = &m_chainDep[RWY_A]; target2 = &m_chainDep[RWY_B]; }
                if (!rest.empty() && target2) { m_chainDep[RWY_NONE] = (Lower(rest) == "aus" || rest == "-") ? std::string() : rest; }
            }
            else if (a == "stand" || a == "stands")
            {
                // ".strips freq stand +G*=129.505" haengt eine Gruppe vorne an
                if (!rest.empty() && rest[0] == '+') { rest = rest.substr(1) + (m_freqStand.empty() ? "" : "; " + m_freqStand); }
                target = &m_freqStand;
            }
            else if (a == "del" || a == "delivery")
            {
                // ".strips freq del +GRUPPE" haengt eine Gruppe vorne an, sonst ersetzen
                if (!rest.empty() && rest[0] == '+') { rest = rest.substr(1) + "; " + m_freqDel; }
                target = &m_freqDel;
            }
            else if (a == "arr" || a == "unbekannt") target = &m_chainArr[RWY_NONE];
            else if (a == "sid")
            {
                // ".strips freq sid PENEM,ORTAG=126.065; *=126.175" Abfluege nach SID
                target = &m_freqSid;
            }
            else if (a == "tower" || a == "twr")
            {
                // ".strips freq tower [north|south] 125.955" (Ground/Delivery -> Tower)
                size_t sp = rest.find(' ');
                int g = ParseRunwayFilterArg(rest.substr(0, sp));
                if ((g == RWY_A || g == RWY_B) && sp != std::string::npos) { target = &m_chainTwr[g]; rest = Trim(rest.substr(sp + 1)); }
                else target = &m_chainTwr[RWY_NONE];
            }
            else
            {
                int g = ParseRunwayFilterArg(arg);
                if (g == RWY_A || g == RWY_B) target = &m_chainArr[g];
            }
            if (!target || rest.empty())
            {
                Msg("Usage: .strips freq dep [north|south] EDDB_*APP=120.340 > 136.450 > 122.800!");
                Msg("           .strips freq north 129.505 > 129.605   (arrivals per runway)   .strips freq reset");
                return true;
            }
            *target = (Lower(rest) == "aus" || rest == "-") ? std::string() : rest;
            if (target2) *target2 = *target;
            SaveOptions();
            Sync();
        }
        Msg("Next frequency: 1. running handoff, 2. chain for " + m_profileIcao +
            ", 3. next controller according to EuroScope, 4. patterns");
        for (int g = RWY_A; g <= RWY_B; ++g)
            Msg("  Departure " + AcpToUtf8(m_split.name[g]) + ":  " + (m_chainDep[g].empty() ? "-" : m_chainDep[g]));
        Msg("  Delivery by stand: " + (m_freqDel.empty() ? std::string("-") : m_freqDel));
        Msg("  Arrival by stand:   " + (m_freqStand.empty() ? std::string("-") : m_freqStand));
        Msg("  Departure by SID:   " + (m_freqSid.empty() ? std::string("-") : m_freqSid));
        Msg("  Tower (for ground/delivery): " + (m_chainTwr[RWY_NONE].empty() ? std::string("-") : m_chainTwr[RWY_NONE]) +
            (m_chainTwr[RWY_A].empty() ? std::string() : "   north " + m_chainTwr[RWY_A]) +
            (m_chainTwr[RWY_B].empty() ? std::string() : "   south " + m_chainTwr[RWY_B]));
        Msg("  Arrival (after landing): " + (m_chainArr[RWY_NONE].empty() ? std::string("-") : m_chainArr[RWY_NONE]));
        for (int g = RWY_A; g <= RWY_B; ++g)
            Msg("  Arrival " + AcpToUtf8(m_split.name[g]) + ":  " + (m_chainArr[g].empty() ? "-" : m_chainArr[g]));
        Msg("  PATTERN=FREQ: controller with matching callsign online.  FREQ: someone online on it.  FREQ!: always.");
    }
    else if (sub == "cpdlc" || sub == "dcl" || sub == "hoppie")
    {
        // .strips cpdlc code XXXX | station EDDB|auto | an | aus | auto an|aus | ton an|aus
        std::string a = Lower(arg);
        std::string v = t.size() > 3 ? t[3] : std::string();
        std::string lv = Lower(v);
        if ((a == "code" || a == "logon") && !v.empty())
        {
            m_dlLogon = v;
            m_dlEnabled = true;
            Msg("Hoppie logon code saved, CPDLC is on.");
        }
        else if (a == "station" && !v.empty()) m_dlStation = (lv == "auto") ? std::string() : Upper(v);
        else if (a == "an" || a == "on") m_dlEnabled = true;
        else if (a == "aus" || a == "off") m_dlEnabled = false;
        else if (a == "auto" || a == "autologon") m_dlAutoLogon = !(lv == "aus" || lv == "off" || lv == "0");
        else if (a == "ton" || a == "sound") m_dlSound = !(lv == "aus" || lv == "off" || lv == "0");
        else if (!a.empty())
        {
            Msg("Usage: .strips cpdlc code LOGONCODE | station EDDB|auto | on | off | auto on|off | sound on|off");
            return true;
        }
        SaveOptions();
        UpdateDatalink();
        PushBoard();
        Msg(std::string("CPDLC (Hoppie): ") + (m_dlEnabled ? "on" : "off") + ", " +
            (m_hoppie.IsRunning() ? m_hoppie.Status() : std::string(m_dlLogon.empty() ? "no logon code (.strips cpdlc code ...)" : "Station " + DlStation())) +
            ", accept logons automatically: " + (m_dlAutoLogon ? "on" : "off") + ", sound: " + (m_dlSound ? "on" : "off"));
    }
    else if (sub == "vacate" || sub == "verlassen")
    {
        // .strips vacate an|aus: Ankuenfte nach dem Verlassen der Piste automatisch nach Taxi In
        std::string a = Lower(arg);
        if (a == "an" || a == "on" || a == "1") m_autoVacate = true;
        else if (a == "aus" || a == "off" || a == "0") m_autoVacate = false;
        SaveOptions();
        int n = 0;
        for (const RwyGeom& r : m_rwyGeom) if (r.apt == m_split.airport) ++n;
        Msg(std::string("Runway vacated -> taxi in: ") + (m_autoVacate ? "on" : "off") +
            "  (" + std::to_string(m_rwyGeom.size()) + " runways in the sector file, " + std::to_string(n) + " at " + m_split.airport + ")");
    }
    else if (sub == "ta")
    {
        Msg("Transition altitude is fixed at 5000 ft.");
    }
    else if (sub == "icao")
    {
        // Rest der Zeile als Ordner (Pfade koennen Leerzeichen enthalten)
        std::string line = Trim(S(sCommandLine));
        size_t p = Lower(line).find("icao");
        std::string dir = Trim(line.substr(p + 4));
        if (dir.empty())
        {
            char buf[96];
            snprintf(buf, sizeof(buf), "ICAO data: %d airlines, %d airports.", (int)m_icao.airlines.size(),
                     (int)m_icao.airports.size());
            Msg(buf);
            auto narrow = [](const std::wstring& w) {
                if (w.empty()) return std::string("not found");
                int n = WideCharToMultiByte(CP_ACP, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
                std::string a(n, '\0');
                WideCharToMultiByte(CP_ACP, 0, w.c_str(), -1, &a[0], n, nullptr, nullptr);
                a.resize(n - 1);
                return a;
            };
            DisplayUserMessage(STRIPBAY_NAME, STRIPBAY_NAME, ("Airlines: " + narrow(m_icao.airlinesPath)).c_str(), true,
                               true, false, false, false);
            DisplayUserMessage(STRIPBAY_NAME, STRIPBAY_NAME, ("Airports: " + narrow(m_icao.airportsPath)).c_str(), true,
                               true, false, false, false);
            {
                size_t n = 0;
                for (const auto& kv : m_icao.stands) n += kv.second.size();
                DisplayUserMessage(STRIPBAY_NAME, STRIPBAY_NAME,
                                   ("Stands (GRP): " + narrow(m_icao.standsPath) + "  (" + std::to_string(n) + " Stands)").c_str(),
                                   true, true, false, false, false);
            }
            Msg("Set folder: .strips icao C:\\path\\to\\package   or search again: .strips icao auto");
            return true;
        }
        if (Lower(dir) == "auto")
        {
            m_icaoDir.clear();
            SaveDataToSettings("IcaoDir", "StripBay Ordner mit ICAO-Dateien", "auto");
        }
        else
        {
            int n = MultiByteToWideChar(CP_ACP, 0, dir.c_str(), -1, nullptr, 0);
            std::wstring w(n, L'\0');
            MultiByteToWideChar(CP_ACP, 0, dir.c_str(), -1, &w[0], n);
            w.resize(n - 1);
            m_icaoDir = w;
            SaveDataToSettings("IcaoDir", "StripBay Ordner mit ICAO-Dateien", dir.c_str());
        }
        m_icaoReport = true;
        StartIcaoSearch();
        Msg("Searching ICAO data ...");
    }
    else if (sub == "stand")
    {
        // Diagnose: wo steht die Parkposition bei diesem Flieger?
        CFlightPlan fp = arg.empty() ? FlightPlanSelectASEL() : FlightPlanSelect(Upper(arg).c_str());
        if (!fp.IsValid())
        {
            Msg("Select the aircraft in EuroScope or give a callsign: .strips stand DLH4AB");
            return true;
        }
        std::string cs = S(fp.GetCallsign());
        std::string src;
        std::string stand = StandOf(fp, cs, &src);
        Msg(cs + ": Stand " + (stand.empty() ? std::string("not found") : stand + " (from " + src + ")"));
        CFlightPlanControllerAssignedData ca = fp.GetControllerAssignedData();
        for (int i = 0; i <= 8; ++i)
        {
            std::string a = S(ca.GetFlightStripAnnotation(i));
            if (!a.empty()) Msg("  Annotation " + std::to_string(i) + ": " + a);
        }
        Msg("  Scratchpad: " + S(ca.GetScratchPadString()));
    }
    else if (sub == "arr")
    {
        int nm = atoi(arg.c_str());
        if (nm < 3 || nm > 20)
        {
            Msg("Usage: .strips arrivals 15   (arrivals appear in FINAL from 15 NM, 3-20; beyond 20 NM strips are gone)");
            return true;
        }
        m_arrivalNm = nm;
        SaveOptions();
        Sync();
    }
    else if (sub == "profile" || sub == "airport")
    {
        // .strips profile EDDP | auto   Platz-Profil festlegen (sonst nach Callsign / aktivem Platz)
        std::string a = Upper(Trim(arg));
        if (a == "AUTO") { m_profileForced.clear(); SaveOptions(); ChooseProfile(); }
        else if (a.size() == 4) { m_profileForced = a; SaveOptions(); ApplyProfile(a); }
        RefreshAirports();   // Flieger des gewaehlten Platzes (bzw. wieder der aktiven Plaetze)
        const char* known = FindProfile(m_profileIcao) ? "" : " (no built-in profile: general behaviour)";
        Msg("Airport profile: " + m_profileIcao + known + (m_profileForced.empty() ? " (automatic)" : " (fixed)") +
            ".  Towers: " + (SplitActive() ? "2 (north/south)" : "1") + ", apron: " + (m_hasApron ? "yes" : "no") +
            (m_autoHandoffFt > 0 ? ", departures gone above " + std::to_string(m_autoHandoffFt) + " ft" : std::string()));
        Msg("Built-in: EDDB Berlin, EDDP Leipzig, EDDV Hannover.  .strips profile EDDV | auto");
    }
    else if (sub == "rwyswitch" || sub == "runwayswitch")
    {
        // Betriebsrichtungswechsel: Abfluege vor dem Rollen auf die aktive Piste umstellen
        auto sw = RunwaySwitchTargets();
        int ok = 0;
        for (const auto& x : sw)
        {
            SetField(x.first, F_RWY, x.second);
            auto e = m_entries.find(x.first);
            if (e != m_entries.end() && NormRwy(e->second.data.rwy) == x.second) ++ok;
        }
        Msg("Runway change: " + std::to_string(ok) + " of " + std::to_string(sw.size()) +
            " departures switched to the active runway (SID kept where valid, otherwise EuroScope picks one).");
    }
    else if (sub == "resync")
    {
        int n = Resync();
        Msg("Resync: sent the state of " + std::to_string(n) + (m_respOn ? " aircraft you are responsible for." : " aircraft (responsibility off)."));
    }
    else if ((sub == "request" || sub == "take" || sub == "give" || sub == "nextresp" || sub == "accept" || sub == "reject") && !m_respOn)
    {
        Msg("Responsibility is off - no handovers. On: .strips responsibility on");
    }
    else if (sub == "take" || sub == "request")
    {
        // take CALLSIGN: Streifen einer anderen Stelle uebernehmen (ein Angebot an mich: annehmen)
        std::string c = Upper(arg);
        if (c.empty() || !m_entries.count(c)) { Msg("Usage: .strips take CALLSIGN"); return true; }
        TakeOver(c);
        Msg(c + ": responsible now " + (EffResp(c).empty() ? std::string("-") : EffResp(c)) + ".");
        PushBoard();
    }
    else if (sub == "give" || sub == "nextresp")
    {
        // give CALLSIGN [UNIT]: an die naechste (bzw. diese) Stelle uebergeben
        std::string c = Upper(arg), u = t.size() > 3 ? Upper(t[3]) : std::string();
        if (c.empty() || !m_entries.count(c)) { Msg("Usage: .strips give CALLSIGN [UNIT]"); return true; }
        if (u.empty()) u = NextRespUnit(c);
        if (u.empty()) { Msg(c + ": no next station."); return true; }
        HandOver(c, u);
        bool offered = m_offer.count(c) && m_offerByMe.count(c);
        Msg(c + (offered ? ": handover offered to " + u + " (waits until accepted)." : ": handed over to " + u + "."));
    }
    else if (sub == "rwystate")
    {
        // rwystate 0|1|2 GRUPPE PISTE TITEL...: Piste frei / Caution (gelb) / Closed (rot, gesperrt).
        // Seit 2.0.80 nur der Status (Kopf der Runway-Bay); ein Designator-Strip ist davon unabhaengig.
        if (t.size() < 6) { Msg("Usage: .strips rwystate 0|1|2 GROUP RWY TITLE"); return true; }
        int st = atoi(t[2].c_str());
        std::string title;
        for (size_t i = 5; i < t.size(); ++i) title += (title.empty() ? "" : " ") + Upper(t[i]);
        // alte Marker-Streifen (bis 2.0.79) entfernen
        for (const char* suf : { " CAUTION", " CLOSED" })
        {
            std::string k = title + suf;
            auto e = m_entries.find(k);
            if (e != m_entries.end() && e->second.freeStrip) RemoveEntry(k);
        }
        if (st <= 0 || st > 2) m_rwyState.erase(title);
        else m_rwyState[title] = st;
        Msg(title + (st == 2 ? ": closed." : st == 1 ? ": caution." : ": open."));
        PushBoard();
    }
    else if (sub == "designator" || sub == "divider")
    {
        // designator BAY TEXT: Designator-Strip (Trennstreifen) in der Bay, normal verschiebbar, nur lokal
        int bay = t.size() > 2 ? atoi(t[2].c_str()) : BAY_PENDING;
        std::string text;
        for (size_t i = 3; i < t.size(); ++i) text += (text.empty() ? "" : " ") + Upper(t[i]);
        if (text.empty()) text = "RUNWAY OBSTRUCTED";
        std::string key = text;
        while (m_entries.count(key) || FlightPlanSelect(key.c_str()).IsValid()) key += "*";
        if (bay < 0 || bay >= BAY_COUNT) bay = BAY_PENDING;
        Entry& e = m_entries[key];
        e.freeStrip = true;
        e.bay = bay;
        e.data.callsign = key;
        e.data.freeStrip = true;
        e.data.designator = true;
        // "OBSTRUCTED", "CLOSED", "VEHICLE": rot, sonst neutral
        if (text.find("OBSTRUCT") != std::string::npos || text.find("CLOSED") != std::string::npos ||
            text.find("VEH") != std::string::npos || text.find("BLOCK") != std::string::npos)
            m_hl[key] = 4;
        m_order[bay].insert(m_order[bay].begin(), key);
        PushBoard();
        Msg(key + ": designator strip created.");
    }
    else if (sub == "hl" || sub == "highlight")
    {
        // hl CALLSIGN 0-5: farblich hervorheben (0 = aus), synchron
        std::string c = Upper(arg);
        int k = t.size() > 3 ? atoi(t[3].c_str()) : 0;
        if (c.empty() || !m_entries.count(c)) { Msg("Usage: .strips hl CALLSIGN 0-5 (0 off, 1 yellow, 2 green, 3 blue, 4 red, 5 purple)"); return true; }
        if (k > 0 && k <= 5) m_hl[c] = k; else m_hl.erase(c);
        SendScratch(c, ".SB_HL_" + std::to_string(k > 0 && k <= 5 ? k : 0));
        CFlightPlan f = FlightPlanSelect(c.c_str());
        if (f.IsValid()) UpdateEntry(f, m_entries[c].arrival);
        PushBoard();
    }
    else if (sub == "beep")
    {
        // beep [request|wilco|unable|offer|TONNAME]: zum Probehoeren
        std::string ev = Lower(arg);
        if (ev.empty()) ev = "request";
        bool isTone = false;
        for (const char* tn : kToneNames) if (ev == tn) isTone = true;
        if (isTone) PlayTone(ev);
        else { auto it = m_sounds.find(ev); PlayTone(it == m_sounds.end() ? std::string("triple") : it->second); }
        Msg("Sound played: " + ev + ".");
    }
    else if (sub == "rwygroup")
    {
        // rwygroup CALLSIGN A|B: Piste der anderen Seite (gleiche Richtung, sonst eine aktive)
        std::string c = Upper(arg), gs = t.size() > 3 ? Upper(t[3]) : std::string();
        auto e = m_entries.find(c);
        if (e == m_entries.end() || e->second.freeStrip || (gs != "A" && gs != "B")) { Msg("Usage: .strips rwygroup CALLSIGN A|B"); return true; }
        int g = gs == "A" ? RWY_A : RWY_B;
        bool arr = e->second.arrival;
        std::string apt = Upper(Trim(arr ? e->second.data.ades : e->second.data.adep));
        std::string cur = NormRwy(arr ? e->second.data.arrRwy : e->second.data.rwy), pick;
        for (const std::string& r : m_split.rwys[g])
            if (cur.size() >= 2 && r.size() >= 2 && r.compare(0, 2, cur, 0, 2) == 0) { pick = r; break; }
        if (pick.empty())
        {
            auto a = m_actRwy[arr ? 1 : 0].find(apt);
            if (a != m_actRwy[arr ? 1 : 0].end())
                for (const std::string& r : a->second) if (m_split.rwys[g].count(r)) { pick = r; break; }
        }
        if (pick.empty()) { Msg(c + ": no runway on that side."); return true; }
        SetField(c, F_RWY, pick);
        Msg(c + ": runway " + pick + ".");
    }
    else if (sub == "accept")
    {
        std::string c = Upper(arg);
        if (!m_offer.count(c)) { Msg(c + ": no handover to accept."); return true; }
        AcceptOffer(c);
        Msg(c + ": handover accepted, responsible now " + (EffResp(c).empty() ? std::string("-") : EffResp(c)) + ".");
    }
    else if (sub == "airportfile" || sub == "aptfile")
    {
        // airportfile [reload] | template: eigene Plaetze aus StripBayAirports.txt (neben der StripBay.dll)
        std::wstring dir = PluginDir();
        std::wstring file = (dir.empty() ? std::wstring() : dir + L"\\") + L"StripBayAirports.txt";
        if (Lower(arg) == "template")
        {
            std::string old;
            if (ReadTextFileW(file, old)) { Msg("StripBayAirports.txt exists already, not overwritten."); return true; }
            std::string tpl =
                "; StripBay - eigene Plaetze. Nach dem Aendern: .strips airportfile reload\r\n"
                "; Ketten wie bei .strips freq: Eintraege mit > trennen, Rufzeichen-Muster=Frequenz, Frequenz! = immer\r\n"
                "[EDDH]\r\n"
                "city = HAMBURG\r\n"
                "runways = 05 15 23 33\r\n"
                "split = no            ; yes = zwei Tower/getrennte Bahnen (dann rwyA/rwyB, depA/depB, arrA/arrB, twrA/twrB)\r\n"
                "apron = yes\r\n"
                "splitGnd = no\r\n"
                "autoHandoffFt = 0     ; >0: IFR-Abfluege verschwinden ueber dieser Hoehe\r\n"
                "dep = EDDH_*APP=118.200 > 122.800!\r\n"
                "arr = 121.805 > EDDH_*GND\r\n"
                "twr = EDDH_TWR\r\n"
                "holdShort = \r\n"
                "intA = \r\n";
            if (!WriteTextFileW(file, tpl)) { Msg("StripBayAirports.txt could not be written."); return true; }
            Msg("Template StripBayAirports.txt written next to StripBay.dll. Edit it, then .strips airportfile reload.");
            return true;
        }
        LoadAirportFile(true);
        std::string cur = m_profileIcao;
        m_profileIcao.clear();
        ApplyProfile(cur);
        return true;
    }
    else if (sub == "export" || sub == "import")
    {
        // export [code] | import: alle Einstellungen in/aus StripBaySettings.txt (neben der StripBay.dll)
        std::wstring dir = PluginDir();
        std::wstring file = (dir.empty() ? std::wstring() : dir + L"\\") + L"StripBaySettings.txt";
        if (sub == "export")
        {
            bool code = Lower(arg) == "code";
            if (ExportSettings(file, code))
                Msg(std::string("Settings exported to StripBaySettings.txt next to StripBay.dll") + (code ? " (with Hoppie code)." : " (without Hoppie code)."));
            else Msg("StripBaySettings.txt could not be written.");
        }
        else
        {
            int n = ImportSettings(file);
            if (n < 0) { Msg("No StripBaySettings.txt next to StripBay.dll."); return true; }
            LoadProfileOptions();
            Msg(std::to_string(n) + " settings imported. Reload StripBay (or restart EuroScope) to apply window and view settings.");
            PushBoard();
        }
    }
    else if (sub == "log")
    {
        // log on|off|flush: Protokoll fuer die Fehlersuche (StripBay.log neben der StripBay.dll)
        std::string v = Lower(arg);
        if (v == "on" || v == "an") { m_logOn = true; Log("log on, StripBay " STRIPBAY_VERSION ", view " + MyUnit()); }
        else if (v == "off" || v == "aus") { Log("log off"); FlushLog(); m_logOn = false; }
        else if (v == "flush") FlushLog();
        else m_logOn = !m_logOn;
        if (m_logOn) Msg("Log on: StripBay.log next to StripBay.dll (written every 5 s). Send it with your bug report.");
        else Msg("Log off.");
        return true;
    }
    else if (sub == "tobt")
    {
        // tobt CALLSIGN NOW|+MIN|HHMM: TOBT an vACDM melden (bestaetigt), vACDM rechnet die TSAT neu
        std::string c = Upper(arg), v = t.size() > 3 ? Upper(t[3]) : std::string("NOW");
        if (c.empty() || !m_entries.count(c) || m_entries[c].freeStrip || m_entries[c].arrival)
        { Msg("Usage: .strips tobt CALLSIGN NOW|+MIN|HHMM"); return true; }
        long long now = (long long)std::time(nullptr), tobt = 0;
        if (v == "NOW") tobt = now;
        else if (!v.empty() && v[0] == '+') tobt = now + 60LL * std::max(0, std::min(180, atoi(v.c_str() + 1)));
        else if (v.size() == 4 && std::all_of(v.begin(), v.end(), ::isdigit)) tobt = TimeOfDayToUtc(v, now);
        if (tobt <= 0) { Msg("Usage: .strips tobt CALLSIGN NOW|+MIN|HHMM"); return true; }
        tobt -= tobt % 60;
        if (!m_vacdmEnabled || !m_vacdm.IsRunning()) { Msg(c + ": vACDM is off, TOBT not sent."); return true; }
        m_vacdm.SetTobt(c, tobt);
        Msg(c + ": TOBT " + FormatHHMM(tobt) + " sent to vACDM.");
    }
    else if (sub == "tsatsort" || sub == "cdmsort")
    {
        // tsatsort on|off: Pending/On Freq nach TSAT, Startup-Vorschlag (blinkt im TSAT-Fenster)
        std::string v = Lower(arg);
        m_tsatSort = v.empty() ? !m_tsatSort : !(v == "off" || v == "aus" || v == "0");
        SaveDataToSettings("TsatSort", "StripBay nach TSAT sortieren", m_tsatSort ? "1" : "0");
        Msg(std::string("Sort by TSAT and startup hint: ") + (m_tsatSort ? "on." : "off."));
        PushBoard();
    }
    else if (sub == "tlplan" || sub == "sequence")
    {
        // tlplan CALLSIGN HHMMSS|NONE: Bahnzeit fuer den Zeitstrahl (Reihenfolge), fuer alle synchron
        std::string c = Upper(arg), v = t.size() > 3 ? Upper(t[3]) : std::string();
        if (c.empty() || !m_entries.count(c) || m_entries[c].freeStrip) { Msg("Usage: .strips tlplan CALLSIGN HHMMSS|NONE"); return true; }
        if (v.size() == 4) v += "00";
        long long pt = PlanFromHhmmss(v, (long long)time(nullptr));
        if (pt > 0) { m_tlPlan[c] = pt; SendScratch(c, ".SB_TP_" + Hhmmss(pt)); }
        else { m_tlPlan.erase(c); SendScratch(c, ".SB_TP_NONE"); }
        PushBoard();
    }
    else if (sub == "timeline" || sub == "tl")
    {
        // timeline [on|off|toggle]   timeline range PAST FUTURE (Minuten, Standard 3 6 wie bei der DFS)
        std::string a = Lower(arg);
        if (a == "theme")
        {
            Msg("Timeline: only the DFS colours since 4.0.0 (Day / Night at the top, or .strips timeline night on|off).");
            PushBoard();
            return true;
        }
        if (a == "estimate" || a == "est")
        {
            std::string v = t.size() > 3 ? Lower(t[3]) : std::string();
            if (v == "on" || v == "1") m_tlEst = true;
            else if (v == "off" || v == "0") m_tlEst = false;
            else m_tlEst = !m_tlEst;
            SaveDataToSettings("TimelineEstimate", "StripBay Zeitstrahl Schaetzung", m_tlEst ? "1" : "0");
            Msg(std::string("Timeline: departures without TTOT/slot ") + (m_tlEst ? "get an estimated, spaced time (EST)." : "wait in HAND OVER IN."));
            PushBoard();
            return true;
        }
        if (a == "night" || a == "day")
        {
            std::string v = t.size() > 3 ? Lower(t[3]) : std::string();
            if (a == "day") m_tlNight = false;
            else if (v == "on" || v == "1") m_tlNight = true;
            else if (v == "off" || v == "0") m_tlNight = false;
            else m_tlNight = !m_tlNight;
            SaveDataToSettings("TimelineNight", "StripBay Zeitstrahl Nacht", m_tlNight ? "1" : "0");
            Msg(std::string("Timeline: ") + (m_tlNight ? "night (dimmed)." : "day."));
            PushBoard();
            return true;
        }
        if (a == "range" || a == "window")
        {
            if (t.size() >= 5)
            {
                m_tlPast = std::max(1, std::min(15, atoi(t[3].c_str())));
                m_tlFuture = std::max(3, std::min(60, atoi(t[4].c_str())));
            }
            else if (t.size() == 3) { m_tlPast = 3; m_tlFuture = 6; }
            else { Msg("Usage: .strips timeline range PAST FUTURE   (minutes, default 3 6)"); return true; }
            std::string rv = std::to_string(m_tlPast) + " " + std::to_string(m_tlFuture);
            SaveDataToSettings("TimelineRange", "StripBay Zeitstrahl Minuten vorher/nachher", rv.c_str());
            Msg("Timeline: " + std::to_string(m_tlPast) + " min past, " + std::to_string(m_tlFuture) + " min ahead.");
            PushBoard();
            return true;
        }
        if (a == "on" || a == "1") m_timeline = true;
        else if (a == "off" || a == "0") m_timeline = false;
        else m_timeline = !m_timeline;
        SaveDataToSettings("Timeline", "StripBay Tower-Zeitstrahl", m_timeline ? "1" : "0");
        Msg(std::string("Tower timeline ") + (m_timeline ? "on (tap = next step, right click / long press / pen button = menu)." : "off (columns)."));
        PushBoard();
    }
    else if (sub == "undo")
    {
        Undo();
    }
    else if (sub == "colw" || sub == "colwidth")
    {
        // colw ANSICHT#PLATZ PROZENT | colw reset: Spaltenbreiten (Ziehen am Bay-Rand)
        if (Lower(arg) == "reset") m_colWidths.clear();
        else if (!arg.empty() && t.size() > 3)
        {
            int pct = std::max(30, std::min(400, atoi(t[3].c_str())));
            if (pct == 100) m_colWidths.erase(arg); else m_colWidths[arg] = pct;
        }
        std::string v;
        for (const auto& kv : m_colWidths) v += (v.empty() ? "" : ";") + kv.first + "=" + std::to_string(kv.second);
        SaveDataToSettings("ColWidths", "StripBay Spaltenbreiten", v.c_str());
        if (Lower(arg) == "reset") Msg("Column widths reset.");
        PushBoard();
    }
    else if (sub == "taxi" || sub == "taxiroute" || sub == "twy")
    {
        // taxi CALLSIGN A B B3 | NONE: Rollweg auf dem Streifen, synchron
        std::string c = Upper(arg);
        if (c.empty() || !m_entries.count(c) || m_entries[c].freeStrip) { Msg("Usage: .strips taxi CALLSIGN A B B3   (NONE = remove)"); return true; }
        std::string rt;
        for (size_t i = 3; i < t.size(); ++i) rt += (rt.empty() ? "" : " ") + t[i];
        SetTaxi(c, Upper(rt) == "NONE" ? std::string() : rt);
    }
    else if (sub == "deice" || sub == "deicing" || sub == "de-ice")
    {
        // deice CALLSIGN [req|done|off]: Enteisung; ohne Angabe weiterschalten (keine -> angefragt -> erledigt -> keine)
        std::string c = Upper(arg), v = t.size() > 3 ? Lower(t[3]) : std::string();
        if (c.empty() || !m_entries.count(c) || m_entries[c].freeStrip) { Msg("Usage: .strips deice CALLSIGN [req|done|off]"); return true; }
        int cur = m_deice.count(c) ? m_deice[c] : 0;
        int st = v == "req" || v == "on" || v == "1" ? 1 : v == "done" || v == "2" ? 2 : v == "off" || v == "none" || v == "0" ? 0 : (cur + 1) % 3;
        SetDeice(c, st);
    }
    else if (sub == "diag" || sub == "diagnose" || sub == "diagnosis")
    {
        std::wstring file;
        if (WriteDiag(file)) Msg("Diagnosis written: StripBay-diag.txt next to StripBay.dll. Attach it to your bug report (github.com/" STRIPBAY_REPO "/issues).");
        else Msg("StripBay-diag.txt could not be written (folder of StripBay.dll not writable?).");
        return true;
    }
    else if (sub == "update")
    {
        // update on|off|check: beim Start nach einer neueren Version auf GitHub sehen
        std::string v = Lower(arg);
        if (v == "on" || v == "an") m_updCheck = true;
        else if (v == "off" || v == "aus") { m_updCheck = false; m_updAvail.clear(); }
        else if (v == "check" || v.empty()) { m_upd.Start(STRIPBAY_REPO); m_updStarted = true; m_updAsked = true; Msg("Looking for a newer version ..."); return true; }
        else { Msg("Usage: .strips update on|off|check"); return true; }
        SaveDataToSettings("UpdateCheck", "StripBay Update-Hinweis (1 an, 0 aus)", m_updCheck ? "1" : "0");
        Msg(m_updCheck ? "Update check on (once after start, asks github.com for the latest release)." : "Update check off.");
        PushBoard();
    }
    else if (sub == "version")
    {
        if (Lower(arg) == "clear") { m_verWarn.clear(); m_verTold.clear(); PushBoard(); }
        Msg(std::string("StripBay ") + STRIPBAY_VERSION + (m_verWarn.empty() ? "" : ". Note: " + m_verWarn) +
            (m_updAvail.empty() ? "" : ". Update available: " + m_updAvail));
        return true;
    }
    else if (sub == "pushdir" || sub == "pb")
    {
        // pushdir CALLSIGN N|E|S|W|NONE: Push-Richtung (Nase), synchron
        std::string c = Upper(arg), d2 = t.size() > 3 ? Upper(t[3]) : std::string();
        if (c.empty() || !m_entries.count(c) || m_entries[c].freeStrip) { Msg("Usage: .strips pushdir CALLSIGN N|E|S|W|NONE"); return true; }
        if (d2 == "N" || d2 == "E" || d2 == "S" || d2 == "W") m_pushDir[c] = d2;
        else { m_pushDir.erase(c); d2 = "NONE"; }
        SendScratch(c, ".SB_PB_" + d2);
        PushBoard();
    }
    else if (sub == "reject")
    {
        std::string c = Upper(arg);
        if (!m_offer.count(c) || m_offerByMe.count(c)) { Msg(c + ": no handover to reject."); return true; }
        RejectOffer(c);
        Msg(c + ": handover rejected.");
    }
    else if (sub == "offerremind" || sub == "remind")
    {
        // offerremind SEKUNDEN: Ton, wenn ein Angebot an mich so lange nicht angenommen ist (0 = aus);
        // immer erst nach dem Blinken
        if (!arg.empty()) m_offerRemind = std::max(0, std::min(600, atoi(arg.c_str())));
        std::string note;
        if (m_offerRemind > 0 && m_offerRemind <= m_offerBlink)
        {
            m_offerRemind = std::min(600, m_offerBlink + 30);
            note = " (after the blinking at " + std::to_string(m_offerBlink) + " s)";
        }
        SaveDataToSettings("OfferRemind", "StripBay Erinnerung an Uebergaben (s)", std::to_string(m_offerRemind).c_str());
        Msg(m_offerRemind ? "Handover sound after " + std::to_string(m_offerRemind) + " s" + note + "." : std::string("Handover sound off."));
        PushBoard();
    }
    else if (sub == "remote")
    {
        // remote on|off|status|code new|port N|disconnect
        std::string a = Lower(arg);
        if (a == "on" || a == "an" || a == "start")
        {
            m_remoteOn = true;
            SaveDataToSettings("Remote", "StripBay Remote", "1");
            if (!m_net.Running()) RemoteStart();
            RemoteStatus();
        }
        else if (a == "off" || a == "aus" || a == "stop")
        {
            m_remoteOn = false;
            SaveDataToSettings("Remote", "StripBay Remote", "0");
            RemoteStop();
            Msg("Remote: off.");
        }
        else if (a == "code")
        {
            std::string v = t.size() > 3 ? Trim(t[3]) : std::string("new");
            if (Lower(v) == "new" || Lower(v) == "neu")
            {
                unsigned seed = (unsigned)GetTickCount() * 2654435761u ^ (unsigned)time(nullptr);
                char b[8];
                snprintf(b, sizeof(b), "%06u", (seed >> 3) % 1000000u);
                v = b;
            }
            bool okc = v.size() == 6;
            for (char ch : v) if (!isdigit((unsigned char)ch)) okc = false;
            if (!okc) { Msg("Usage: .strips remote code new | 6 digits"); return true; }
            m_remoteCode = v;
            m_hub.code = v;
            SaveDataToSettings("RemoteCode", "StripBay Remote Kopplungscode", v.c_str());
            m_hub.DropAll();   // alte Kopplungen gelten nicht mehr
            Msg("Remote: new code " + v + ". Connected devices have to enter it.");
            PushBoard();
        }
        else if (a == "port")
        {
            int pt = t.size() > 3 ? atoi(t[3].c_str()) : 0;
            if (pt < 1024 || pt > 65000) { Msg("Usage: .strips remote port 1024-65000 (default 47810)"); return true; }
            m_remotePort = pt;
            SaveDataToSettings("RemotePort", "StripBay Remote Port", std::to_string(pt).c_str());
            if (m_net.Running()) { RemoteStop(); RemoteStart(); }
            RemoteStatus();
        }
        else if (a == "disconnect" || a == "trennen")
        {
            m_hub.DropAll();
            Msg("Remote: all devices disconnected.");
        }
        else RemoteStatus();
    }
    else if (sub == "responsibility" || sub == "resp" || sub == "zustaendigkeit" || sub == "zust\xE4ndigkeit" || sub == "zust\xC3\xA4ndigkeit")
    {
        std::string a = Lower(arg);
        if (a == "on" || a == "an") SetRespOn(true);
        else if (a == "off" || a == "aus") SetRespOn(false);
        else Msg(std::string("Responsibility: ") + (m_respOn ? "on" : "off") + ". Usage: .strips responsibility on|off");
    }
    else if (sub == "offerblink" || sub == "blink")
    {
        // offerblink SEKUNDEN: so lange ist ein Angebot an mich nur farbig, danach blinkt es (0 = sofort)
        if (!arg.empty()) m_offerBlink = std::max(0, std::min(300, atoi(arg.c_str())));
        SaveDataToSettings("OfferBlink", "StripBay Uebergabe blinkt nach (s)", std::to_string(m_offerBlink).c_str());
        std::string note;
        if (m_offerRemind > 0 && m_offerRemind <= m_offerBlink)
        {
            m_offerRemind = std::min(600, m_offerBlink + 30);
            SaveDataToSettings("OfferRemind", "StripBay Erinnerung an Uebergaben (s)", std::to_string(m_offerRemind).c_str());
            note = ", sound moved to " + std::to_string(m_offerRemind) + " s";
        }
        Msg("Handover blinks after " + std::to_string(m_offerBlink) + " s" + note + ".");
        PushBoard();
    }
    else if (sub == "sound" && !arg.empty())
    {
        // sound EREIGNIS TON: request|wilco|unable|offer  triple|chime|double|low|single|off
        std::string ev = Lower(arg), tn = t.size() > 3 ? Lower(t[3]) : std::string();
        bool okTone = tn == "off";
        for (const char* k : kToneNames) if (tn == k) okTone = true;
        if (!m_sounds.count(ev) || !okTone) { Msg("Usage: .strips sound request|wilco|unable|offer triple|chime|double|low|single|off"); return true; }
        m_sounds[ev] = tn;
        SaveDataToSettings(("Sound_" + ev).c_str(), "StripBay Ton", tn.c_str());
        if (tn != "off") PlayTone(tn);
        Msg("Sound " + ev + ": " + tn + ".");
    }
    else if (sub == "sounds")
    {
        std::string l;
        for (const auto& kv : m_sounds) l += (l.empty() ? "" : ", ") + kv.first + "=" + kv.second;
        Msg("Sounds: " + l + ". Change: .strips sound EVENT triple|chime|double|low|single|off");
    }
    else if (sub == "offer" || sub == "handover")
    {
        // offer CALLSIGN UNIT|CANCEL
        std::string c = Upper(arg), u = t.size() > 3 ? Upper(t[3]) : std::string();
        if (c.empty() || u.empty() || !m_entries.count(c)) { Msg("Usage: .strips offer CALLSIGN UNIT|CANCEL"); return true; }
        OfferResp(c, u);
    }
    else if (sub == "inbound" || sub == "outbound" || sub == "flip")
    {
        // Streifen von Hand zu Inbound bzw. Outbound machen (synchron)
        std::string c = Upper(arg);
        auto e = m_entries.find(c);
        if (c.empty() || e == m_entries.end() || e->second.freeStrip) { Msg("Usage: .strips inbound|outbound|flip CALLSIGN"); return true; }
        bool arr = sub == "inbound" ? true : sub == "outbound" ? false : !e->second.arrival;
        if (!arr && m_divert.count(c)) SetDivert(c, std::string(), true);
        SetDirection(c, arr, true);
        Msg(c + (arr ? ": now inbound." : ": now outbound."));
    }
    else if (sub == "divert" || sub == "diversion")
    {
        // divert CALLSIGN [ICAO|OFF]: Diversion zu uns von Hand eintragen (ohne ICAO: Profilplatz)
        std::string c = Upper(arg);
        std::string a = t.size() > 3 ? Upper(Trim(t[3])) : std::string();
        auto e = m_entries.find(c);
        if (c.empty() || (e == m_entries.end() && !FlightPlanSelect(c.c_str()).IsValid()) || (e != m_entries.end() && e->second.freeStrip))
        { Msg("Usage: .strips divert CALLSIGN [ICAO|OFF]"); return true; }
        if (a == "OFF" || a == "NONE" || (a.empty() && m_divert.count(c)))
        {
            SetDivert(c, std::string(), true);
            Msg(c + ": diversion removed.");
            return true;
        }
        if (a.empty()) a = m_arrAirports.count(m_profileIcao) || m_airports.count(m_profileIcao) ? m_profileIcao
                           : (m_arrAirports.empty() ? m_profileIcao : *m_arrAirports.begin());
        SetDivert(c, a, true);
        Msg(c + ": diversion to " + a + ".");
    }
    else if (sub == "deselect")
    {
        // Auswahl aufheben (EuroScope kennt kein "keiner angewaehlt": StripBay zeigt ihn nicht mehr markiert)
        CFlightPlan sel = FlightPlanSelectASEL();
        m_aselHidden = sel.IsValid() ? S(sel.GetCallsign()) : std::string();
        PushBoard();
    }
    else if (sub == "nextbtn")
    {
        std::string a = Lower(arg);
        if (a == "on" || a == "1") m_nextBtn = true;
        else if (a == "off" || a == "0") m_nextBtn = false;
        else m_nextBtn = !m_nextBtn;
        SaveOptions();
        PushBoard();
        Msg(std::string("Next bay button ") + (m_nextBtn ? "on (\xE2\x96\xB6 top right on the strips)." : "off."));
    }
    else if (sub == "stdclr")
    {
        // Taxi In: bis zur Parkposition freigegeben umschalten (mit allen synchron)
        std::string c = Upper(arg);
        auto e = m_entries.find(c);
        if (e == m_entries.end() || !e->second.arrival) { Msg("Usage: .strips stdclr CALLSIGN (arrival)"); return true; }
        bool now = !m_stdClr.count(c);
        if (now) m_stdClr[c] = true; else m_stdClr.erase(c);
        SendScratch(c, now ? ".SB_SC_TRUE" : ".SB_SC_FALSE");
        CFlightPlan f = FlightPlanSelect(c.c_str());
        if (f.IsValid()) UpdateEntry(f, true);
        PushBoard();
    }
    else if (sub == "topdown")
    {
        std::string a = Lower(arg);
        if (a == "on" || a == "1") m_topDown = true;
        else if (a == "off" || a == "0") m_topDown = false;
        else m_topDown = !m_topDown;
        SaveOptions();
        PushBoard();
        Msg(std::string("Top-down ") + (m_topDown ? "on: unstaffed lower stations (DEL, Apron, Ground) are shown in your view."
                                                   : "off: only your own station's bays."));
    }
    else if (sub == "sqkfn")
    {
        // Funktionsnummer der TopSky-Squawk-Vergabe (vSID: 667)
        int n = atoi(arg.c_str());
        if (n > 0 && n < 100000) { m_sqkFnTs = n; SaveOptions(); }
        Msg("Squawk Auto calls TopSky function " + std::to_string(m_sqkFnTs) + " (.strips sqkfn N to change, default 667).");
    }
    else if (sub == "grpauto")
    {
        // Funktionsnummer "Auto" im Stand-Menue des Ground Radar Plugins (v1.5: 2)
        // 0 = GRP nicht aufrufen, StripBay waehlt selbst aus den GRP-Daten
        std::string a = Lower(arg);
        int n = atoi(arg.c_str());
        if (a == "off" || a == "0") { m_grpAutoFn = 0; SaveOptions(); }
        else if (n > 0 && n < 1000) { m_grpAutoFn = n; SaveOptions(); }
        if (m_grpAutoFn > 0)
            Msg("Stand Auto asks the Ground Radar plugin (function " + std::to_string(m_grpAutoFn) +
                ", needs StripBay allowed on a radar screen) and waits 3 s; if GRP assigns nothing, StripBay picks a free stand from GRpluginStands.txt.");
        else
            Msg("Stand Auto: StripBay picks a free stand from GRpluginStands.txt (GRP is not called). .strips grpauto 2 to ask GRP again.");
    }
    else if (sub == "range")
    {
        int cap = atoi(arg.c_str());
        int rem = t.size() > 3 ? atoi(t[3].c_str()) : 0;
        if (cap <= 0 || rem < cap || rem > 20)
        {
            Msg("Usage: .strips range PICKUP LIMIT (in NM), e.g. .strips range 5 20");
            Msg("Departures disappear when handed over, at the latest at LIMIT (max. 20 NM: beyond that everything is gone).");
            return true;
        }
        m_captureNm = cap;
        m_removeNm = rem;
        SaveOptions();
        Sync();
    }
    else if (sub == "cdm")
    {
        std::string a = Lower(arg);
        if (a == "aus" || a == "off")
        {
            m_vacdmEnabled = false;
            UpdateVacdm();
            SaveOptions();
            Msg("vACDM download off. TSAT only from the CDM plugin.");
        }
        else if (a == "an" || a == "on")
        {
            m_vacdmEnabled = true;
            UpdateVacdm();
            SaveOptions();
            Msg("vACDM download on.");
        }
        else if (a == "url")
        {
            std::string u = t.size() > 3 ? t[3] : "";
            if (u.empty())
            {
                Msg("Usage: .strips cdm url https://...  or  .strips cdm url auto");
                return true;
            }
            m_cdmUrl = (Lower(u) == "auto") ? std::string() : u;
            UpdateVacdm();
            SaveOptions();
            Msg(m_cdmUrl.empty() ? "vACDM server is read from the vacdm.txt of the vACDM plugin."
                                 : "vACDM server fixed: " + m_cdmUrl);
        }
        else
        {
            Msg("TSAT sources: 1. CDM plugin (flight strip annotation), 2. vACDM server.");
            if (!m_vacdmEnabled) Msg("vACDM download is off (.strips cdm on).");
            else if (m_cdmUrlFound.empty()) Msg("vACDM: no vACDM plugin loaded and no URL set (.strips cdm url ...).");
            else Msg(m_vacdm.Status());
        }
    }
    else if (sub == "piste" || sub == "pisten" || sub == "rwy" || sub == "runway")
    {
        std::string a = Lower(arg);
        if (a.empty())
        {
            RunwayStatus();
            Msg("Usage: .strips area combined | north | south   or   .strips runways on | off | berlin | set ...");
            return true;
        }
        if (a == "aus" || a == "off" || a == "an" || a == "on")
        {
            m_split.enabled = (a == "an" || a == "on");
            SaveRunwaySplit();
            Sync();
            RefreshRunways();
            Msg(m_split.enabled ? "Split runways on." : "Split runways off, all bays as before.");
            return true;
        }
        if (a == "berlin" || a == "reset" || a == "standard")
        {
            SetRunwayDefaults();
            SaveRunwaySplit();
            Sync();
            RefreshRunways();
            RunwayStatus();
            return true;
        }
        if (a == "set")
        {
            std::string spec;
            for (size_t i = 3; i < t.size(); ++i) spec += (spec.empty() ? "" : ";") + t[i];
            if (!ParseRunwaySplit(spec))
            {
                Msg("Usage: .strips runways set EDDB NORTH:06L,24R SOUTH:06R,24L");
                Msg("Each runway in one group only, two different names.");
                return true;
            }
            SaveRunwaySplit();
            Sync();
            RefreshRunways();
            RunwayStatus();
            return true;
        }
        int f = ParseRunwayFilterArg(arg);
        if (f < 0)
        {
            Msg("Unknown: " + AcpToUtf8(arg) + ". Usage: .strips area combined | north | south");
            return true;
        }
        m_window.SetRwyFilter(f);
        WindowStateChanged();
        Msg(f == 0 ? std::string("Showing: both runways.") : "Showing: only " + AcpToUtf8(m_split.name[f]) + ".");
        if (!SplitActive()) Msg("Note: " + m_split.airport + " is currently not one of your active airports.");
    }
    else if (sub == "reset")
    {
        m_hidden.clear();
        for (auto& kv : m_entries) kv.second.hasLocal = false;
        Sync();
        Msg("Hidden strips restored, local deviations discarded.");
    }
    else
    {
        Msg("Commands: .strips <command> [value]   (.sb works too; switches take on | off)");
        Msg("  settings                         open the settings window");
        Msg("  window show | hide               show or hide the strip window");
        Msg("  fullscreen [on | off]            fullscreen on the current monitor");
        Msg("  ontop [on | off]                 keep the window on top");
        Msg("  zoom 130                         size in percent (60 to 250)");
        Msg("  theme light | dark               light (DFS style) or dark");
        Msg("  layout auto | columns | grid     bays side by side or in two rows");
        Msg("  timeline on | off | range 3 6    tower: DFS-style time line instead of holding/runway/final");
        Msg("  timeline night on|off, timeline estimate on|off, tlplan CS HHMMSS|NONE (sequence)");
        Msg("  station del | apn | gnd | twr | all   which station to show");
        Msg("  area combined | north | south    split runways: what to show");
        Msg("  runways on | off | berlin        split runways on/off, Berlin default");
        Msg("  runways set EDDB NORTH:06L,24R SOUTH:06R,24L   own runway groups");
        Msg("  airports auto | EDDF,EDDS        airports automatic or fixed");
        Msg("  takeoff lineup | depa            state when a departure is dragged onto the runway");
        Msg("  arrivals 15                      arrivals appear in FINAL from this distance (NM)");
        Msg("  range 5 20                       departures: pickup and safety limit (NM, max. 20)");
        Msg("  grpauto 2                        GRP function number for stand Auto");
        Msg("  rwyswitch                        runway change: move departures (not taxiing) to the active runway");
        Msg("  profile EDDB|EDDP|EDDV|auto      airport profile (automatic from your callsign / active airport)");
        Msg("  resync                           send the state of your aircraft to everyone again");
        Msg("  vacate on | off                  arrival goes to taxi in after vacating the runway");
        Msg("  freq [dep|north|south|stand|del CHAIN | reset]   next frequency chains");
        Msg("  gndstands W*,G* | reset          stands without apron (startup at ground)");
        Msg("  holdshort V1 V2 V3 V4 VC P1 ... | reset   hold short quick list");
        Msg("  intersections north|south LIST | reset   intersection keys per runway");
        Msg("  weather on | off                 METAR and ATIS letter from VATSIM");
        Msg("  cdm on | off | url URL | auto    vACDM (TSAT)");
        Msg("  cpdlc on | off | code CODE | station EDDB | autologon on|off | sound on|off");
        Msg("  new CALLSIGN                     create a strip (without flight plan: note strip)");
        Msg("  note TEXT                        note strip, e.g. for vehicles");
        Msg("  recover CALLSIGN | all           bring strips back (Recover list)");
        Msg("  handover undo CALLSIGN           bring a handed-over departure back");
        Msg("  offerblink SEC / offerremind SEC  handover to me: blinks after SEC (30), sound later (60, 0 = off)");
        Msg("  responsibility on|off  off = nobody is entered as responsible (no handovers), strips follow the ground state");
        Msg("  give CS [UNIT] | accept CS | reject CS | take CS | offer CS CANCEL   handover: offer to the next station, accept, reject, take over, take back");
        Msg("  taxi CS A B B3 | NONE     taxi route on the strip (also via the PB field: TWY...)   deice CS [req|done|off]   de-icing mark");
        Msg("  search TEXT               show only matching strips (callsign, type, runway, SID, stand, airport, DEICE); no text = all");
        Msg("  perstation on|off         remember size, layout and theme for each station");
        Msg("  update on|off|check       look for a newer version on GitHub   version   own version and notes   diag   write StripBay-diag.txt for bug reports");
        Msg("  remote on|off|status|code new|port N  StripBay Remote: use the strips on another Windows PC in your network");
        Msg("  stand [CALLSIGN]                 where the stand comes from (diagnostics)");
        Msg("  icao [folder | auto]             ICAO data (callsigns, airport names)");
        Msg("  reset                            restore hidden strips, drop local deviations");
    }
    return true;
}
