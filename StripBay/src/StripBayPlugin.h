// StripBay - EuroScope-Plugin: haelt die Streifen synchron mit den
// EuroScope-Ground-States (die auch TopSky und das Ground Radar Plugin nutzen).
#pragma once

#include "StripWindow.h"     // bringt windows.h mit, muss vor EuroScopePlugIn.h stehen
#include "EuroScopePlugIn.h"
#include "Cdm.h"
#include "Hoppie.h"
#include "Icao.h"
#include "RemoteProto.h"
#include "RemoteNet.h"
#include "Version.h"

#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

#define STRIPBAY_NAME      "StripBay"
#define STRIPBAY_AUTHOR    "Luis Korb, S\xF6ren Pflug"   // ANSI fuer EuroScope
#define STRIPBAY_COPYRIGHT "Freie Nutzung"

class AnchorScreen;

class StripBayPlugin : public EuroScopePlugIn::CPlugIn, public IStripHost
{
public:
    StripBayPlugin();
    virtual ~StripBayPlugin();

    // ----- EuroScope-Ereignisse -----
    virtual void OnTimer(int Counter) override;
    virtual void OnFlightPlanControllerAssignedDataUpdate(EuroScopePlugIn::CFlightPlan FlightPlan, int DataType) override;
    virtual void OnFlightPlanFlightPlanDataUpdate(EuroScopePlugIn::CFlightPlan FlightPlan) override;
    virtual void OnFlightPlanDisconnect(EuroScopePlugIn::CFlightPlan FlightPlan) override;
    virtual void OnAirportRunwayActivityChanged(void) override;
    virtual void OnNewMetarReceived(const char* sStation, const char* sFullMetar) override;
    virtual bool OnCompileCommand(const char* sCommandLine) override;
    virtual EuroScopePlugIn::CRadarScreen* OnRadarScreenCreated(const char* sDisplayName, bool NeedRadarContent,
                                                                bool GeoReferenced, bool CanBeSaved,
                                                                bool CanBeCreated) override;

    // ----- Rueckmeldungen aus dem Fenster -----
    virtual void StripMoved(const std::string& callsign, int bay, int index) override;
    virtual void StripTapped(const std::string& callsign) override;
    virtual void SetGroundState(const std::string& callsign, int groundCmd) override;
    virtual void ToggleClearance(const std::string& callsign) override;
    virtual void ToggleRunwayClearance(const std::string& callsign) override;
    virtual void GoAround(const std::string& callsign) override;
    virtual void Datalink(const std::string& callsign, int action, const std::string& text) override;
    virtual void SetIntersection(const std::string& callsign, const std::string& text, bool assigned) override;
    virtual void ToggleHandover(const std::string& callsign) override;
    virtual void SetHoldShort(const std::string& callsign, const std::string& text) override;
    virtual void CreateStrip(const std::string& text, bool asNote, int bay) override;
    virtual void RestoreStrip(const std::string& id) override;
    std::vector<std::string> RunwayOptions(const std::string& apt);
    virtual std::map<std::string, int> StandStatus(const std::string& callsign) override;
    // Auto: Stand vom Ground Radar Plugin zuweisen lassen (dessen Tag-Funktion "Auto"),
    // sonst selbst einen freien Stand aus den GRP-Daten waehlen und per GRP/S/ verteilen
    void AutoStand(const std::string& cs);
    void AutoStandFallback(const std::string& cs, const std::string& before, const std::string& why);
    void ProcessAutoStands();
    // Squawk automatisch (TopSky, sonst CCAMS)
    void AutoSquawk(const std::string& cs);
    void ProcessAutoSquawks();
    struct SqkPend { std::string before; long long deadline = 0; std::string plugin; };
    std::map<std::string, SqkPend> m_sqkPending;
    struct AutoPend { std::string before; long long deadline = 0; };
    std::map<std::string, AutoPend> m_autoPending;
    std::string PickFreeStand(const std::string& cs, const std::string& avoid);
    void StandOccupancy(const std::string& apt, std::map<std::string, std::vector<std::string>>& occ,
                        std::map<std::string, std::vector<std::string>>& asg);
    int  m_grpAutoFn = 2;   // Funktionsnummer "Auto" im Stand-Menue des Ground Radar Plugins (v1.5)
    virtual std::vector<std::string> FieldOptions(const std::string& callsign, int field) override;
    virtual void SetField(const std::string& callsign, int field, const std::string& value) override;
    std::vector<std::string> SectorList(int type, const std::string& apt, const std::string& rwy);
    virtual void SetScratchpad(const std::string& callsign, const std::string& text) override;
    virtual void RemoveStrip(const std::string& callsign) override;
    virtual void WindowStateChanged() override;

