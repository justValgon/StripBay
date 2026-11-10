// StripBay - Datenmodell, unabhaengig von EuroScope.
// Das Fenster kennt nur diese Strukturen, das Plugin fuellt sie.
#pragma once

#include <cctype>
#include <map>
#include <string>
#include <vector>

// Bays = Zustaende im Plugin. Welche Bays eine Ansicht als Spalte zeigt
// (auch mehrere zusammen in einer Spalte), legt das Fenster fest.
enum Bay
{
    BAY_PENDING = 0,
    BAY_ONFREQ,
    BAY_STARTUP,
    BAY_PUSHBACK,
    BAY_TAXI,        // Taxi Out
    BAY_TOGND,       // vom Apron an Ground uebergeben (nur lokal, Status bleibt TAXI)
    BAY_HANDOFF,     // vom Ground an Tower uebergeben (nur lokal, Status bleibt)
    BAY_RUNWAY,      // auf der Piste: Abfluege LINEUP/DEPA, Ankuenfte mit Landefreigabe
                     // oder vom Tower auf die Piste gelegt. Eine gemeinsame Reihenfolge.
    BAY_FINAL,       // Ankuenfte in der Luft
    BAY_GNDIN,       // Ankuenfte am Boden bei Ground (gelandet, noch kein TXIN)
    BAY_TAXIIN,      // Ankuenfte am Boden bei Apron (Ground State TXIN = von Ground an Apron uebergeben)
    BAY_COUNT
};

const int BAY_NONE = -1;   // Status passt zu keiner Bay, Streifen bleibt wo er ist
const int BAY_GONE = -2;   // Streifen verschwindet (Ankunft geparkt)

// Ansichten
enum View
{
    VIEW_ALL = 0,
    VIEW_DEL,
    VIEW_GND,
    VIEW_TWR,
    VIEW_APN,        // Apron (Vorfeld): Startup, Pushback, Taxi, An Ground, Taxi In
    VIEW_COUNT
};

// Ground States, wie EuroScope sie kennt (TopSky und GRP lesen dieselben Werte)
enum GroundCmd
{
    GC_UNKNOWN = -1,
    GC_NSTS = 0,    // kein Status
    GC_ONFREQ,
    GC_STUP,
    GC_PUSH,
    GC_TAXI,
    GC_LINEUP,
    GC_DEPA,
    GC_TXIN,        // Taxi In (Ankunft)
    GC_PARK,        // geparkt (Ankunft fertig)
    GC_COUNT
};

// In welche Bay gehoert ein Abflug mit diesem Status?
// Line-up und Startfreigabe liegen beide in der Runway-Bay.
inline int DepartureBay(int cmd)
{
    switch (cmd)
    {
    case GC_NSTS:   return BAY_PENDING;
    case GC_ONFREQ: return BAY_ONFREQ;
    case GC_STUP:   return BAY_STARTUP;
    case GC_PUSH:   return BAY_PUSHBACK;
    case GC_TAXI:   return BAY_TAXI;
    case GC_LINEUP: return BAY_RUNWAY;
    case GC_DEPA:   return BAY_RUNWAY;
    default:        return BAY_NONE;
    }
}

// ... und eine Ankunft? In der Luft = Final, auf der Piste (Landefreigabe CTL
// oder vom Tower dorthin gelegt) = Runway, am Boden = Taxi In, geparkt =
// Streifen weg. keepLanded: auf der Piste auch nach der Landung bleiben, bis
// der Tower den Streifen herauszieht.
inline int ArrivalBay(int cmd, bool onGround, bool onRunway, bool keepLanded)
{
    if (cmd == GC_PARK) return BAY_GONE;
    if (cmd == GC_TXIN) return BAY_TAXIIN;   // an Apron uebergeben
    if (onRunway && (!onGround || keepLanded)) return BAY_RUNWAY;
    if (onGround) return BAY_GNDIN;
    return BAY_FINAL;
}