    void ScreenClosed(AnchorScreen* screen);

private:
    struct Entry
    {
        StripData   data;
        int         bay = BAY_PENDING;
        // Lokale Ueberschreibung, falls EuroScope einen Status nicht annimmt
        // (z. B. ONFREQ in alten ES-Versionen). Gilt nur, solange sich der
        // Ground State in EuroScope nicht aendert.
        bool        hasLocal = false;
        int         localBay = BAY_PENDING;
        std::string localBase;
        bool        localIntentional = false;   // An Tower: gewollt lokal, keine orange Ecke
        bool        arrival = false;
        int         groundAlt = -1;   // Abflug: Hoehe beim Rollen (fuer "airborne")
        bool        released = false; // Ankunft vom Tower aus Runway nach Taxi In gezogen
        bool        onRunway = false; // Ankunft vom Tower auf die Runway gelegt (nur lokal, ohne CTL)
        bool        freeStrip = false;// Notiz-Streifen ohne Flugplan
        int         offRwyCount = 0;  // Ankunft: wie oft hintereinander neben jeder Piste gesehen
        bool        vacated = false;  // Ankunft: Piste verlassen, automatisch nach Taxi In gegeben
    };
    // Pisten aus dem Sektorfile (Enden), fuer "Piste verlassen"
    struct RwyGeom { std::string apt; double lat0, lon0, lat1, lon1; };
    std::vector<RwyGeom> m_rwyGeom;
    bool m_autoVacate = true;          // Ankuenfte nach dem Verlassen der Piste automatisch nach Taxi In
    // Liegt die Position auf einer Piste des Platzes? -1 = keine Pistendaten fuer den Platz
    int OnRunwayGeom(const std::string& apt, double lat, double lon) const;

    // Synchronisation
    void Sync();
    bool QualifiesDeparture(EuroScopePlugIn::CFlightPlan& fp, bool alreadyShown);
    bool QualifiesArrival(EuroScopePlugIn::CFlightPlan& fp, bool alreadyShown);
    void UpdateEntry(EuroScopePlugIn::CFlightPlan& fp, bool arrival);
    std::string StandOf(EuroScopePlugIn::CFlightPlan& fp, const std::string& callsign, std::string* source);
    void RemoveEntry(const std::string& callsign);
    void MoveOrder(const std::string& callsign, int fromBay, int toBay, int index);
    void NormalizeOrder();
    virtual void SetStripGroup(const std::string& callsign, int group) override;
    virtual void Command(const std::string& line) override;
    void ChangeState(const std::string& callsign, GroundCmd cmd, int targetBay, int index);
    bool ApplyGroundState(const std::string& callsign, GroundCmd cmd);
    void SendScratch(const std::string& callsign, const std::string& text);
    // Synchronisation ueber das Scratchpad wie bei vSID: Nachricht setzen, EuroScope einen
    // Takt Zeit geben (dann geht sie ans Netz), erst danach den alten Inhalt zurueck.
    // Mehrere Nachrichten je Flieger nacheinander.
    struct SyncQ { std::deque<std::string> msgs; std::string base; bool busy = false; long long since = 0; };
    std::map<std::string, SyncQ> m_sync;
    long long m_tick = 0;
    void ProcessSync();
    bool IsOwnSync(const std::string& cs, const std::string& sp) const;
    void SetCtl(const std::string& callsign, bool cleared);
    int  ArrivalBayOf(const Entry& e, GroundCmd cmd) const;
    void RefreshAirports();
    void PushBoard();

    // Getrennte Pisten
    int  RunwayGroup(const std::string& airport, const std::string& rwy, bool* unknown) const;
    bool SplitActive() const;
    // Ist an einem deiner Plaetze ein Delivery (<APT>_*DEL) online? Sonst macht Apron Delivery mit
    bool DelOnline();
    bool ApronOnline();
    bool GndOnline();
    std::map<std::string, bool> m_stdClr;   // Taxi In bis zum Stand freigegeben (".SB_SC_TRUE/FALSE")
    std::map<std::string, bool> m_lineup;
    std::map<std::string, int> m_hl;
    std::map<std::string, int> m_rwyState;  // Piste (Spaltentitel) -> 1 gelb, 2 rot
    std::map<std::string, long long> m_handedAt;   // von mir abgegeben (Takt): noch kFadeSec grau sichtbar
    static const int kFadeSec = 30;
    std::map<std::string, std::string> m_divert;   // Ausweichlandung: Callsign -> unser Platz
    bool   QualifiesDiversion(EuroScopePlugIn::CFlightPlan& fp, const std::string& cs, bool alreadyShown);
    std::string StandAtPosition(const std::string& apt, EuroScopePlugIn::CFlightPlan& fp);
    double DistToAirportNm(const std::string& apt, EuroScopePlugIn::CRadarTarget& rt);   // Line-up ueber StripBay (".SB_LU_"), wenn ES LINEUP ablehnt
    // --- Uebergabe (4.0.0): eine Regel fuer alle Wege -------------------------------------------------
    // Stellen mit StripBay und eingeschalteter Zustaendigkeit melden sich (".SB_UN_<Stelle>", ab: ".SB_UX_").
    // Nur an sie geht eine Uebergabe als Angebot, das angenommen wird; an alle anderen direkt.
    std::map<std::string, long long> m_sbUnits;
    std::string m_announced;                 // zuletzt gemeldete eigene Stelle ("" = keine)
    bool        m_announceAgain = false;     // ein Lotse ist neu da: noch einmal melden
    bool        UnitHasSb(const std::string& unit) const;
    bool        TwrOnline();
    void        Announce();
    std::string EffResp(const std::string& cs) const;                        // Zustaendigkeit, wie sie gilt
    void        HandOver(const std::string& cs, const std::string& unit);    // an die naechste (bzw. diese) Stelle
    void        GiveTo(const std::string& cs, const std::string& unit);
    void        TakeOver(const std::string& cs);
    void        CancelOffer(const std::string& cs, bool revertBay);
    std::string m_handTo;                    // HandOver -> StripMoved: an diese Stelle (statt der naechsten laut Kette)
    bool m_nextBtn = true;
    bool m_pdcOnly = true;                  // CPDLC nur fuer PDC (Logons werden abgelehnt)
    int  m_pdcSeq = 1;                      // laufende PDC-Nummer
    std::string m_aselHidden;
    std::wstring m_grpStandsStamp;          // GRpluginStands.txt des geladenen GRP: Pfad|Aenderungszeit
    void SyncGrpStands();
    std::set<std::string> m_syncWarned;     // Hinweis "Sync nicht moeglich" nur einmal je Flieger               // ins Leere getippt: dieser angewaehlte Flieger nicht markiert                  // Weiter-Taste auf den Streifen
    bool m_topDown = true;                  // Top-down: unbesetzte untere Stationen mitmachen
    int  m_sqkFnTs = 667;                   // TopSky: Squawk vergeben (wie vSID)
    int  m_grpSetFn = 7, m_grpClearFn = 4;   // GRP: Stand aus der Liste / [---] (Disassembly GRP 1.5)
    std::set<std::string> m_ioManual;
    // letzter Ground State eines Abflugs: verliert EuroScope ihn (leer), bleibt der Streifen,
    // wo er war (statt nach PENDING zu fallen), und StripBay setzt den Status wieder
    std::map<std::string, int> m_lastGs;
    std::map<std::string, int> m_gsRestore;
    std::map<std::string, long long> m_nsAt;
    // --- Verlauf je Streifen (4.1.0): wer hat wann was gesetzt. Nur lokal mitgeschrieben, aus den eigenen
    // Aktionen und aus dem, was von den anderen ankommt (Merker, Ground State, Clearance).
    std::map<std::string, std::vector<std::string>> m_hist;          // Zeilen "HHMM  Text  (you|other)"
    std::map<std::string, std::map<std::string, std::string>> m_histLast;   // je Art der letzte Wert (Wiederholungen weglassen)
    std::map<std::string, long long> m_gsByMeAt;                     // Ground State zuletzt von mir gesetzt (Takt)
    void Hist(const std::string& cs, const std::string& kind, const std::string& value, const std::string& text, int by);   // by: 1 ich, 0 anderer, -1 ohne
    void HistSync(const std::string& cs, const std::string& msg, bool mine);
    long long m_lastResync = -1;
    // Schutz gegen Nachrichten-Fluten (jede Meldung oeffnet/blinkt den EuroScope-Chat)
    int m_inCommand = 0;
    std::map<std::string, long long> m_msgSeen;
    long long m_msgWin = -1; int m_msgCount = 0;
    std::map<std::string, std::string> m_realSp;
    bool m_applyingGs = false;   // ApplyGroundState schreibt gerade Befehle ins Scratchpad   // letzter echter Scratchpad-Inhalt (ohne Sync-Nachrichten)
    long long m_lastSqAnswer = -1000;
    bool m_syncAsked = false;    // nach dem Login einmal die anderen um ihre Merker bitten (.SB_SQ_)
    bool m_resyncForce = false;
    void SetDirection(const std::string& cs, bool arrival, bool send);
    void SetDivert(const std::string& cs, const std::string& apt, bool send);
    std::string NextRespUnit(const std::string& cs);
    bool StationFreq(const std::string& kind, int g, const std::string& apt, std::string& freq, std::string& who);
    bool GrpStandCall(const std::string& cs, int fn, const std::string& item);
    std::map<std::string, std::string> m_parkPlan;   // geparkt: ADEP>ADES beim Parken (neuer Flugplan -> NSTS)
    std::set<std::string> m_parked;   // geparkt per Tipp auf STAND (im Papierkorb, zurueckholbar)
    void RefreshRunways();
    void SetRunwayDefaults();
    void SaveRunwaySplit();
    std::string RunwaySplitText() const;
    bool ParseRunwaySplit(const std::string& text);
    int  ParseRunwayFilterArg(const std::string& arg) const;
    void RunwayStatus();