inline bool IsArrivalBay(int bay)
{
    return bay == BAY_FINAL || bay == BAY_GNDIN || bay == BAY_TAXIIN;
}

// Runway nimmt beide, Final und Taxi In nur Ankuenfte, der Rest nur Abfluege
inline bool CanDrop(bool arrival, int bay)
{
    if (bay < 0 || bay >= BAY_COUNT) return false;
    if (bay == BAY_RUNWAY) return true;
    return arrival == IsArrivalBay(bay);
}

// Pistengruppen. RWY_NONE = keiner Gruppe zugeordnet (anderer Platz oder
// Piste unbekannt), solche Streifen erscheinen bei beiden Pisten.
const int RWY_NONE = 0;
const int RWY_A = 1;   // Berlin: Nord
const int RWY_B = 2;   // Berlin: Sued

// Eine Zeile im CPDLC-Verlauf
struct DlLine
{
    std::string time;    // "1432Z"
    bool        up = false;   // true = von dir an den Piloten
    std::string text;
    std::string status;  // Antwort des Piloten (WILCO, UNABLE ...)
};

// Aktionen im CPDLC-Fenster
enum DlAction
{
    DL_ACCEPT_LOGON, DL_REJECT_LOGON, DL_SEND_DCL, DL_DCL_VOICE, DL_CONTACT, DL_MONITOR, DL_FREE,
    DL_STANDBY, DL_UNABLE, DL_ROGER, DL_AFFIRM, DL_NEGATIVE, DL_LOGOFF, DL_READ, DL_COUNT
};

struct StripData
{
    std::string callsign;
    std::string acType;          // ICAO-Typ, z. B. A20N
    std::string wtc;             // L, M, H, J
    std::string rules;           // I, V, Y, Z
    std::string adep, ades, altn;
    std::string sid, rwy;
    std::string squawkAssigned;  // vom Lotsen vergeben
    std::string squawkCurrent;   // was der Transponder sendet
    std::string scratch;
    std::string eobt;            // HHMM
    std::string tsat;            // HHMM, leer wenn keine
    long long   tsatUtc = 0;     // Sekunden seit 1970 (UTC), 0 = keine TSAT
    std::string tsatSource;      // "CDM" oder "vACDM"
    std::string ctot;            // HHMM, leer wenn keine (CTOT, Slot)
    std::string ttot;            // HHMM, leer wenn keine (geplante Startzeit)
    std::string ctotSource;      // "CDM" oder "vACDM"
    std::string route, remarks;
    std::string groundStateRaw;  // genau so, wie EuroScope ihn liefert
    std::string comm;            // V, R, T oder leer
    std::string spoken;          // ausgeschriebenes Rufzeichen, z. B. "LUFTHANSA 4AB"
    std::string adesName;        // Zielflughafen ausgeschrieben, z. B. "London/Heathrow"
    std::string adepName;        // Abflugflughafen ausgeschrieben

    // Ankuenfte
    bool        arrival = false;
    std::string stand;           // Parkposition aus dem Ground Radar Plugin
    std::string star;
    std::string arrRwy;
    bool        onGround = false;      // gelandet (Entfernung, Geschwindigkeit, Vertikalrate)
    double      distDest = 0;          // NM bis zum Zielplatz
    bool        landingCleared = false;// Landefreigabe (CTL-Flag, synchron mit vSID)
    // CPDLC / DCL (Hoppie)
    int         dlLogon = 0;           // 0 nein, 1 Logon angefragt, 2 angemeldet
    int         dlDcl = 0;             // 0 nichts, 1 angefragt, 2 gesendet, 3 bestaetigt (WILCO), 4 abgelehnt
    int         dlUnread = 0;          // ungelesene Nachrichten
    bool        dlPending = false;     // Pilotennachricht wartet auf Antwort
    std::string dlPendingText;
    std::string dlRequest;             // DCL-Anfrage des Piloten
    std::string dlDclText;             // DCL, wie er gesendet wuerde (oder "!Grund", wenn etwas fehlt)
    std::string dlContactText;         // "CONTACT ..." mit der naechsten Frequenz (leer = keine)
    std::vector<DlLine> dlLog;         // letzte Nachrichten, aelteste zuerst
    bool        goAround = false;      // Durchstarten (geteilt ueber ".SB_GA_..."), endet mit neuer CTL oder Landung
    long long   ctotUtc = 0;           // CTOT als Zeitpunkt (fuer das Slot-Fenster), 0 = keine
    bool        standConflict = false; // Stand doppelt belegt (zweiter Flieger mit demselben Stand am Platz)
    std::string standConflictWith;     // Rufzeichen des anderen Fliegers
    bool        rwyInactive = false;   // Piste im Tag ist fuer diese Richtung nicht aktiv (Runway-Dialog)

    int clearedAlt = 0;          // Initial / CFL in ft, 0 = nicht gesetzt
    int clearedApproach = 0;     // CFL-Sonderwert: 1 = ILS-Anflug frei, 2 = Sichtanflug frei
    int finalAlt = 0;            // RFL in ft
    int assignedHeading = 0;
    int assignedSpeed = 0;
    int groundSpeed = 0;         // kt
    int altitude = 0;            // ft (Druckhoehe)
    std::string intersection;    // Abflug: Intersection aus vSID, z. B. "L6" (leer = volle Laenge / keine)
    bool intAssigned = false;    // true = zugewiesen, false = nur "able for"
    bool airborne = false;       // Abflug ist in der Luft
    bool gndStand = false;       // Stand ohne Apron (GAT, Apron 1, W-Stands): Startup und Taxi direkt bei Ground
    bool hovDue = false;         // in der Luft und mindestens 2000 ft ueber dem Platz: Wegschicken hervorheben
    bool handedOver = false;     // Abflug weggeschickt (vSID-Handover-Flag HOV)
    std::string holdShort;       // Hold Short, z. B. "V1" oder "24R" (leer = keins)
    bool toGround = false;       // vom Apron an Ground uebergeben (nur lokal)
    bool freeStrip = false;      // Notiz-Streifen ohne Flugplan (z. B. Fahrzeug, Pistenkontrolle), nur lokal
    bool designator = false;
    long long planUtc = 0;       // Zeitstrahl: per Ziehen geplante Bahnzeit (Reihenfolge), synchron
    std::string pushDir;         // Push-Richtung (Nase): N, E, S, W - synchron     // Designator-Strip (Trennstreifen in einer Bay, z. B. RUNWAY OBSTRUCTED), nur lokal
    std::string nextFreq;        // naechste Frequenz, z. B. "119.625", leer = keine bekannt
    std::string nextCtrl;        // wer dort sitzt, z. B. "EDDB_N_APP"

    // Getrennte Pisten (z. B. EDDB Nord/Sued). Massgeblich ist die Piste im Tag:
    // bei Abfluegen die Departure-Runway, bei Ankuenften die Arrival-Runway.
    int  rwyGroup = RWY_NONE;    // RWY_NONE, RWY_A oder RWY_B
    bool rwyUnknown = false;     // Flieger am geteilten Platz, Piste passt zu keiner Gruppe