    // Naechste Frequenz
    void NextFrequency(EuroScopePlugIn::CFlightPlan& fp, bool arrival, const std::string& apt, const std::string& rwy,
                       std::string& freq, std::string& who);
    bool EvalFreqChain(const std::string& chain, std::string& freq, std::string& who);
    void SetFreqDefaults();
    bool ControllerFreq(EuroScopePlugIn::CController c, std::string& freq, std::string& who);

    // Einstellungen
    void LoadSettings(WindowSettings& ws);
    void SaveWindowSettings();
    void SaveOptions();
    GroundCmd CmdForBay(int bay, bool arrival) const;
    void UpdateVacdm();
    void UpdateWx();
    std::string WxAirport() const;
    std::string FindVacdmUrl();
    void StartIcaoSearch();

    void MsgAuto(const std::string& utf8);
    void Msg(const std::string& utf8);

    std::map<std::string, Entry> m_entries;
    std::vector<std::string>     m_order[BAY_COUNT];
    std::set<std::string>        m_hidden;      // von Hand entfernte Streifen
    std::map<std::string, bool>  m_hiddenArr;   // ... und ob sie Ankuenfte waren (zum Zurueckholen)
    std::map<std::string, bool>  m_forced;      // von Hand erstellte Streifen: Callsign -> Ankunft
    bool m_restoringAll = false;                 // Recover all: keine Einzelmeldungen
    struct FreeTrash { std::string key; int bay; bool designator = false; };
    std::vector<FreeTrash>       m_trashFree;   // entfernte Notiz-Streifen
    std::vector<std::string>     m_trashOrder;  // Reihenfolge im Papierkorb (aelteste zuerst)
    std::set<std::string>        m_airports;      // Abflug-Flughaefen
    std::set<std::string>        m_arrAirports;   // Ankunfts-Flughaefen
    bool                         m_autoAirports = true;
    std::map<std::string, std::string> m_standLive;   // aktueller Stand je Callsign (juengste Quelle)
    std::map<std::string, std::string> m_standAnn;    // zuletzt gelesener Stand aus den Annotationen
    std::map<std::string, std::string> m_standSrc;    // woher m_standLive kommt (Diagnose)
    std::map<std::string, std::string> m_standSp;     // zuletzt gesehener GRP/S/... im Scratchpad
    std::map<std::string, bool>        m_ctl;         // CTL-Flag (Landefreigabe), synchron mit vSID
    std::map<std::string, bool>        m_hov;         // Handover-Flag (weggeschickt), synchron mit vSID
    std::map<std::string, std::pair<std::string, bool>> m_int;   // Intersection (Name, zugewiesen), synchron mit vSID
    std::map<std::string, bool> m_ga;                  // Durchstarten, geteilt ueber ".SB_GA_TRUE/FALSE"
    std::map<std::string, std::set<std::string>> m_actRwy[2];   // [0] Abflug, [1] Ankunft: aktive Pisten je Platz
    std::map<std::string, std::string> m_hs;          // Hold Short, geteilt ueber ".SB_HS_..." im Scratchpad
    // --- ab 4.2.0 ---
    std::map<std::string, std::string> m_taxi;        // Rollweg ("A B B3"), geteilt ueber ".SB_TX_A-B-B3"
    std::map<std::string, int>         m_deice;       // Enteisung: 1 angefragt, 2 erledigt, geteilt ueber ".SB_DI_1"
    void SetTaxi(const std::string& cs, const std::string& text);
    bool ResyncExtras(const std::string& cs);
    void SetDeice(const std::string& cs, int state);
    // Versionen: jede StripBay meldet ihre Version (".SB_VN_4X2X0"); andere Hauptversion = Warnung
    bool        m_verSent = false;
    std::string m_verWarn;                   // Text fuer das Fenster (leer = alles gleich)
    std::set<std::string> m_verTold;         // schon gemeldete Versionen
    void VersionSeen(const std::string& wire);
    // Update-Hinweis (GitHub-Releases), einmal nach dem Start
    UpdateChecker m_upd;
    bool        m_updCheck = true, m_updStarted = false, m_updAsked = false;
    std::string m_updAvail;
    void ProcessUpdate();
    // Diagnose (.strips diag): letzte Meldungen und Zustand in eine Textdatei
    std::deque<std::string> m_recentMsgs;
    bool WriteDiag(std::wstring& fileOut);

    GroundCmd m_runwayCmd = GC_LINEUP;   // Status, wenn ein Abflug auf die Runway gezogen wird
    std::string m_freqDep = "{APT}_*DEP,{APT}_*APP";   // Ersatz, wenn EuroScope keinen naechsten Lotsen kennt
    std::string m_freqArr = "{APT}_*GND";
    // Frequenzketten fuer den geteilten Platz (Berlin), erste passende gewinnt:
    //   MUSTER=FREQ  Lotse mit passendem Rufzeichen online -> FREQ
    //   FREQ         jemand ist auf dieser Frequenz online
    //   FREQ!        immer (z. B. 122.800 UNICOM als Ende der Kette)
    std::string m_chainDep[3];   // [RWY_NONE] Piste unbekannt, [RWY_A] Nord, [RWY_B] Sued
    std::string m_chainArr[3];   // [RWY_NONE] Piste unbekannt, [RWY_A] Nord, [RWY_B] Sued
    std::string m_freqStand;     // Ankuenfte nach Parkposition, z. B. "W1,W2,W3,W4=121.705" (vor den Ketten)
    std::string m_freqDel;       // Delivery: Abfluege nach Stand, "MUSTER,...=KETTE; ..." (erste passende Gruppe)
    std::string m_hsList;
    std::string m_gndStands = "W*";
    std::string m_intNorth, m_intSouth;   // Intersections je Piste (Tasten im Intersection-Feld)  // Stands ohne Apron (GAT, Apron 1, W): Startup bei Ground
    // Uebergaben fuer alle synchron (Scratchpad ".SB_XF_..." bzw. ".SB_AP_..."):
    //  Abfluege: "TWR" = an Tower, "GND" = an Ground (gilt, solange der Ground State TAXI ist)
    //  Ankuenfte: "RWY" = auf der Piste, "REL" = an Ground abgegeben, "FIN" = zurueck im Anflug
    std::map<std::string, std::string> m_xfer;
    std::map<std::string, std::string> m_arrPos;
    void SendXfer(const std::string& cs, const std::string& v);
    void SendArrPos(const std::string& cs, const std::string& v);        // Schnellauswahl Hold Short (dazu kommen die Pisten des Platzes)
    std::map<std::string, std::string> m_freqOverride;   // von Hand gesetzte naechste Frequenz je Callsign
    int       m_transitionAlt = 5000;
    double    m_captureNm = 5.0;     // so nah am Platz muss ein Flieger sein, damit ein Streifen entsteht
    double    m_removeNm = 20.0;     // Sicherheitsgrenze: nicht weggeschickte Abfluege verschwinden ab hier
    static constexpr double kGoneNm = 20.0;
    static constexpr const char* kArrNorth = "129.605 > 129.505";   // Apron Nord, sonst Ground Nord
    static constexpr const char* kArrSouth = "121.855 > EDDB_A_GND > 121.705";   // Main Apron, sonst Ground Sued
    void      MigrateFreqChains();
    // Platz-Profile (plug and play): Berlin, Leipzig, Hannover, sonst allgemein.
    // Gewaehlt nach eigenem Callsign, sonst nach dem aktiven Platz.
    std::string m_profileIcao = "EDDB";   // aktuelles Profil
    std::string m_profileForced;          // .strips profile EDDP (leer = automatisch)
    bool      m_hasApron = true;          // Platz hat Apron-Positionen
    int       m_autoHandoffFt = 0;        // >0: Abflug verschwindet ueber dieser Hoehe (kein Wegschicken)
    std::vector<std::string> m_fixedRwys; // Pisten ohne Sektordaten
    bool      m_splitGnd = true;          // Ground getrennt Nord/Sued (nur Berlin)
    std::string m_chainTwr[3];            // Tower je Bahn (Ground/Delivery -> Tower)
    std::string m_freqSid;                // Abflug nach SID
    int       MyRole();
    bool      EvalSidFreq(const std::string& sid, std::string& freq, std::string& who);
    void      ChooseProfile();
    void      ApplyProfile(const std::string& icao);
    void      LoadProfileOptions();
    std::string PKey(const char* base) const;
    // Betriebsrichtungswechsel: Abfluege vor dem Rollen auf inaktiver Piste -> neue Piste
    std::vector<std::pair<std::string, std::string>> RunwaySwitchTargets();
    // Nachzuegler-Sync: kommt ein Lotse am Platz online, eigene Merker einmal neu verteilen
    std::set<std::string> m_knownCtrls;
    bool      m_ctrlScanned = false;
    long long m_resyncAt = -1;
    void      ScanControllers();
    int       Resync();   // weiter weg: Flieger komplett weg (auch aus Recover)
    bool      FarAway(EuroScopePlugIn::CFlightPlan& fp);
    void      ForgetFlight(const std::string& cs);
    // Zustaendigkeit je Flieger (".SB_RS_<UNIT>", wie vSID ueber das Scratchpad an alle):
    // DEL, APN/APNN/APNS, GND/GNDN/GNDS, TWR/TWRN/TWRS
    std::map<std::string, std::string> m_resp;
    std::map<std::string, bool> m_onFreq;     // On Freq ueber StripBay (".SB_OF_TRUE"), wenn ES ONFREQ nicht kennt
    std::string m_dlLastStatus;
    bool m_clrViaDl = false;                  // Freigabe kommt gerade per CPDLC (WILCO): kein ON FREQ
    std::string MyUnit() const;
    std::string SideOf(const std::string& cs) const;
    void        SetResp(const std::string& cs, const std::string& unit);
    void        SetRespOn(bool on);
    // Uebergabe in zwei Schritten: anbieten (".SB_HO_<UNIT>"), die andere Stelle nimmt an
    void        OfferResp(const std::string& cs, const std::string& unit);
    void        AcceptOffer(const std::string& cs);
    bool        UnitIsMine(const std::string& u);
    bool        ApronSideOnline(char side);   // Berlin: 'N' Apron Nord (129.605 / EDDB_E_GND), 'S' Main Apron (121.855 / EDDB_A_GND)
    std::string DlLowerStation();     // CPDLC: niedrigere Station am Platz online (die verbindet), sonst leer
    std::string m_dlBlockedBy;
    std::map<std::string, std::string> m_offer;
    std::set<std::string> m_pinned;
    std::map<std::string, std::string> m_pushDir;
    std::map<std::string, int> m_colWidths;
    // eigene Plaetze und Einstellungs-Export (Dateien neben der StripBay.dll)
    std::wstring m_pluginDir;            // nur fuer Tests (leer = Ordner der DLL)
    // Protokoll (.strips log on): StripBay.log neben der DLL, die letzten 5000 Zeilen
    bool m_logOn = false, m_logDirty = false;
    std::deque<std::string> m_log;
    long long m_logFlushed = 0;
    void Log(const std::string& line);
    void FlushLog();
    std::wstring PluginDir() const;
    int  LoadAirportFile(bool verbose);
    bool ExportSettings(const std::wstring& file, bool withCode);
    int  ImportSettings(const std::wstring& file);
    bool m_tsatSort = true;
    // Tower: Zeitstrahl wie die DFS-Planungsanzeige (WFF TWR-HMI), Fenster in Minuten
    bool m_timeline = false;
    int  m_tlPast = 3, m_tlFuture = 6;
    bool m_tlDfs = true, m_tlNight = false;
    bool m_tlEst = true;   // Zeitstrahl: Abfluege ohne TTOT/Slot automatisch gestaffelt einreihen (Schaetzung)
    std::map<std::string, long long> m_tlPlan;   // Zeitstrahl: per Ziehen geplante Bahnzeit (synchron .SB_TP_)
    // Rueckgaengig: die letzten Aktionen (Verschieben, Zustaendigkeit, Angebot, Entfernen)
    struct UndoRec { int kind = 0; std::string cs; int bay = -1, index = -1; std::string resp; };
    enum { UNDO_MOVE = 1, UNDO_RESP, UNDO_OFFER, UNDO_REMOVE };
    std::vector<UndoRec> m_undo;
    bool m_undoing = false;
    void PushUndo(int kind, const std::string& cs);
    void Undo();   // Spaltenbreiten je Ansicht und Platz (%), gespeichert   // Push-Richtung (Nase) N/E/S/W, synchron (.SB_PB_)
    std::set<std::string> m_offerByMe;
    std::map<std::string, long long> m_offerAt;   // seit wann angeboten (Takt)
    static const int kRejectShowSec = 30;
    bool m_respOn = true;    // Zustaendigkeit / Uebergaben; aus = StripBay traegt niemanden als zustaendig ein
    int m_offerBlink = 30;   // Sekunden, bis ein Angebot an mich blinkt (vorher nur farbig), 0 = sofort
    static bool IsRespUnit(const std::string& u);
    friend struct RespOnExit;
    double    m_arrivalNm = 15.0;    // Ankuenfte erscheinen ab dieser Entfernung im Final