    GroundCmd state = GC_NSTS;   // normalisierter Ground State
    bool clearance = false;      // Clearance-Flag aus EuroScope
    bool selected = false;       // in EuroScope angewaehlt (ASEL)
    bool localOnly = false;      // EuroScope hat den Status nicht uebernommen
    bool handedOff = false;      // vom Ground an Tower uebergeben (nur lokal)
    bool xpdrOff = false;        // auf der Piste (Line-up/Takeoff) ohne Mode C
    int  standBusy = 0;          // Ankunft: zugewiesener Stand laut GRP-Daten 2 = besetzt, 3 = gesperrt (BLOCKS)
    std::string standBusyBy;     // wer dort steht bzw. welcher Nachbar sperrt
    bool standMissing = false;   // Ankunft am Boden ohne Stand
    bool atGate = false;         // Ankunft am Gate angekommen (< 3 kt, im Standkreis): "geparkt" moeglich
    std::string vsidErr;         // Pruefung wie vSID: "MANUAL" (keine SID), "EQUIP" (kein RNAV), "RWY" (SID passt nicht zur Piste)
    std::string vsidErrText;     // dasselbe in Worten (Details)
    std::string resp;            // Zustaendigkeit (synchron): DEL, APN, APNN, APNS, GND(N/S), TWR(N/S); leer = keine
    bool respMine = false;       // die Zustaendigkeit ist deine Stelle
    bool stdClr = false;
    bool heli = false;           // Hubschrauber (Typ oder Helipad als Piste)
    bool diverted = false;       // von Hand als Diversion eingetragen
    bool trackedOther = false;   // von einem anderen Lotsen assumed (z. B. Anflug)
    int  hl = 0;                 // farblich hervorgehoben: 0 nein, 1 gelb, 2 gruen, 3 blau, 4 rot, 5 lila (synchron)
    std::string divertFrom;      // Ausweichlandung: urspruengliches Ziel (ades ist dann unser Platz)
    bool pinned = false;         // von Hand erstellt: immer zu sehen (in der passenden Spalte der Ansicht)
    // --- Uebergabe ---
    std::string nextResp;        // naechste Stelle (Taste "Hand over -> GND"), leer = keine
    std::string offerTo;         // Uebergabe angeboten an diese Stelle (wartet auf Annahme)
    bool offerForMe = false;     // an mich angeboten: hervorgehoben, Feld oben rechts = annehmen
    bool offerBlink = false;     // an mich angeboten und noch nicht angenommen: blinkt
    std::string offerRejectedBy; // mein Angebot wurde abgelehnt (von dieser Stelle): rot, 30 s
    bool fading = false;         // gerade abgegeben: noch grau zu sehen, verschwindet bald
};

// Editierbare Felder im Detailmenue (SetField / FieldOptions)
enum Field
{
    F_SID = 0, F_RWY, F_SQUAWK, F_CFL, F_RFL, F_RULES, F_TYPE, F_ALTN, F_ADES, F_ADEP, F_STAND, F_STAR, F_COMM,
    F_HDG, F_SPD, F_EOBT, F_ROUTE, F_REMARKS, F_FREQ, F_STATE, F_HS, F_RESP,
    F_HISTORY,   // Verlauf des Streifens (nur lesen): FieldOptions liefert die Zeilen, neueste zuerst
    F_COUNT
};

// Eintrag im Papierkorb (entfernte oder weggeschickte Streifen)
struct TrashItem
{
    std::string id;      // fuer RestoreStrip
    std::string label;   // Callsign bzw. Notiztext
    std::string kind;    // "entfernt", "weggeschickt", "Notiz"
    bool arrival = false;   // Ankunft (Papierfarbe)
};

struct Board
{
    std::vector<StripData> bays[BAY_COUNT];
    std::string airports;        // Anzeige oben links, z. B. "EDDF EDDS"
    int transitionAltitude = 5000;
    bool connected = true;
    bool icaoLoaded = false;     // ICAO_Airlines/Airports gefunden

    // Getrennte Pisten: nur aktiv, wenn der geteilte Platz einer deiner Plaetze ist
    bool rwySplit = false;
    // Delivery am Platz online? Sonst zeigt die Apron-Ansicht die Delivery-Spalten mit
    bool delOnline = true;
    // Apron am Platz online (129.605 / 121.855)? Sonst zeigt Ground auch Startup, Pushback und Taxi In bis zum Stand
    bool apnOnline = true;
    bool gndOnline = true;       // Ground am Platz online (jemand anderes)
    bool topDown = true;
    std::string vacdmError;
    std::map<std::string, int> rwyState;   // Piste (Spaltentitel) -> 0 frei, 1 gelb, 2 rot      // vACDM meldet einen Fehler (Verbindung, Antwort): rot markieren         // Top-down: unbesetzte untere Stationen in der eigenen Ansicht mitmachen
    // Platz-Profil: Apron vorhanden (sonst keine Apron-Ansicht), automatische Uebergabe (Hannover)
    bool hasApron = true;
    bool splitGnd = true;        // Ground/Apron Nord/Sued getrennt (Berlin); sonst gilt der Bereich nur beim Tower
    std::string profile;
    bool profileForced = false;
    int autoHandoffFt = 0;