    std::vector<AnchorScreen*> m_screens;
    // TSAT: CDM-Plugin ueber Annotation, vACDM ueber den Server
    VacdmClient                m_vacdm;
    bool                       m_vacdmEnabled = true;
    std::string                m_cdmUrl;      // fest eingestellte vACDM-URL, leer = automatisch
    std::string                m_cdmUrlFound; // zuletzt gefundene URL (Statusanzeige)

    // Airline-Rufzeichen und Flughafennamen aus dem Sektorpaket
    IcaoLoader                 m_icaoLoader;
    IcaoTables                 m_icao;
    bool                       m_icaoDone = false;
    bool                       m_icaoReport = false;   // Ergebnis im ES-Chat melden (nach .strips icao)
    std::wstring               m_icaoDir;              // fest eingestellter Ordner, leer = suchen

    // Getrennte Pisten (Standard: EDDB, Nord 06L/24R, Sued 06R/24L, alte Namen 07/25 auch)
    struct RunwaySplit
    {
        bool                  enabled = true;
        std::string           airport;
        std::string           name[3];     // [RWY_A], [RWY_B], ANSI
        std::set<std::string> rwys[3];
        std::string           active[3];   // gerade aktive Pisten der Gruppe (Runway-Dialog)
    };
    RunwaySplit                m_split;