    // Einstellungen fuer das Einstellungsfenster (vom Plugin)
    struct Options
    {
        bool splitEnabled = true, autoVacate = true, wx = true, vacdm = true;
        bool dlOn = false, dlCode = false, dlAutoLogon = true, dlSound = true;
        bool takeoffDepa = false;
        bool nextBtn = true;      // Weiter-Taste auf den Streifen
        bool tsatSort = true;     // Pending / On Freq nach TSAT sortieren, Startup-Vorschlag (blinkt im TSAT-Fenster)
        bool timeline = false;    // Tower: Zeitstrahl statt Holding/Runway/Final-Spalten (DFS-Planungsanzeige)
        int  tlPast = 3, tlFuture = 6;   // Zeitstrahl: Minuten Vergangenheit / Zukunft
        bool tlDfs = true;        // Zeitstrahl im DFS-Farbschema (dunkelgrau / schwarz), sonst wie das Fenster
        bool tlNight = false;     // DFS-Schema: Nacht (gedimmt)
        bool tlEst = true;        // Abfluege ohne TTOT/Slot: geschaetzte, gestaffelte Zeit (EST)
        int offerBlink = 30;      // Angebot an mich: blinkt nach so vielen Sekunden
        int offerRemind = 60;     // ... und Ton nach so vielen Sekunden (0 = aus)
        int  arrNm = 15, transitionAlt = 5000;
        std::string dlStation, gndStands, hsList, intNorth, intSouth;
        std::map<std::string, int> colWidths;   // "Ansicht#Platz" -> Breite in % des Standards (Ziehen am Bay-Rand)
        // --- ab 3.1.0: neue Felder nur hier unten anfuegen. Wire.h liest sie optional (TAIL in genwire.py),
        // damit StripBay Remote 3.0 und 3.1 weiter zusammenpassen.
        bool respOn = true;       // Zustaendigkeit / Uebergaben; aus = niemand wird als zustaendig eingetragen
    } opt;

    // CPDLC (Hoppie)
    bool dlEnabled = false;
    bool dlConnected = false;
    std::string dlStation;

    // Wetter in der oberen Leiste
    std::string wxAirport;       // Platz, dessen METAR/ATIS angezeigt wird, z. B. "EDDB"
    std::string metar;           // aktueller METAR (leer = keiner)
    std::string atis;            // ATIS-Buchstabe(n), z. B. "B" oder "D C  A D"

    std::vector<TrashItem> trash;   // Papierkorb, neueste zuerst
    std::string rwyName[3];      // [RWY_A], [RWY_B]: z. B. "NORD", "SUED" (ANSI)
    std::string rwyActive[3];    // aktive Pisten der Gruppe, z. B. "24R" (leer = keine aktiv)
    // Betriebsrichtung gewechselt: so viele Abfluege (noch nicht rollend) stehen auf einer
    // nicht mehr aktiven Piste und lassen sich umstellen, z. B. "06L 06R"
    std::string undoLabel;       // letzte rueckgaengig machbare Aktion, z. B. "DLH1 move" (leer = keine)
    int rwySwitchCount = 0;
    std::string rwySwitchTo;

    // Remote (weiteres Windows-Geraet im Netz): Ansicht und Bereich bestimmen, wer du bist, und gelten
    // deshalb fuer alle Fenster dieser Sitzung; Zoom, Layout, Hell/Dunkel bleiben je Geraet.
    int view = 0, rwyFilter = 0;
    bool remoteOn = false;       // Server im Plugin laeuft
    int remoteClients = 0;       // verbundene Geraete
    int remotePort = 0;
    std::string remoteCode;      // Kopplungscode (6 Ziffern)
    std::string remoteAddr;      // Adresse(n) dieses PCs im Netz, z. B. "192.168.1.20"
};
// Neues Feld in StripData, Board oder Options? Auch in Wire.h (Remote) eintragen - der Test prueft das.

// Rueckkanal vom Fenster zum Plugin. Alle Aufrufe kommen aus dem
// EuroScope-Hauptthread, das Plugin darf also direkt die ES-API nutzen.
class IStripHost
{
public:
    virtual ~IStripHost() {}
    virtual void StripMoved(const std::string& callsign, int bay, int index) = 0;
    virtual void StripTapped(const std::string& callsign) = 0;
    virtual void SetGroundState(const std::string& callsign, int groundCmd) = 0;
    virtual void ToggleClearance(const std::string& callsign) = 0;
    // Runway-Bay: Startfreigabe (Abflug, LINEUP <-> DEPA) bzw. Landefreigabe (Ankunft, CTL)
    virtual void ToggleRunwayClearance(const std::string& callsign) = 0;
    // Durchstarten: Landefreigabe weg, zurueck nach Final, Marke G/A (nochmal = zuruecknehmen)
    virtual void GoAround(const std::string& callsign) = 0;
    // Befehl wie in der EuroScope-Kommandozeile ausfuehren (".strips ..."), z. B. aus dem Einstellungsfenster
    virtual void Command(const std::string& line) = 0;
    // Notiz-Streifen in eine Pistenspalte gelegt: gehoert dann zu dieser Piste
    virtual void SetStripGroup(const std::string& callsign, int group) = 0;
    // CPDLC/DCL (Hoppie): Aktion aus DlAction, text fuer Freitext
    virtual void Datalink(const std::string& callsign, int action, const std::string& text) = 0;
    // Intersection setzen (leer = keine), synchron ueber vSID
    virtual void SetIntersection(const std::string& callsign, const std::string& text, bool assigned) = 0;
    // Abflug weggeschickt (vSID-Handover-Flag), danach verschwindet er in der Luft
    virtual void ToggleHandover(const std::string& callsign) = 0;
    // Hold Short setzen (leer = loeschen), geteilt mit anderen StripBay-Nutzern
    virtual void SetHoldShort(const std::string& callsign, const std::string& text) = 0;
    // Streifen erstellen: Callsign mit Flugplan -> echter Streifen, sonst (oder asNote) Notiz-Streifen in bay
    virtual void CreateStrip(const std::string& text, bool asNote, int bay) = 0;
    // Streifen aus dem Papierkorb zurueckholen ("*" = alle)
    virtual void RestoreStrip(const std::string& id) = 0;
    // Detailmenue: Auswahlliste fuer ein Feld (leer = freie Eingabe) und Wert setzen
    virtual std::vector<std::string> FieldOptions(const std::string& callsign, int field) = 0;
    // Stands des Platzes: 0 frei, 1 vergeben (anderer Flieger), 2 besetzt, 3 gesperrt (BLOCKS)
    virtual std::map<std::string, int> StandStatus(const std::string& callsign) = 0;
    virtual void SetField(const std::string& callsign, int field, const std::string& value) = 0;
    virtual void SetScratchpad(const std::string& callsign, const std::string& text) = 0;
    virtual void RemoveStrip(const std::string& callsign) = 0;
    virtual void WindowStateChanged() = 0;
};

// Intersections: welche Liste (erste/zweite Piste)? Ohne Pistenaufteilung (Hannover) nach der Piste.
inline int IntersectionGroup(const std::string& profile, const std::string& rwy, int grp)
{
    if (grp == RWY_A || grp == RWY_B) return grp;
    if (profile == "EDDV")
    {
        std::string r;
        for (char c : rwy) if (isalnum((unsigned char)c)) r += (char)toupper((unsigned char)c);
        if (r == "09L" || r == "27R") return RWY_A;   // Nordbahn
        if (r == "09R" || r == "27L") return RWY_B;   // Suedbahn
    }
    return grp;
}