    // METAR und ATIS-Buchstabe fuer die obere Leiste
    WxClient                   m_wx;

    // CPDLC / DCL ueber Hoppie
    struct DlMsg { long long t; bool up; std::string text; int min; int mrn; std::string ra; std::string status; };
    struct DlState
    {
        int logon = 0;            // 0 nein, 1 angefragt, 2 angemeldet
        int logonMin = -1;
        int dcl = 0;              // 0 nichts, 1 angefragt, 2 gesendet, 3 WILCO, 4 UNABLE
        int dclMin = -1;
        std::string dclRequest;
        int unread = 0;
        int pendingMin = -1;      // Pilotennachricht, die eine Antwort braucht
        std::string pendingText;
        std::vector<DlMsg> log;
    };
    HoppieClient               m_hoppie;
    std::map<std::string, DlState> m_dl;
    bool        m_dlEnabled = false;
    std::string m_dlLogon;        // geheimer Logon-Code
    std::string m_dlStation;      // eigene Station, leer = Platz (z. B. EDDB)
    bool        m_dlAutoLogon = true;
    bool        m_dlSound = true;
    std::map<std::string, std::string> m_sounds = { { "request", "triple" }, { "wilco", "single" },
                                                    { "unable", "low" }, { "offer", "chime" } };
    void        PlayEventSound(const std::string& ev);
    int         m_offerRemind = 60;                  // Sekunden bis zur Erinnerung (Ton) an ein Angebot, 0 = aus; immer nach dem Blinken
    std::set<std::string> m_offerReminded;
    std::map<std::string, long long> m_rejectedAt;   // mein Angebot wurde abgelehnt (Takt)
    std::map<std::string, std::string> m_rejectedBy;
    void        RejectOffer(const std::string& cs);
    int         m_dlMin = 0;
    void UpdateDatalink();
    void DlProcess();
    std::string DlStation() const;
    int  DlSend(const std::string& cs, const std::string& ra, const std::string& text, int mrn, bool telex = false);
    void DlLog(const std::string& cs, bool up, const std::string& text, int min, int mrn, const std::string& ra);
    std::string DlDclText(const StripData& s, std::string* error);
    std::string DlUnitName(const std::string& freq, const std::string& who) const;
    bool                       m_wxEnabled = true;
    std::map<std::string, std::string> m_metarEs;   // METAR, wie EuroScope ihn liefert

    StripWindow                m_window;
    // StripBay Remote: weiteres Windows-Geraet im Netz (Server hier im Plugin)
    remote::Hub     m_hub;
    RemoteServerNet m_net;
    bool        m_remoteOn = false;
    int         m_remotePort = remote::kDefaultPort;
    std::string m_remoteCode;
    std::string m_remoteAddr;
    void        RemoteSetup();
    bool        RemoteStart();
    void        RemoteStop();
    void        RemoteStatus();
    int                        m_lastView = -1;
    int                        m_lastRwyFilter = -1;
};
