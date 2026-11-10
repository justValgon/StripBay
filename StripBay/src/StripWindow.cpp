// StripBay - Touch-Fenster: Layout, Zeichnen, Drag and Drop, Detailansicht.
#include "StripWindow.h"
#include "Ink.h"

#include <algorithm>
#include <map>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <cwctype>
#include <ctime>
#include <set>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "msimg32.lib")

extern "C" IMAGE_DOS_HEADER __ImageBase;

// ---------------------------------------------------------------------------
// Pointer- und DPI-Konstanten (falls das SDK sie nicht kennt)
// ---------------------------------------------------------------------------
#ifndef WM_POINTERUPDATE
#define WM_POINTERUPDATE          0x0245
#define WM_POINTERDOWN            0x0246
#define WM_POINTERUP              0x0247
#define WM_POINTERACTIVATE        0x024B
#define WM_POINTERCAPTURECHANGED  0x024C
#endif
#ifndef PA_NOACTIVATE
#define PA_NOACTIVATE 3
#endif
#ifndef WM_RBUTTONDOWN
#define WM_RBUTTONDOWN 0x0204
#define WM_RBUTTONUP   0x0205
#endif
#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif
#ifndef GET_X_LPARAM
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#endif

static const WORD   kPointerFlagPrimary = 0x2000;      // POINTER_MESSAGE_FLAG_PRIMARY (im HIWORD von wParam)
static const DWORD  kPointerTypeMouse   = 4;           // PT_MOUSE
static const wchar_t* kClassName = L"StripBayWindow";
static const UINT_PTR kClockTimer = 1;
static const UINT_PTR kInkTimer = 2;     // Handschrift: nach einer kurzen Pause erkennen

typedef BOOL   (WINAPI* PFN_GetPointerType)(UINT32, DWORD*);
typedef UINT   (WINAPI* PFN_GetDpiForWindow)(HWND);
typedef HANDLE (WINAPI* PFN_SetThreadDpiAwarenessContext)(HANDLE);

static PFN_GetPointerType               s_GetPointerType = nullptr;
static PFN_GetDpiForWindow              s_GetDpiForWindow = nullptr;
static PFN_SetThreadDpiAwarenessContext s_SetThreadDpiCtx = nullptr;
#ifdef PEN_FLAG_ERASER
// Stift: Radierer-Ende bzw. umgedrehter Stift (Zeitstrahl: Menue am Label, wie im DFS-Entwurf)
typedef BOOL   (WINAPI* PFN_GetPointerPenInfo)(UINT32, POINTER_PEN_INFO*);
static PFN_GetPointerPenInfo            s_GetPointerPenInfo = nullptr;
#endif

static void LoadUser32Extras()
{
    static bool loaded = false;
    if (loaded) return;
    loaded = true;
    HMODULE u = GetModuleHandleW(L"user32.dll");
    if (!u) return;
    s_GetPointerType  = (PFN_GetPointerType)(void*)GetProcAddress(u, "GetPointerType");
    s_GetDpiForWindow = (PFN_GetDpiForWindow)(void*)GetProcAddress(u, "GetDpiForWindow");
    s_SetThreadDpiCtx = (PFN_SetThreadDpiAwarenessContext)(void*)GetProcAddress(u, "SetThreadDpiAwarenessContext");
#ifdef PEN_FLAG_ERASER
    s_GetPointerPenInfo = (PFN_GetPointerPenInfo)(void*)GetProcAddress(u, "GetPointerPenInfo");
#endif
}

// EuroScope selbst ist nicht DPI-aware. Unser Fenster laeuft "per monitor",
// damit es auf einem hochaufloesenden Touchscreen scharf bleibt. Jeder Aufruf
// von aussen (Befehle aus EuroScope) laeuft deshalb in diesem Kontext.
class DpiScope
{
public:
    DpiScope()
    {
        LoadUser32Extras();
        if (s_SetThreadDpiCtx) m_old = s_SetThreadDpiCtx((HANDLE)(LONG_PTR)-4);  // PER_MONITOR_AWARE_V2
    }
    ~DpiScope()
    {
        if (s_SetThreadDpiCtx && m_old) s_SetThreadDpiCtx(m_old);
    }
private:
    HANDLE m_old = nullptr;
};

// ---------------------------------------------------------------------------
// Farben
// ---------------------------------------------------------------------------
// Zwei Farbschemata: dunkel (Standard) und hell, angelehnt an Tower-Streifen-
// systeme wie bei der DFS: heller Arbeitsplatz, schwarze Schrift, Abfluege
// hellblau, Ankuenfte hellgelb. SetTheme() stellt alle Farben um.
namespace col
{
    COLORREF app, topBar, bay, bayBorder, bayHeader, text, textDim, paper, paperSel, paperLine, paperArr, paperArrSel,
             lineArr, ink, inkDim, ifr, vfr, mixed, select, warn, local, ok, card, cardBorder, button, danger, tsatPaper,
             tsatCard, emptyText, scrollTrack, scrollThumb, shadow, stripFrame, warnText, buttonAlt, input, onAccent;
    BYTE dimAlpha;
}

static void SetTheme(bool light)
{
    using namespace col;
    // in beiden Schemata gleich
    ifr = RGB(44, 106, 182);  vfr = RGB(48, 148, 70);  mixed = RGB(200, 120, 30);
    warn = RGB(200, 36, 36);  local = RGB(240, 118, 28); ok = RGB(46, 150, 80);
    tsatPaper = RGB(112, 198, 112); tsatCard = RGB(46, 150, 80); onAccent = RGB(12, 14, 18);
    // Hell: angelehnt an die DFS-Streifenanzeige (blaue Bays, graue Kopfleisten, flache Tasten).
    // Dunkel (3.0): neutrales Anthrazit um #212326 - Leisten genau darin, Grund eine Stufe dunkler,
    // Bays und Karten je eine Stufe heller; Papier leicht gedaempft, damit es nachts nicht blendet.
    if (!light)
    {
        app = RGB(25, 26, 29);          topBar = RGB(33, 35, 38);       bay = RGB(40, 43, 47);
        bayBorder = RGB(60, 64, 70);    bayHeader = RGB(50, 53, 58);    text = RGB(230, 232, 235);
        textDim = RGB(146, 151, 159);   paper = RGB(198, 212, 232);     paperSel = RGB(250, 226, 140);
        paperLine = RGB(128, 146, 172); paperArr = RGB(230, 222, 200);  paperArrSel = RGB(250, 214, 120);
        lineArr = RGB(168, 154, 120);   ink = RGB(10, 12, 14);          inkDim = RGB(70, 76, 86);
        col::select = RGB(255, 170, 0); card = RGB(43, 46, 50);         cardBorder = RGB(82, 87, 94);
        button = RGB(55, 59, 64);       danger = RGB(168, 48, 42);      emptyText = RGB(104, 110, 120);
        scrollTrack = RGB(33, 35, 38);  scrollThumb = RGB(88, 93, 101); shadow = RGB(8, 9, 10);
        stripFrame = RGB(68, 73, 80);   warnText = RGB(255, 120, 100);  buttonAlt = RGB(68, 72, 78);
        input = RGB(27, 29, 32);        dimAlpha = 110;
    }
    else
    {
        // Hintergruende blass (2.0.43)
        app = RGB(210, 215, 222);       topBar = RGB(226, 229, 232);    bay = RGB(184, 204, 236);
        bayBorder = RGB(146, 168, 204);  bayHeader = RGB(214, 214, 208); text = RGB(14, 16, 18);
        textDim = RGB(70, 76, 84);      paper = RGB(232, 241, 252);     paperSel = RGB(255, 236, 150);
        paperLine = RGB(150, 172, 198); paperArr = RGB(250, 245, 230);  paperArrSel = RGB(255, 228, 140);
        lineArr = RGB(190, 176, 140);   ink = RGB(0, 0, 0);             inkDim = RGB(84, 90, 98);
        col::select = RGB(230, 120, 0); card = RGB(232, 234, 236);      cardBorder = RGB(120, 126, 134);
        button = RGB(214, 216, 212);    danger = RGB(196, 56, 48);      emptyText = RGB(70, 98, 150);
        scrollTrack = RGB(160, 186, 226); scrollThumb = RGB(96, 124, 170); shadow = RGB(90, 96, 104);
        stripFrame = RGB(90, 100, 114); warnText = RGB(190, 30, 24);    buttonAlt = RGB(196, 200, 204);
        input = RGB(255, 255, 255);     dimAlpha = 70;
    }
}

// Hervorhebung: 1 gelb, 2 gruen, 3 blau, 4 rot, 5 lila (Papier) bzw. kraeftig (Buttons)
static const COLORREF kHlPaper[6] = { 0, RGB(255, 240, 140), RGB(190, 236, 180), RGB(184, 214, 250), RGB(250, 190, 186), RGB(226, 204, 246) };
static const COLORREF kHlStrong[6] = { RGB(200, 200, 200), RGB(240, 200, 40), RGB(70, 170, 80), RGB(60, 120, 210), RGB(210, 60, 50), RGB(150, 90, 200) };

// Farben konsequent nach Richtung: Abflug-Bays in Blautoenen (wie das blaue Abflugpapier),
// Ankunfts-Bays in warmen Ocker-/Brauntoenen (wie das Ankunftspapier), Runway (beides) neutral
static const COLORREF kBayAccent[BAY_COUNT] = {
    RGB(120, 140, 170),  // Pending
    RGB(74, 132, 200),   // On frequency
    RGB(44, 112, 190),   // Startup
    RGB(32, 92, 168),    // Pushback
    RGB(22, 72, 150),    // Taxi Out
    RGB(96, 154, 212),   // An Ground uebergeben (Apron)
    RGB(60, 122, 184),   // An Tower uebergeben
    RGB(104, 108, 120),  // Runway (Abflug und Ankunft)
    RGB(214, 152, 44),   // Final
    RGB(190, 122, 40),   // Taxi In bei Ground
    RGB(150, 96, 36),    // Taxi In bei Apron (TXIN)
};

static const wchar_t* kBayTitle[BAY_COUNT] = {
    L"PENDING", L"ON FREQUENCY", L"STARTUP", L"PUSHBACK", L"TAXI OUT", L"TO GROUND", L"TO TOWER", L"RUNWAY", L"FINAL",
    L"TAXI IN GROUND", L"TAXI IN APRON"
};

static const wchar_t* kStateButton[GC_COUNT] = {
    L"Pending", L"On Freq", L"Startup", L"Push", L"Taxi Out", L"Line-up", L"Takeoff clearance", L"Taxi In", L"Parked"
};

// Spalten je Ansicht. Eine Spalte zeigt eine oder mehrere Bays; Ziehen in
// die Spalte legt den Streifen in dropBay.
struct ColumnDef
{
    const wchar_t* title;
    int accent;          // Bay, deren Farbe die Spalte bekommt
    int bays[4];
    int nb;
    int dropBay;
    bool perRwy;         // bei getrennten Pisten je Piste eine eigene Spalte
    int kind = 0;        // 0 = alle Streifen, 1 = nur Abfluege, 2 = nur Ankuenfte,
                         // 3 = Uebergaben: Abfluege (Handoff/Runway, nicht in der Luft) und Ankuenfte an Apron (TXIN)
    int weight = 10;     // relative Breite: Arbeitsspalten 10, Uebergabe-Spalten schmaler
    int gs = 0;          // Ground-Stands (GAT, Apron 1, W): 0 egal, 1 nur diese, 2 ohne diese
    int stack = 0;       // 1 = oberer Teil einer Doppelspalte, 2 = unterer Teil (60 %), 3 = kleiner unterer Teil (30 %)
    int dropArr = -1;    // Ankunft hineingezogen: in diese Bay (-1 = dropBay)
    const char* rwyMatch = nullptr;   // nur Streifen dieser Pisten ("09L 27R"), "HELI" = Hubschrauber
    int vw = 0;          // stack 4: untereinander im Platz der vorigen Spalte, Hoehe nach vw (0 = 1)
};

// RUNWAY fasst Line-up, Takeoff und Landing zusammen: Abfluege mit LINEUP oder
// DEPA und Ankuenfte auf der Piste, in einer gemeinsamen Reihenfolge. Start- und
// Landefreigabe markiert man auf dem Streifen (Feld T/O bzw. LDG).
// Pistenbezogen (perRwy) sind alle Spalten, die ein Tower-Lotse einer Piste fuehrt.
static const ColumnDef kColsAll[] = {
    { L"PENDING",         BAY_PENDING,  { BAY_PENDING },             1, BAY_PENDING,  false },
    { L"ON FREQUENCY",    BAY_ONFREQ,   { BAY_ONFREQ },              1, BAY_ONFREQ,   false },
    { L"STARTUP",         BAY_STARTUP,  { BAY_STARTUP },             1, BAY_STARTUP,  false },
    { L"PUSHBACK",        BAY_PUSHBACK, { BAY_PUSHBACK },            1, BAY_PUSHBACK, false },
    { L"TAXI / HOLDING",  BAY_TAXI,     { BAY_HANDOFF, BAY_TOGND, BAY_TAXI }, 3, BAY_TAXI, true },
    { L"RUNWAY",          BAY_RUNWAY,   { BAY_RUNWAY },              1, BAY_RUNWAY,   true },
    { L"FINAL",           BAY_FINAL,    { BAY_FINAL },               1, BAY_FINAL,    true },
    { L"TAXI IN",         BAY_GNDIN,    { BAY_GNDIN, BAY_TAXIIN },   2, BAY_GNDIN,    false },
};
static const ColumnDef kColsDel[] = {
    { L"PENDING",         BAY_PENDING,  { BAY_PENDING },             1, BAY_PENDING,  false },
    { L"ON FREQUENCY",    BAY_ONFREQ,   { BAY_ONFREQ },              1, BAY_ONFREQ,   false },
    { L"STARTUP",         BAY_STARTUP,  { BAY_STARTUP },             1, BAY_STARTUP,  false },
};
// Weniger ist mehr: nur die Spalten, in denen gearbeitet wird, breit; Uebergabe-
// Spalten (TO GROUND, TO APRON, DEPARTING TOWER, Inbounds beim Tower) schmal.
// Uebergebene Streifen entfernt man mit einem Tipp auf FREQ (wie beim Wegschicken).
// Bay-Namen durchgehend englisch, bei Pistenspalten mit der aktiven Piste ("RUNWAY 25R").
//
// Ground mit Apron online: erste Spalte oben TAXI OUT, darunter klein STARTUP nur fuer
// Flieger, die ohne Apron direkt zu Ground kommen (GAT, Apron 1, W-Stands).
static const ColumnDef kColsGnd[] = {
    { L"TAXI OUT",        BAY_TAXI,     { BAY_TOGND, BAY_TAXI },     2, BAY_TAXI,     false, 1, 10, 0, 1 },
    { L"STARTUP / PUSH",  BAY_STARTUP,  { BAY_PENDING, BAY_ONFREQ, BAY_STARTUP, BAY_PUSHBACK }, 4, BAY_STARTUP, false, 1, 10, 1, 3 },
    { L"TAXI IN",         BAY_GNDIN,    { BAY_GNDIN },               1, BAY_GNDIN,    false, 2, 10 },
};
// Ground ohne Apron: Startup und Pushback fuer alle, Taxi In bis zur Parkposition (STAND antippen = geparkt)
static const ColumnDef kColsGndSolo[] = {
    { L"STARTUP",         BAY_STARTUP,  { BAY_STARTUP },             1, BAY_STARTUP,  false, 0, 8 },
    { L"PUSHBACK",        BAY_PUSHBACK, { BAY_PUSHBACK },            1, BAY_PUSHBACK, false, 0, 8 },
    { L"TAXI OUT",        BAY_TAXI,     { BAY_TOGND, BAY_TAXI },     2, BAY_TAXI,     false, 1, 10 },
    { L"TAXI IN",         BAY_GNDIN,    { BAY_GNDIN, BAY_TAXIIN },   2, BAY_GNDIN,    false, 2, 10 },
};
// Apron (Vorfeld): Startup, Pushback, Taxi, to Ground (nur Abfluege von Apron-Stands),
// Taxi In: oben die eigenen (TXIN), darunter die noch bei Ground (Vorschau)
static const ColumnDef kColsApn[] = {
    { L"STARTUP",         BAY_STARTUP,  { BAY_STARTUP },             1, BAY_STARTUP,  false, 0, 10, 2 },
    { L"PUSHBACK",        BAY_PUSHBACK, { BAY_PUSHBACK },            1, BAY_PUSHBACK, false, 0, 10, 2 },
    { L"TAXI",            BAY_TAXI,     { BAY_TAXI },                1, BAY_TAXI,     false, 1, 10, 2 },
    { L"TAXI IN",         BAY_TAXIIN,   { BAY_TAXIIN, BAY_GNDIN },   2, BAY_TAXIIN,   false, 2, 10, 2 },
};
// Apron, wenn kein Delivery online ist: Delivery (Pending und On Frequency) in einer Spalte davor
static const ColumnDef kColsApnDel[] = {
    { L"PENDING",         BAY_PENDING,  { BAY_PENDING },             1, BAY_PENDING,  false, 0, 8 },
    { L"ON FREQUENCY",    BAY_ONFREQ,   { BAY_ONFREQ },              1, BAY_ONFREQ,   false, 0, 8 },
    { L"STARTUP",         BAY_STARTUP,  { BAY_STARTUP },             1, BAY_STARTUP,  false, 0, 10, 2 },
    { L"PUSHBACK",        BAY_PUSHBACK, { BAY_PUSHBACK },            1, BAY_PUSHBACK, false, 0, 10, 2 },
    { L"TAXI",            BAY_TAXI,     { BAY_TAXI },                1, BAY_TAXI,     false, 1, 10, 2 },
    { L"TAXI IN",         BAY_TAXIIN,   { BAY_TAXIIN, BAY_GNDIN },   2, BAY_TAXIIN,   false, 2, 10, 2 },
};
// Tower: je Piste eine Reihe HOLDING | RUNWAY | FINAL (erste Piste oben, zweite unten),
// rechts schmal und fuer beide Pisten gemeinsam die an Ground abgegebenen Ankuenfte.
static const ColumnDef kColsTwr[] = {
    { L"HOLDING",         BAY_TAXI,     { BAY_HANDOFF, BAY_TOGND, BAY_TAXI }, 3, BAY_TAXI, true, 0, 10 },
    { L"RUNWAY",          BAY_RUNWAY,   { BAY_RUNWAY },              1, BAY_RUNWAY,   true,  0, 11 },
    { L"FINAL",           BAY_FINAL,    { BAY_FINAL },               1, BAY_FINAL,    true,  0, 10 },
};

// Farben der Pistengruppen: kraeftig fuer Kopfzeilen, hell fuer das RWY-Feld auf dem Papier
static const COLORREF kRwyAccent[3] = { RGB(120, 130, 140), RGB(236, 112, 84), RGB(150, 116, 226) };
static const COLORREF kRwyPaper[3]  = { RGB(255, 255, 255), RGB(248, 196, 178), RGB(214, 200, 246) };
static const COLORREF kRwyUnknown   = RGB(255, 170, 90);

struct ViewDef
{
    const ColumnDef* cols;
    int n;
    const wchar_t* name;
    int accent;          // -1 = neutraler Button
};
static const ViewDef kViews[VIEW_COUNT] = {
    { kColsAll, (int)(sizeof(kColsAll) / sizeof(kColsAll[0])), L"All Bays", -1 },
    { kColsDel, (int)(sizeof(kColsDel) / sizeof(kColsDel[0])), L"Delivery", BAY_ONFREQ },
    { kColsGnd, (int)(sizeof(kColsGnd) / sizeof(kColsGnd[0])), L"Ground", BAY_TAXI },
    { kColsTwr, (int)(sizeof(kColsTwr) / sizeof(kColsTwr[0])), L"Tower", BAY_RUNWAY },
    { kColsApn, (int)(sizeof(kColsApn) / sizeof(kColsApn[0])), L"Apron", BAY_TOGND },
};
static const ViewDef kViewApnDel = { kColsApnDel, (int)(sizeof(kColsApnDel) / sizeof(kColsApnDel[0])), L"Apron + DEL", BAY_TOGND };
static const ViewDef kViewGndSolo = { kColsGndSolo, (int)(sizeof(kColsGndSolo) / sizeof(kColsGndSolo[0])), L"Ground + Apron", BAY_TAXI };
// Aktuelle Ansicht; Apron ohne Delivery online zeigt die Delivery-Spalte mit,
// Ground ohne Apron online Startup, Pushback und Taxi In bis zum Stand
// Top-down: wer macht die unbesetzten unteren Stationen mit?
//   Delivery:          DEL -> Apron (falls es eins gibt) -> Ground -> Tower
//   Startup/Pushback:  Apron -> Ground -> Tower (GAT/Apron 1/W-Stands: Ground -> Tower)
//   Taxi In:           Ground -> Tower
static const ColumnDef kColDelivery    = { L"DELIVERY", BAY_ONFREQ, { BAY_PENDING, BAY_ONFREQ }, 2, BAY_PENDING, false, 0, 9 };
static const ColumnDef kColDeliveryAp  = { L"DELIVERY", BAY_ONFREQ, { BAY_PENDING, BAY_ONFREQ }, 2, BAY_PENDING, false, 0, 9, 2 };
static const ColumnDef kColStartup     = { L"STARTUP",  BAY_STARTUP, { BAY_STARTUP, BAY_PUSHBACK }, 2, BAY_STARTUP, false, 0, 9 };
static const ColumnDef kColStartupGs   = { L"STARTUP",  BAY_STARTUP, { BAY_STARTUP, BAY_PUSHBACK }, 2, BAY_STARTUP, false, 0, 8, 1 };
static const ColumnDef kColTwrTaxiOut  = { L"TAXI",     BAY_TAXI, { BAY_TOGND, BAY_TAXI }, 2, BAY_TAXI, true, 1, 10 };
static const ColumnDef kColTwrTaxiIn   = { L"TAXI IN",  BAY_GNDIN, { BAY_GNDIN, BAY_TAXIIN }, 2, BAY_GNDIN, false, 2, 10 };

static const ViewDef& ViewOfBase(int view, const Board& b);
// erste Spalte mit Pistenfilter (Hannover): dort landen Streifen ohne bekannte Piste
static int FirstRwyDef(const ViewDef& v)
{
    for (int c = 0; c < v.n; ++c)
        if (v.cols[c].rwyMatch && std::string(v.cols[c].rwyMatch) != "HELI" && std::string(v.cols[c].rwyMatch) != "AREA") return c;
    return -1;
}
// Apron ohne Ground online: statt TO GROUND die Spalte TO TOWER (Apron gibt direkt an den Tower)
static const ViewDef& ViewOfNamed(int view, const Board& b);
// Platz ohne Apron (Hannover, Leipzig): im Namen kein "Apron"/"APN"
static const ViewDef& ViewOf(int view, const Board& b)
{
    const ViewDef& v = ViewOfNamed(view, b);
    if (b.hasApron || !v.name) return v;
    std::wstring n = v.name;
    for (const wchar_t* x : { L" + Apron", L" + APN" })
        for (size_t p; (p = n.find(x)) != std::wstring::npos;) n.erase(p, wcslen(x));
    if (n == v.name) return v;
    static std::wstring names[8];
    static ViewDef vds[8];
    static int k = 0;
    k = (k + 1) % 8;
    names[k] = n;
    vds[k] = { v.cols, v.n, names[k].c_str(), v.accent };
    return vds[k];
}

static const ViewDef& ViewOfNamed(int view, const Board& b)
{
    const ViewDef& v = ViewOfBase(view, b);
    // Hannover, Tower: eine Runway-Bay je Piste (drei Bahnen) und eine fuer die Helipads 2 und 3
    if (view == VIEW_TWR && b.profile == "EDDV")
    {
        static std::vector<ColumnDef> tc;
        static ViewDef tv;
        tc.clear();
        std::vector<ColumnDef> tail;   // Zeitstrahl: Helipad und Area als eigene Spalten rechts daneben
        for (int i = 0; i < v.n; ++i)
        {
            // HOLDING: davor eine eigene TAXI-Bay (rollende Abfluege, Ankuenfte nach der Landung)
            if (v.cols[i].dropBay == BAY_TAXI && v.cols[i].nb == 3 && !(b.topDown && !b.gndOnline))
            {
                ColumnDef taxi = v.cols[i];
                taxi.title = L"TAXI";
                taxi.accent = BAY_TAXI;
                taxi.bays[0] = BAY_TAXI; taxi.bays[1] = BAY_TOGND; taxi.bays[2] = BAY_GNDIN; taxi.bays[3] = BAY_TAXIIN;
                taxi.nb = 4;
                taxi.dropBay = BAY_TAXI;
                taxi.dropArr = BAY_GNDIN;
                taxi.weight = 9;
                tc.push_back(taxi);
                ColumnDef hold = v.cols[i];
                hold.bays[0] = BAY_HANDOFF;
                hold.nb = 1;
                hold.dropBay = BAY_HANDOFF;
                hold.weight = 9;
                tc.push_back(hold);
                continue;
            }
            if (v.cols[i].dropBay != BAY_RUNWAY || v.cols[i].nb != 1) { tc.push_back(v.cols[i]); continue; }
            // Pisten wie auf der Karte untereinander (Nord oben, Center schmal, Sued unten),
            // daneben Helipad H2/H3 ueber AREA fuer die Hubschrauber
            static const wchar_t* kT[5] = { L"RWY 09L/27R", L"RWY 09C/27C", L"RWY 09R/27L", L"HELIPAD H2/H3", L"AREA" };
            static const char* kM[5] = { "09L 27R", "09C 27C", "09R 27L", "HELI", "AREA" };
            static const int kW[5] = { 12, 12, 12, 7, 7 };
            static const int kS[5] = { 0, 4, 4, 0, 4 };
            static const int kV[5] = { 4, 2, 4, 1, 1 };
            for (int k = 0; k < 5; ++k)
            {
                ColumnDef c = v.cols[i];
                c.title = kT[k];
                c.rwyMatch = kM[k];
                c.weight = kW[k];
                c.stack = kS[k];
                c.vw = kV[k];
                if (k >= 3 && b.opt.timeline)
                {
                    c.perRwy = false;
                    c.weight = 10;
                    tail.push_back(c);
                    continue;
                }
                tc.push_back(c);
            }
        }
        for (const ColumnDef& c : tail) tc.push_back(c);
        tv = { tc.data(), (int)tc.size(), v.name, v.accent };
        return tv;
    }
    if (view != VIEW_APN || !b.hasApron || b.gndOnline) return v;
    static std::vector<ColumnDef> cols;
    static ViewDef vd;
    cols.assign(v.cols, v.cols + v.n);
    for (ColumnDef& c : cols)
        if (c.dropBay == BAY_TOGND)
        {
            c.title = L"TO TOWER";
            c.accent = BAY_HANDOFF;
            c.bays[0] = BAY_HANDOFF;
            c.nb = 1;
            c.dropBay = BAY_HANDOFF;
        }
    vd = { cols.data(), (int)cols.size(), v.name, v.accent };
    return vd;
}

static const ViewDef& ViewOfBase(int view, const Board& b)
{
    if (view == VIEW_APN && !b.hasApron) view = VIEW_GND;   // Platz ohne Apron
    // Platz ohne Apron (Hannover, Leipzig): Ground macht immer Startup und Pushback, auch ohne Top-down
    if (!b.topDown) return (view == VIEW_GND && !b.hasApron) ? kViewGndSolo : kViews[view];

    bool apn = b.hasApron && b.apnOnline;
    if (view == VIEW_APN && !b.delOnline) return kViewApnDel;
    if (view == VIEW_GND)
    {
        const ViewDef& base = apn ? kViews[VIEW_GND] : kViewGndSolo;
        if (b.delOnline || apn) return base;   // Delivery macht Apron
        // Ground macht auch Delivery
        static std::vector<ColumnDef> cols[2];
        static ViewDef vd[2];
        int k = apn ? 1 : 0;
        cols[k].clear();
        cols[k].push_back(apn ? kColDeliveryAp : kColDelivery);
        for (int i = 0; i < base.n; ++i) cols[k].push_back(base.cols[i]);
        vd[k] = { cols[k].data(), (int)cols[k].size(), apn ? L"Ground + DEL" : L"Ground + Apron + DEL", BAY_TAXI };
        return vd[k];
    }
    if (view == VIEW_TWR && !b.gndOnline)
    {
        // Tower macht Ground (Taxi In), je nach Lage auch Startup/Pushback und Delivery
        static std::vector<ColumnDef> cols;
        static ViewDef vd;
        cols.clear();
        bool del = !b.delOnline && !apn;
        if (del) cols.push_back(kColDelivery);
        // Tower + GND: Taxi und Holding in einer Spalte (TAXI / HOLDING); ganz rechts TAXI IN und
        // darunter STARTUP / PUSH (mit Apron online nur die Ground-Stands GAT, Apron 1, W: klein)
        const ViewDef& base = kViews[VIEW_TWR];
        for (int i = 0; i < base.n; ++i)
        {
            ColumnDef c = base.cols[i];
            if (c.dropBay == BAY_TAXI && c.nb == 3) c.title = L"TAXI / HOLDING";
            cols.push_back(c);
        }
        ColumnDef ti = kColTwrTaxiIn;
        ti.stack = 1;
        ColumnDef su = apn ? kColStartupGs : kColStartup;
        su.title = L"STARTUP / PUSH";
        su.stack = apn ? 3 : 2;
        cols.push_back(ti);
        cols.push_back(su);
        vd = { cols.data(), (int)cols.size(), del ? (apn ? L"Tower + GND + DEL" : L"Tower + GND + APN + DEL")
                                                  : (apn ? L"Tower + GND" : L"Tower + GND + APN"), BAY_RUNWAY };
        return vd;
    }
    return kViews[view];
}

// Status-Buttons in den Details fuer Abfluege
static const int kDepButtons[] = { GC_NSTS, GC_ONFREQ, GC_STUP, GC_PUSH, GC_TAXI, GC_LINEUP, GC_DEPA };

// ---------------------------------------------------------------------------
// Kleine GDI-Helfer
// ---------------------------------------------------------------------------
static std::wstring W(const std::string& s)
{
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_ACP, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_ACP, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

static void Fill(HDC hdc, const RECT& rc, COLORREF c)
{
    SetDCBrushColor(hdc, c);
    FillRect(hdc, &rc, (HBRUSH)GetStockObject(DC_BRUSH));
}

static void Frame(HDC hdc, const RECT& rc, COLORREF c, int w)
{
    RECT t = { rc.left, rc.top, rc.right, rc.top + w };
    RECT b = { rc.left, rc.bottom - w, rc.right, rc.bottom };
    RECT l = { rc.left, rc.top, rc.left + w, rc.bottom };
    RECT r = { rc.right - w, rc.top, rc.right, rc.bottom };
    Fill(hdc, t, c); Fill(hdc, b, c); Fill(hdc, l, c); Fill(hdc, r, c);
}

static void RoundBox(HDC hdc, const RECT& rc, int radius, COLORREF fill, COLORREF border)
{
    HGDIOBJ ob = SelectObject(hdc, GetStockObject(DC_BRUSH));
    HGDIOBJ op = SelectObject(hdc, GetStockObject(DC_PEN));
    SetDCBrushColor(hdc, fill);
    SetDCPenColor(hdc, border);
    // flaches Design: hoechstens leicht abgerundete Ecken
    radius = std::min(radius, 4);
    if (radius <= 1) Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
    else RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, radius, radius);
    SelectObject(hdc, ob);
    SelectObject(hdc, op);
}

static void Text(HDC hdc, RECT rc, const std::wstring& s, HFONT f, COLORREF c,
                 UINT fmt = DT_LEFT | DT_VCENTER | DT_SINGLELINE)
{
    if (s.empty()) return;
    HGDIOBJ of = SelectObject(hdc, f);
    SetTextColor(hdc, c);
    SetBkMode(hdc, TRANSPARENT);
    DrawTextW(hdc, s.c_str(), (int)s.size(), &rc, fmt | DT_NOPREFIX | ((fmt & DT_SINGLELINE) ? DT_END_ELLIPSIS : 0));
    SelectObject(hdc, of);
}

static int TextWidth(HDC hdc, const std::wstring& s, HFONT f)
{
    HGDIOBJ of = SelectObject(hdc, f);
    SIZE sz = {};
    GetTextExtentPoint32W(hdc, s.c_str(), (int)s.size(), &sz);
    SelectObject(hdc, of);
    return sz.cx;
}

// Rechteck mit einer Farbe halbdurchsichtig ueberdecken (Vorankuendigung: grau)
static void WashRect(HDC hdc, const RECT& r, COLORREF c, BYTE alpha)
{
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = 1;
    bi.bmiHeader.biHeight = 1;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp) return;
    *(DWORD*)bits = (DWORD)((GetRValue(c) << 16) | (GetGValue(c) << 8) | GetBValue(c));
    HDC src = CreateCompatibleDC(hdc);
    HGDIOBJ old = SelectObject(src, bmp);
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, alpha, 0 };
    AlphaBlend(hdc, r.left, r.top, r.right - r.left, r.bottom - r.top, src, 0, 0, 1, 1, bf);
    SelectObject(src, old);
    DeleteDC(src);
    DeleteObject(bmp);
}

static void Dim(HDC hdc, int w, int h, BYTE alpha)
{
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = 1;
    bi.bmiHeader.biHeight = 1;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp) return;
    *(DWORD*)bits = 0;  // schwarz
    HDC src = CreateCompatibleDC(hdc);
    HGDIOBJ old = SelectObject(src, bmp);
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, alpha, 0 };
    AlphaBlend(hdc, 0, 0, w, h, src, 0, 0, 1, 1, bf);
    SelectObject(src, old);
    DeleteDC(src);
    DeleteObject(bmp);
}

static std::wstring FmtAlt(int ft, int ta)
{
    if (ft <= 0) return L"-";
    wchar_t buf[32];
    if (ft > ta) swprintf(buf, 32, L"FL%03d", (ft + 50) / 100);
    else         swprintf(buf, 32, L"%d", ft);
    return buf;
}

// Freigegebene Hoehe (CFL), auch die Anflug-Sonderwerte von EuroScope
static std::wstring CflText(const StripData& s, int ta)
{
    if (s.clearedApproach == 1) return L"ILS";
    if (s.clearedApproach == 2) return L"VIS";
    return s.clearedAlt > 0 ? FmtAlt(s.clearedAlt, ta) : std::wstring();
}

static std::wstring Or(const std::string& s, const wchar_t* fallback = L"-")
{
    return s.empty() ? std::wstring(fallback) : W(s);
}

static RECT Rc(int l, int t, int r, int b) { RECT x = { l, t, r, b }; return x; }

// Liegt die aktuelle Zeit innerhalb TSAT +/- 5 Minuten?
// CTOT-Slotfenster (-5/+10 Minuten): 0 keine/egal, 1 frueh, 2 bald (ab -15), 3 im Fenster, 4 verpasst
static int CtotState(const StripData& s)
{
    if (s.arrival || s.ctotUtc <= 0 || s.airborne) return 0;
    long long d = (long long)std::time(nullptr) - s.ctotUtc;
    if (d > 10 * 60) return 4;
    if (d >= -5 * 60) return 3;
    if (d >= -15 * 60) return 2;
    return 1;
}
static const COLORREF kCtotFill[5] = { 0xFFFFFFFF, RGB(255, 214, 153), RGB(250, 222, 90), RGB(150, 214, 140), RGB(240, 96, 80) };

// Stand im Detailfenster mit Hinweis (doppelt, besetzt, gesperrt, fehlt)
static std::wstring StandDetail(const StripData& s)
{
    if (s.standMissing && s.stand.empty()) return L"- (no stand yet)";
    std::wstring t = s.stand.empty() ? std::wstring(L"-") : W(s.stand);
    if (s.standConflict) t += L"  (also " + W(s.standConflictWith) + L"!)";
    if (s.standBusy == 2) t += L"  (occupied by " + W(s.standBusyBy) + L")";
    if (s.standBusy == 3) t += L"  (blocked by " + W(s.standBusyBy) + L")";
    return t;
}

// TSAT + 5 Minuten vorbei und noch kein Startup (Pending / On Freq)
static bool TsatExpired(const StripData& s)
{
    if (s.arrival || s.tsatUtc <= 0) return false;
    if (!(s.state == GC_NSTS || s.state == GC_ONFREQ)) return false;
    return (long long)std::time(nullptr) - s.tsatUtc > 300;
}

static bool InTsatWindow(const StripData& s)
{
    if (s.tsatUtc <= 0) return false;
    long long d = (long long)std::time(nullptr) - s.tsatUtc;
    return d >= -300 && d <= 300;
}

// "121.705 EDDB_S_GND" -> "121.705"
// Zustaendigkeit: "GNDN" -> "GND N"; Farbe je Stelle
static std::wstring RespLabel(const std::string& u)
{
    if (u.size() == 4) return W(u.substr(0, 3)) + L" " + W(u.substr(3));
    return W(u);
}
static COLORREF RespColor(const std::string& u)
{
    if (u.compare(0, 3, "DEL") == 0) return RGB(110, 110, 120);
    if (u.compare(0, 3, "APN") == 0) return RGB(46, 140, 80);
    if (u.compare(0, 3, "GND") == 0) return RGB(170, 110, 30);
    if (u.compare(0, 3, "TWR") == 0) return RGB(54, 104, 168);
    return RGB(120, 120, 120);
}

// Freigabe: PENDING (noch nicht), SENT (DCL per CPDLC raus, wartet auf WILCO), CLEARED
static std::wstring ClearanceText(const StripData& s)
{
    if (s.clearance) return L"CLEARED";
    if (s.dlDcl == 2) return L"SENT";
    return L"PENDING";
}

static std::string Split1(const std::string& s)
{
    size_t sp = s.find(' ');
    return sp == std::string::npos ? s : s.substr(0, sp);
}

// "EDDB 301820Z 24008KT 200V270 CAVOK 09/04 Q1021 NOSIG" -> "EDDB  24008KT 200V270  Q1021"
static std::string ShortMetar(const std::string& metar)
{
    std::vector<std::string> t;
    size_t p = 0;
    while (p < metar.size())
    {
        size_t e = metar.find(' ', p);
        if (e == std::string::npos) e = metar.size();
        if (e > p) t.push_back(metar.substr(p, e - p));
        p = e + 1;
    }
    std::string icao, wind, qnh;
    for (size_t i = 0; i < t.size(); ++i)
    {
        const std::string& w = t[i];
        if (icao.empty() && w.size() == 4 && isalpha((unsigned char)w[0]) && w != "AUTO" && w != "METAR") icao = w;
        bool kt = w.size() >= 7 && (w.compare(w.size() - 2, 2, "KT") == 0 || w.compare(w.size() - 3, 3, "MPS") == 0);
        if (wind.empty() && kt && (isdigit((unsigned char)w[0]) || w.compare(0, 3, "VRB") == 0))
        {
            wind = w;
            // Windrichtungsbereich "200V270" direkt danach
            if (i + 1 < t.size() && t[i + 1].size() == 7 && t[i + 1][3] == 'V') wind += " " + t[i + 1];
        }
        if (qnh.empty() && w.size() == 5 && (w[0] == 'Q' || w[0] == 'A') && isdigit((unsigned char)w[1]) &&
            isdigit((unsigned char)w[4]))
            qnh = w;   // "Q1021" bzw. "A2992"
    }
    std::string out = icao;
    if (!wind.empty()) out += (out.empty() ? "" : "  ") + wind;
    if (!qnh.empty()) out += (out.empty() ? "" : "  ") + qnh;
    return out.empty() ? metar : out;
}

static RECT Inset(RECT r, int dx, int dy) { r.left += dx; r.right -= dx; r.top += dy; r.bottom -= dy; return r; }

// ---------------------------------------------------------------------------
// Aufbau
// ---------------------------------------------------------------------------
StripWindow::StripWindow(IStripHost* host) : m_host(host)
{
    SetTheme(false);
    LoadUser32Extras();
    UpdateScale();
}

StripWindow::~StripWindow()
{
    Destroy();
    DeleteFonts();
}

bool StripWindow::Create(const WindowSettings& settings)
{
    if (m_hwnd) return true;

    HINSTANCE inst = (HINSTANCE)&__ImageBase;
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &StripWindow::WndProcStatic;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    DpiScope dpi;

    m_zoom = std::min(2.5, std::max(0.6, settings.zoom));
    m_layout = (settings.layout >= 0 && settings.layout <= 2) ? settings.layout : 0;
    m_view = (settings.view >= 0 && settings.view < VIEW_COUNT) ? settings.view : VIEW_ALL;
    m_rwyFilter = (settings.rwyFilter == RWY_A || settings.rwyFilter == RWY_B) ? settings.rwyFilter : 0;
    m_light = settings.light;
    SetTheme(m_light);
    m_topmost = settings.topmost;
    m_normalRect = settings.normalRect;

    // Gespeicherte Position nur nutzen, wenn sie noch auf einem Monitor liegt
    RECT r = m_normalRect;
    if (r.right - r.left < 300 || r.bottom - r.top < 200 || !MonitorFromRect(&r, MONITOR_DEFAULTTONULL))
    {
        r = Rc(120, 120, 1520, 860);
        m_normalRect = r;
    }

    DWORD ex = WS_EX_NOACTIVATE | WS_EX_APPWINDOW | (m_topmost ? WS_EX_TOPMOST : 0);
    m_hwnd = CreateWindowExW(ex, kClassName, L"StripBay", WS_OVERLAPPEDWINDOW,
                             r.left, r.top, r.right - r.left, r.bottom - r.top,
                             nullptr, nullptr, inst, this);
    if (!m_hwnd) return false;

    m_dpi = s_GetDpiForWindow ? s_GetDpiForWindow(m_hwnd) : 96;
    if (m_dpi == 0) m_dpi = 96;
    UpdateScale();

    SetTimer(m_hwnd, kClockTimer, 500, nullptr);   // Uhr und Blinken (Anforderung, DCL)

    if (settings.fullscreen) ToggleFullscreen();
    if (settings.visible) ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    return true;
}

void StripWindow::Destroy()
{
    if (m_hwnd)
    {
        DpiScope dpi;
        KillTimer(m_hwnd, kClockTimer);
        SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, 0);
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
        UnregisterClassW(kClassName, (HINSTANCE)&__ImageBase);
    }
    if (m_memDC)
    {
        SelectObject(m_memDC, m_memOld);
        DeleteObject(m_memBmp);
        DeleteDC(m_memDC);
        m_memDC = nullptr;
        m_memBmp = nullptr;
        m_memW = m_memH = 0;
    }
}

void StripWindow::SetBoard(const Board& board)
{
    if (!board.atis.empty() && board.atis != m_lastAtis)
    {
        if (!m_lastAtis.empty() && board.wxAirport == m_board.wxAirport) m_atisChanged = (long long)std::time(nullptr);
        m_lastAtis = board.atis;
    }
    m_board = board;
    if (!m_board.hasApron && m_view == VIEW_APN) m_view = VIEW_GND;   // Platz ohne Apron: Ground
    RebuildColumns();
    // Zeitstrahl: seit wann ist eine Ankunft gelandet bzw. ein Abflug in der Luft (wandert nach links)
    {
        long long now = (long long)std::time(nullptr);
        std::set<std::string> live;
        for (int b = 0; b < BAY_COUNT; ++b)
            for (const StripData& s : m_board.bays[b])
                if (!s.freeStrip && (s.arrival ? s.onGround : s.airborne))
                {
                    live.insert(s.callsign);
                    m_tlSeen.emplace(s.callsign, now);
                }
        for (auto it = m_tlSeen.begin(); it != m_tlSeen.end();)
            if (live.count(it->first)) ++it;
            else it = m_tlSeen.erase(it);
    }

    // Streifen weg oder in eine ausgeblendete Spalte gewandert: Details schliessen, Drag abbrechen
    if (!m_detailCs.empty() && ColumnOf(m_detailCs) < 0) m_detailCs.clear();
    if (m_editing && m_editMode != 3 && m_editMode != 6 && !FindStrip(m_editCs)) CloseEditor(false);
    if (m_dlOpen && !FindStrip(m_dlCs)) m_dlOpen = false;

    if (m_mode == M_PRESS_STRIP || m_mode == M_DRAG_STRIP)
    {
        int bay = -1;
        // Ein Streifen ohne Pistengruppe steht in beiden Pistenspalten: die behalten, aus der er kommt
        int col = ColumnHas(m_dragFromCol, m_dragCs) ? m_dragFromCol : ColumnOf(m_dragCs);
        if (col < 0 || !FindStrip(m_dragCs, &bay)) PointerCancel();
        else
        {
            m_dragFromCol = col;
            m_dragBay = bay;
        }
    }

    Invalidate();
}

void StripWindow::Show(bool show)
{
    if (!m_hwnd) return;
    DpiScope dpi;
    ShowWindow(m_hwnd, show ? SW_SHOWNOACTIVATE : SW_HIDE);
}

bool StripWindow::IsVisible() const
{
    return m_hwnd && IsWindowVisible(m_hwnd);
}

void StripWindow::ToggleFullscreen()
{
    if (!m_hwnd) return;
    DpiScope dpi;
    DWORD vis = IsWindowVisible(m_hwnd) ? WS_VISIBLE : 0;

    if (!m_fullscreen)
    {
        GetWindowRect(m_hwnd, &m_normalRect);
        MONITORINFO mi = {};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST), &mi);
        SetWindowLongPtrW(m_hwnd, GWL_STYLE, WS_POPUP | vis);
        SetWindowPos(m_hwnd, nullptr, mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOZORDER);
        m_fullscreen = true;
    }
    else
    {
        SetWindowLongPtrW(m_hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | vis);
        SetWindowPos(m_hwnd, nullptr, m_normalRect.left, m_normalRect.top,
                     m_normalRect.right - m_normalRect.left, m_normalRect.bottom - m_normalRect.top,
                     SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOZORDER);
        m_fullscreen = false;
    }
    Invalidate();
}

void StripWindow::SetTopmost(bool on)
{
    m_topmost = on;
    if (!m_hwnd) return;
    DpiScope dpi;
    SetWindowPos(m_hwnd, on ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void StripWindow::SetZoom(double zoom)
{
    m_zoom = std::min(2.5, std::max(0.6, zoom));
    UpdateScale();
    for (int& v : m_scroll) v = 0;
    Invalidate();
}

void StripWindow::SetLayout(int layout)
{
    m_layout = (layout >= 0 && layout <= 2) ? layout : 0;
    for (int& v : m_scroll) v = 0;
    Invalidate();
}

void StripWindow::SetView(int view)
{
    m_view = (view >= 0 && view < VIEW_COUNT) ? view : VIEW_ALL;
    if (m_view == VIEW_DEL) m_rwyFilter = 0;   // Delivery gibt es nur einmal: immer beide Seiten
    for (int& v : m_scroll) v = 0;
    if (m_mode != M_NONE) PointerCancel();
    if (m_editing) CloseEditor(false);
    RebuildColumns();
    if (!m_detailCs.empty() && ColumnOf(m_detailCs) < 0) m_detailCs.clear();
    Invalidate();
}

void StripWindow::SetLight(bool light)
{
    m_light = light;
    SetTheme(light);
    Invalidate();
}

void StripWindow::SetRwyFilter(int filter)
{
    m_rwyFilter = (m_view != VIEW_DEL && (filter == RWY_A || filter == RWY_B)) ? filter : 0;
    for (int& v : m_scroll) v = 0;
    if (m_mode != M_NONE) PointerCancel();
    RebuildColumns();
    if (!m_detailCs.empty() && ColumnOf(m_detailCs) < 0) m_detailCs.clear();
    if (m_editing && ColumnOf(m_editCs) < 0) CloseEditor(false);
    Invalidate();
}

void StripWindow::ShowNote(const std::wstring& text, int ms)
{
    m_note = text;
    m_noteUntil = GetTickCount() + (DWORD)std::max(1000, ms);
    Invalidate();
}

void StripWindow::SetTitle(const std::wstring& title)
{
    if (m_hwnd) SetWindowTextW(m_hwnd, title.c_str());
}

// Meldung unten in der Mitte; Remote ohne Verbindung: alles abgedunkelt mit Hinweis (Eingaben gesperrt)
void StripWindow::DrawNoteAndOffline(HDC hdc, int width, int height)
{
    m_offlineCard = {};
    if (!m_board.connected)
    {
        Dim(hdc, width, height, 150);
        int w = std::min(width - Sx(40), Sx(600)), h = Sx(m_remoteClient ? 170 : 120);
        RECT r = Rc((width - w) / 2, (height - h) / 2, (width - w) / 2 + w, (height - h) / 2 + h);
        RoundBox(hdc, r, Sx(8), col::card, RGB(220, 60, 50));
        Text(hdc, Rc(r.left + Sx(16), r.top + Sx(10), r.right - Sx(16), r.top + Sx(52)), L"NOT CONNECTED", m_fCallsign, RGB(240, 90, 76),
             DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        Text(hdc, Rc(r.left + Sx(16), r.top + Sx(52), r.right - Sx(16), r.top + Sx(80)), L"Reconnecting automatically\x2026", m_fHeader,
             col::text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (m_remoteClient)
        {
            // Grund (z. B. Firewall) ueber zwei Zeilen, darunter: hier tippen = anderer PC / Code
            const std::wstring d = m_offlineDetail.empty() ? std::wstring(L"Waiting for StripBay in EuroScope (.strips remote on).") : m_offlineDetail;
            Text(hdc, Rc(r.left + Sx(16), r.top + Sx(84), r.right - Sx(16), r.top + Sx(128)), d, m_fLabel, col::text, DT_CENTER | DT_WORDBREAK);
            Text(hdc, Rc(r.left + Sx(16), r.top + Sx(132), r.right - Sx(16), r.bottom - Sx(8)), L"Tap here to change PC or code", m_fLabel,
                 col::textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            m_offlineCard = r;
        }
        else
            Text(hdc, Rc(r.left + Sx(16), r.top + Sx(82), r.right - Sx(16), r.bottom - Sx(8)), L"EuroScope: .strips remote status", m_fLabel,
                 col::textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    if (!m_note.empty() && (int)(m_noteUntil - GetTickCount()) > 0)
    {
        int tw = std::min(width - Sx(40), TextWidth(hdc, m_note, m_fHeader) + Sx(36));
        int h = Sx(40);
        RECT r = Rc((width - tw) / 2, height - h - Sx(18), (width - tw) / 2 + tw, height - Sx(18));
        Fill(hdc, Rc(r.left + Sx(3), r.top + Sx(4), r.right + Sx(3), r.bottom + Sx(4)), col::shadow);
        RoundBox(hdc, r, Sx(6), col::card, col::cardBorder);
        FitText(hdc, Rc(r.left + Sx(14), r.top, r.right - Sx(14), r.bottom), m_note, m_fHeader, m_fLabel, col::text);
        m_anyBlink = true;   // weiter zeichnen, bis sie weg ist
    }
    else if (!m_note.empty()) m_note.clear();
}

// Fortschritt einer Bay fuer Abfluege bzw. Ankuenfte (-1 = gehoert nicht dazu): damit ein Streifen ausserhalb
// der eigenen Spalten in die naechstgelegene kommt und nie zurueck an den Anfang
static int BayStage(int bay, bool arrival)
{
    if (!arrival)
        switch (bay)
        {
        case BAY_PENDING:  return 0;
        case BAY_ONFREQ:   return 1;
        case BAY_STARTUP:  return 2;
        case BAY_PUSHBACK: return 3;
        case BAY_TAXI:     return 4;
        case BAY_TOGND:    return 5;
        case BAY_HANDOFF:  return 6;
        case BAY_RUNWAY:   return 7;
        default:           return -1;
        }
    switch (bay)
    {
    case BAY_FINAL:  return 0;
    case BAY_RUNWAY: return 1;
    case BAY_GNDIN:  return 2;
    case BAY_TAXIIN: return 3;
    default:         return -1;
    }
}

// Bei getrennten Pisten wird jede pistenbezogene Spalte aufgeteilt: entweder in
// zwei Spalten (beide Pisten, obere Haelfte erste Piste, untere zweite) oder sie
// zeigt nur die gefilterte Piste. Streifen ohne Gruppe (Piste unbekannt oder
// anderer Platz) stehen bei beiden Pisten, damit niemand sie uebersieht.
void StripWindow::RebuildColumns()
{
    const ViewDef& v = ViewOf(m_view, m_board);
    m_cols.clear();
    // Hannover im Zeitstrahl: Nordbahn (09L/27R, dazu 09C/27C) und Suedbahn (09R/27L) als zwei Seiten
    const bool tlEddv = TlSplitEddv();
    for (int c = 0; c < v.n; ++c)
    {
        const ColumnDef& def = v.cols[c];
        int groups[2] = { RWY_NONE, RWY_NONE };
        int ng = 1;
        if (m_board.rwySplit && def.perRwy)
        {
            if (m_rwyFilter == 0) { groups[0] = RWY_A; groups[1] = RWY_B; ng = 2; }
            else groups[0] = m_rwyFilter;
        }
        else if (tlEddv && def.perRwy &&
                 (def.dropBay == BAY_TAXI || def.dropBay == BAY_HANDOFF || def.dropBay == BAY_RUNWAY || def.dropBay == BAY_FINAL))
        {
            if (def.rwyMatch) groups[0] = std::string(def.rwyMatch).find("09R") != std::string::npos ? RWY_B : RWY_A;
            else { groups[0] = RWY_A; groups[1] = RWY_B; ng = 2; }
        }
        for (int gi = 0; gi < ng && (int)m_cols.size() < kMaxCols; ++gi)
        {
            Column col;
            col.def = c;
            col.group = groups[gi];
            col.sub = (ng == 2) ? gi + 1 : 0;
            for (int k = 0; k < def.nb; ++k)
            {
                int b = def.bays[k];
                for (int i = 0; i < (int)m_board.bays[b].size(); ++i)
                {
                    const StripData& sd = m_board.bays[b][i];
                    if ((def.kind == 1 && sd.arrival) || (def.kind == 2 && !sd.arrival)) continue;
                    if (def.kind == 3 && (sd.arrival ? b != BAY_TAXIIN : (b == BAY_TAXIIN || sd.airborne))) continue;
                    if (HiddenByResp(sd, b)) continue;
                    if (def.rwyMatch && !RwyColMatch(def.rwyMatch, sd, c == FirstRwyDef(v))) continue;
                    // Ground-Stand-Spalte: andere Stands nur als graue Vorschau im Startup
                    if ((def.gs == 1 && !sd.gndStand && !(m_view == VIEW_GND && b == BAY_STARTUP && Preview(sd, b))) ||
                        (def.gs == 2 && sd.gndStand && !sd.freeStrip)) continue;
                    // Ground Nord/Sued geteilt: alle Spalten nur mit der eigenen Piste
                    if (m_view == VIEW_GND && m_board.rwySplit && m_board.splitGnd && m_rwyFilter != 0 && !def.perRwy &&
                        sd.rwyGroup != RWY_NONE && sd.rwyGroup != m_rwyFilter) continue;
                    if (def.kind == 1 && sd.airborne) continue;   // Ground: Abfluege in der Luft nicht mehr
                    int g = tlEddv ? TlRwyGroup(sd) : sd.rwyGroup;
                    if (col.group != RWY_NONE && g != RWY_NONE && g != col.group) continue;
                    // Piste unbekannt: nur einmal zeigen (bei beiden Pisten in der ersten, mit "?" im RWY-Feld)
                    if (col.group != RWY_NONE && g == RWY_NONE && ng == 2 && col.group != RWY_A) continue;
                    bool dup = false;
                    for (const StripRef& r : col.strips)
                        if (m_board.bays[r.bay][r.idx].callsign == sd.callsign) dup = true;
                    if (dup) continue;
                    col.strips.push_back({ b, i });
                }
            }
            m_cols.push_back(col);
        }
    }

    // Zeitstrahl (Tower): Holding, Runway und Final gehen darin auf; je Pistengruppe eine Seite
    m_tlAbs.assign(m_cols.size(), 0);
    m_tlSideGroup.clear();
    m_tlRwyCol.clear();
    m_tlRwyCols.clear();
    for (int c = 0; c < (int)m_cols.size(); ++c)
    {
        const ColumnDef& d = v.cols[m_cols[c].def];
        bool ab = m_view == VIEW_TWR && d.perRwy &&
                  (d.dropBay == BAY_TAXI || d.dropBay == BAY_HANDOFF || d.dropBay == BAY_RUNWAY || d.dropBay == BAY_FINAL);
        m_tlAbs[c] = ab ? 1 : 0;
        if (!ab) continue;
        int g = m_cols[c].group;
        int si = (int)(std::find(m_tlSideGroup.begin(), m_tlSideGroup.end(), g) - m_tlSideGroup.begin());
        if (si == (int)m_tlSideGroup.size()) { m_tlSideGroup.push_back(g); m_tlRwyCol.push_back(-1); m_tlRwyCols.emplace_back(); }
        if (d.dropBay == BAY_RUNWAY && m_tlRwyCol[si] < 0) m_tlRwyCol[si] = c;
        if (d.dropBay == BAY_RUNWAY) m_tlRwyCols[si].push_back(c);
    }

    // Als zustaendig eingetragen: der Streifen ist immer zu sehen, auch ausserhalb des eigenen
    // Bereichs (andere Piste, andere Bay, GAT/W-Stand ...). Er kommt in die Spalte, die seine Bay
    // zeigt, sonst in die erste passende (Abflug/Ankunft).
    if (m_cols.empty()) return;
    std::set<std::string> shown;
    for (const Column& c : m_cols)
        for (const StripRef& r : c.strips) shown.insert(m_board.bays[r.bay][r.idx].callsign);
    for (int b = 0; b < BAY_COUNT; ++b)
        for (int i = 0; i < (int)m_board.bays[b].size(); ++i)
        {
            const StripData& sd = m_board.bays[b][i];
            if (!(sd.respMine || sd.fading || sd.offerForMe || sd.pinned) || sd.freeStrip || shown.count(sd.callsign)) continue;
            // meine, mir angebotene, gerade abgegebene (grau) und von Hand erstellte Streifen bleiben zu sehen,
            // auch wenn ihre Bay hier keine eigene Spalte hat
            // Ohne eigene Spalte fuer seine Bay: in die Spalte, die seinem Stand am naechsten ist - nie
            // zurueck an den Anfang (ein Push eines anderen Lotsen landet bei DEL in STARTUP, nicht in PENDING)
            const int stage = BayStage(b, sd.arrival);
            int best = -1, score = -1;
            bool bestHasBay = false;
            for (int c = 0; c < (int)m_cols.size(); ++c)
            {
                const ColumnDef& def = v.cols[m_cols[c].def];
                bool typeOk = !((def.kind == 1 && sd.arrival) || (def.kind == 2 && !sd.arrival) ||
                                (def.kind == 3 && sd.arrival && b != BAY_TAXIIN));
                bool hasBay = false;
                int closeness = 0;   // nicht "near": das ist unter Windows ein Makro
                for (int k = 0; k < def.nb; ++k)
                {
                    if (def.bays[k] == b) hasBay = true;
                    int colStage = BayStage(def.bays[k], sd.arrival);
                    if (stage >= 0 && colStage >= 0) closeness = std::max(closeness, 9 - std::min(9, std::abs(stage - colStage)));
                }
                bool grpOk = m_cols[c].group == RWY_NONE || sd.rwyGroup == RWY_NONE || sd.rwyGroup == m_cols[c].group;
                int sc = (hasBay ? 1000 : 0) + (typeOk ? 100 : 0) + (grpOk ? 10 : 0) + closeness;
                if (sc > score) { score = sc; best = c; bestHasBay = hasBay; }
            }
            (void)bestHasBay;
            if (best >= 0) { m_cols[best].strips.push_back({ b, i }); shown.insert(sd.callsign); }
        }

    // CDM: Pending und On Freq nach TSAT (frueheste oben, ohne TSAT unten in ihrer Reihenfolge)
    if (m_board.opt.tsatSort)
        for (auto& c : m_cols)
        {
            const ColumnDef& def = v.cols[c.def];
            if (def.dropBay != BAY_PENDING && def.dropBay != BAY_ONFREQ) continue;
            std::stable_sort(c.strips.begin(), c.strips.end(), [&](const StripRef& a, const StripRef& b) {
                const StripData& x = m_board.bays[a.bay][a.idx];
                const StripData& y = m_board.bays[b.bay][b.idx];
                bool hx = x.tsatUtc > 0 && !x.freeStrip, hy = y.tsatUtc > 0 && !y.freeStrip;
                if (hx != hy) return hx;
                return hx && x.tsatUtc < y.tsatUtc;
            });
        }
}

// Naechster Schritt eines Streifens: 0 keiner, 1 in Bay "next", 2 Freigabe auf der Runway (T/O bzw. LDG),
// 3 geparkt. Abfluege: Pending > On Freq > Startup > Pushback > Taxi > (Apron: an Ground, Ground: an
// Tower, Tower: Runway) > Runway > Takeoff. Ankuenfte: Final > Runway > Landefreigabe > Taxi In >
// (an Apron) > geparkt (nur am Gate).
int StripWindow::NextStep(const StripData& s, int bay, int& next) const
{
    next = -1;
    if (s.freeStrip) return 0;
    if (Outgoing(s)) return 0;   // angeboten: wartet auf die naechste Stelle
    if (s.offerForMe) return 5;  // mir angeboten: erst annehmen
    // grau (hat noch die Stelle vor mir): erst uebernehmen, damit kein Tipp aus Versehen einen fremden Flieger bewegt
    if (m_board.opt.respOn && !s.respMine && !s.resp.empty() && !s.fading && Preview(s, bay)) return 6;
    if (s.fading && !s.respMine) return 0;   // abgegeben: nichts mehr zu tun
    if (!s.arrival)
    {
        switch (bay)
        {
        case BAY_PENDING:  next = BAY_ONFREQ; break;
        case BAY_ONFREQ:   next = BAY_STARTUP; break;
        case BAY_STARTUP:
            // Delivery fuehrt bis zum Startup: danach uebergeben (4), nicht selbst pushen
            if (m_view == VIEW_DEL) return (m_board.opt.respOn && !s.nextResp.empty()) ? 4 : 0;
            next = BAY_PUSHBACK; break;
        case BAY_PUSHBACK: next = BAY_TAXI; break;
        case BAY_TAXI:     next = m_view == VIEW_APN ? (m_board.gndOnline ? BAY_TOGND : BAY_HANDOFF)
                                 : m_view == VIEW_GND ? BAY_HANDOFF : BAY_RUNWAY; break;
        case BAY_TOGND:    next = m_view == VIEW_TWR || m_view == VIEW_ALL ? BAY_RUNWAY : BAY_HANDOFF; break;
        case BAY_HANDOFF:  next = BAY_RUNWAY; break;
        case BAY_RUNWAY:   return s.state == GC_DEPA ? 0 : 2;
        default: return 0;
        }
        return 1;
    }
    switch (bay)
    {
    case BAY_FINAL:  next = BAY_RUNWAY; return 1;
    case BAY_RUNWAY: if (!s.landingCleared) return 2; next = BAY_GNDIN; return 1;
    case BAY_GNDIN:
        if (m_board.hasApron && m_board.apnOnline) { next = BAY_TAXIIN; return 1; }
        return s.atGate ? 3 : 0;
    case BAY_TAXIIN: return s.atGate ? 3 : 0;
    default: return 0;
    }
}

// Ground und Apron: Streifen, fuer die die jeweils andere Stelle zustaendig ist, ausblenden
// (ausser ich habe sie angefordert oder bin selbst zustaendig, z. B. per Top-down)
bool StripWindow::HiddenByResp(const StripData& sd, int bay) const
{
    if (sd.freeStrip || sd.respMine || sd.resp.empty() || sd.fading || sd.offerForMe || sd.pinned) return false;
    // Uebergabe-Spalten gibt es nicht mehr: was eine andere Stelle hat, ist weg,
    // ausser es kommt gleich zu mir (Vorankuendigung, grau)
    if (m_view == VIEW_ALL) return false;
    return !Preview(sd, bay);
}

// Vorankuendigung (grau): Streifen, den noch die Stelle vor mir hat und der als Naechstes zu mir kommt.
// Antippen bietet "Take over". Streifen von Stellen nach mir sind nie Vorankuendigung (die sind weg).
//   Tower:  rollende Abfluege von Ground/Apron; Final, solange der Anflug den Flieger noch hat
//   Ground: Abfluege von Delivery/Apron (Startup bzw. Rollen), gelandete Ankuenfte des Towers
//   Apron:  Startups von Delivery, Ankuenfte, die bei Ground rollen
bool StripWindow::Preview(const StripData& sd, int bay) const
{
    if (sd.freeStrip) return false;
    if (sd.arrival && bay == BAY_FINAL && sd.trackedOther) return true;
    if (sd.respMine || sd.offerForMe) return false;
    if (sd.fading) return true;   // gerade abgegeben: grau, bis er verschwindet
    auto from = [&](const char* u) { return sd.resp.compare(0, 3, u) == 0; };
    const bool none = sd.resp.empty();
    const bool upstreamDep = from("DEL") || from("APN");
    switch (m_view)
    {
    case VIEW_TWR:
        if (sd.arrival || (bay != BAY_TAXI && bay != BAY_TOGND)) return false;
        return upstreamDep || from("GND") || (none && m_board.gndOnline);
    case VIEW_GND:
        if (!sd.arrival)
        {
            // ohne Top-down schon den Startup grau, mit Top-down erst das Rollen
            if (bay == BAY_STARTUP) return upstreamDep && !m_board.topDown;
            if (bay == BAY_TAXI) return (upstreamDep && m_board.topDown) || (from("APN") && m_board.hasApron && m_board.apnOnline);
            return false;
        }
        return bay == BAY_GNDIN && from("TWR");
    case VIEW_APN:
        if (!sd.arrival) return bay == BAY_STARTUP && from("DEL");
        return bay == BAY_GNDIN && (from("GND") || none);
    default:
        return false;
    }
}

// Von mir angeboten und noch nicht angenommen: wartet ("-> TWR"), nur Zuruecknehmen
bool StripWindow::Outgoing(const StripData& s) const
{
    return !s.freeStrip && s.respMine && !s.offerTo.empty() && !s.offerForMe;
}

// Pistenspalte (Hannover): passt der Streifen? Ohne bekannte Piste in die erste Spalte.
bool StripWindow::RwyColMatch(const char* match, const StripData& sd, bool firstCol) const
{
    std::string m = match;
    std::string r;
    for (char ch : sd.arrival ? sd.arrRwy : sd.rwy) if (isalnum((unsigned char)ch)) r += (char)toupper((unsigned char)ch);
    if (r.size() == 2 && isalpha((unsigned char)r[1])) r = "0" + r;
    bool pad = r.size() >= 2 && r[0] == 'H' && isdigit((unsigned char)r[1]);
    if (m == "HELI") return sd.heli && pad;      // Hubschrauber auf einem Helipad
    if (m == "AREA") return sd.heli && !pad;     // Hubschrauber sonst (im Gebiet, ohne Helipad)
    if (sd.heli) return false;
    if (r.empty()) return firstCol;
    if (m.find(r) != std::string::npos) return true;
    // Piste gehoert zu keiner Spalte: in die erste
    return firstCol && std::string("09L 27R 09C 27C 09R 27L").find(r) == std::string::npos;
}


bool StripWindow::ShowsArrivalsIn(int bay) const
{
    const ViewDef& v = ViewOf(m_view, m_board);
    for (int c = 0; c < v.n; ++c)
    {
        if (v.cols[c].kind == 1) continue;
        if (v.cols[c].kind == 3 && bay != BAY_TAXIIN) continue;   // Uebergaben: Ankuenfte nur an Apron
        for (int k = 0; k < v.cols[c].nb; ++k)
            if (v.cols[c].bays[k] == bay) return true;
    }
    return false;
}

// Uebergeben und nur noch zur Ansicht da: Tower -> Ground (Ankuenfte), Ground -> Apron
// (Ankuenfte mit TXIN), Apron -> Ground (Abfluege). Ein Tipp auf FREQ entfernt den Streifen.
bool StripWindow::HandedOver(const StripData& s, int bay) const
{
    if (s.freeStrip) return false;
    if (m_view == VIEW_TWR) return s.arrival && (bay == BAY_GNDIN || bay == BAY_TAXIIN);
    if (m_view == VIEW_GND) return s.arrival && bay == BAY_TAXIIN && m_board.apnOnline;
    if (m_view == VIEW_APN) return !s.arrival && bay == BAY_TOGND;
    return false;
}

// Stationsauswahl (Dropdown): nur die Station. North / Combined / South waehlt man daneben.
std::wstring StripWindow::StationLabel() const
{
    return ViewOf(m_view, m_board).name;
}

void StripWindow::BuildStationItems()
{
    m_stationItems.clear();
    m_stationItems.push_back({ VIEW_DEL, -1, L"Delivery" });
    if (m_board.hasApron) m_stationItems.push_back({ VIEW_APN, -1, L"Apron" });
    m_stationItems.push_back({ VIEW_GND, -1, L"Ground" });
    m_stationItems.push_back({ VIEW_TWR, -1, L"Tower" });
    m_stationItems.push_back({ VIEW_ALL, -1, L"All Bays" });
}

// Bereich bei getrennten Pisten: "North 25R", "Combined", "South 25L"
std::wstring StripWindow::AreaLabel(int group) const
{
    if (group != RWY_A && group != RWY_B) return L"Combined";
    std::wstring n = GroupEn(group);
    if (n == L"NORTH") n = L"North"; else if (n == L"SOUTH") n = L"South";
    // Apron: nur Combined / South / North, ohne Pistennamen
    if (m_view != VIEW_APN && !m_board.rwyActive[group].empty()) n += L" " + W(m_board.rwyActive[group]);
    return n;
}

void StripWindow::DrawViewMenu(HDC hdc, int width, int height)
{
    BuildStationItems();   // jedes Mal: Platz ohne Apron zeigt keine Apron-Ansicht
    int itemH = Sx(48), gap = Sx(4), pad = Sx(8);
    int w = Sx(300);
    for (const StationItem& it : m_stationItems) w = std::max(w, TextWidth(hdc, it.label, m_fButton) + Sx(48));
    int h = pad * 2 + (int)m_stationItems.size() * (itemH + gap) - gap;
    int x = std::min((int)m_viewBtn.left, width - w - Sx(8));
    x = std::max(Sx(8), x);
    int y = m_viewBtn.bottom + Sx(4);
    if (y + h > height - Sx(8)) y = std::max(Sx(8), height - Sx(8) - h);
    m_viewMenuCard = Rc(x, y, x + w, y + h);
    RoundBox(hdc, m_viewMenuCard, Sx(12), col::card, col::cardBorder);
    for (size_t i = 0; i < m_stationItems.size(); ++i)
    {
        const StationItem& it = m_stationItems[i];
        bool cur = it.view == m_view;
        RECT r = Rc(x + pad, y + pad + (int)i * (itemH + gap), x + w - pad, y + pad + (int)i * (itemH + gap) + itemH);
        COLORREF bg = cur ? kBayAccent[std::max(0, ViewOf(it.view, m_board).accent)] : col::button;
        if (it.filter == RWY_A || it.filter == RWY_B) bg = cur ? kRwyAccent[it.filter] : col::button;
        DrawButton(hdc, r, it.label, bg, cur ? col::onAccent : col::text, A_VIEW_BASE + (int)i, false);
    }
}

// ---------------------------------------------------------------------------
// Tastenfeld direkt am Streifen: nur die moeglichen Werte.
//  Squawk: 0-7, OK, C.  Hold Short / Intersection: erst Buchstabe (V), dann Rest (1, 2, C ...)
// ---------------------------------------------------------------------------
static void KpSplit(const std::string& o, std::string& pre, std::string& suf)
{
    if (o.size() >= 2 && isalpha((unsigned char)o[0])) { pre = o.substr(0, 1); suf = o.substr(1); }
    else { pre = o; suf.clear(); }
}

static std::string HhmmssStr(long long t);   // Zeitstrahl (unten)
static std::wstring Hhmm(long long t);

void StripWindow::OpenKeypad(int kind, const std::string& cs, POINT at)
{
    const StripData* s = FindStrip(cs);
    if (!s || !m_host) return;
    m_pickOpen = false;
    m_kpKind = kind;
    m_kpCs = cs;
    m_kpText.clear();
    m_kpGroup.clear();
    m_kpOpts.clear();
    m_kpAnchor = Rc(at.x, at.y, at.x + 1, at.y + 1);
    m_kpAble = false;
    if (kind == KP_SQUAWK) m_kpOpts = { "1", "2", "3", "4", "5", "6", "7", "0" };
    else if (kind == KP_HDG) m_kpOpts = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "0" };
    else if (kind == KP_HL) m_kpOpts = { "OFF", "YELLOW", "GREEN", "BLUE", "RED", "PURPLE" };
    else if (kind == KP_PB) m_kpOpts = { "N", "E", "S", "W" };
    else if (kind == KP_TOBT) m_kpOpts = { "NOW", "+5", "+10", "+15", "+20", "+30" };
    else if (kind == KP_TLSEQ) m_kpOpts = { "-2", "-1", "+1", "+2", "+5", "NOW" };   // Reihenfolge im Zeitstrahl
    else if (kind == KP_HS) m_kpOpts = m_host->FieldOptions(cs, F_HS);
    else
    {
        // Intersections der Piste des Fliegers (unbekannt: beide)
        int ig = IntersectionGroup(m_board.profile, s->arrival ? s->arrRwy : s->rwy, s->rwyGroup);
        std::string li = ig == RWY_A ? m_board.opt.intNorth : ig == RWY_B ? m_board.opt.intSouth
                                                                         : m_board.opt.intNorth + " " + m_board.opt.intSouth;
        std::string tok;
        for (char c : li + " ")
        {
            if (c == ' ' || c == ',' || c == ';') { if (!tok.empty()) m_kpOpts.push_back(tok); tok.clear(); }
            else tok += (char)toupper((unsigned char)c);
        }
    }
    Invalidate();
}

// Heading: genau drei Ziffern, 001 bis 360
static bool HdgValid(const std::string& t)
{
    if (t.size() != 3) return false;
    int v = atoi(t.c_str());
    return v >= 1 && v <= 360;
}

void StripWindow::KeypadKey(int action)
{
    if (m_kpKind == KP_NONE) return;
    std::string cs = m_kpCs;
    int kind = m_kpKind;
    auto commit = [&](const std::string& v) {
        m_kpKind = KP_NONE;
        if (!m_host) return;
        if (kind == KP_SQUAWK) m_host->SetField(cs, F_SQUAWK, v);
        else if (kind == KP_HDG) m_host->SetField(cs, F_HDG, v);
        else if (kind == KP_HS) m_host->SetHoldShort(cs, v);
        else if (kind == KP_PB) m_host->Command(".strips pushdir " + cs + " " + (v.empty() ? std::string("NONE") : v));
        else if (kind == KP_TOBT) { if (!v.empty()) m_host->Command(".strips tobt " + cs + " " + v); }
        else if (kind == KP_TLSEQ)
        {
            // Basis: die angezeigte Zeit des Labels (sonst jetzt), dann Minuten vor bzw. zurueck, nie vor jetzt
            long long now = (long long)std::time(nullptr), base = now;
            for (const TlItem& it : m_tlItems)
                if (it.cs == cs && it.t > 0) base = it.t;
            if (v.empty()) { m_host->Command(".strips tlplan " + cs + " NONE"); return; }
            long long t = v == "NOW" ? now : base + atoi(v.c_str()) * 60LL;
            t = (std::max(now, t) + 5) / 10 * 10;
            m_host->Command(".strips tlplan " + cs + " " + HhmmssStr(t));
        }
        else if (kind == KP_HL)
        {
            static const char* names[6] = { "OFF", "YELLOW", "GREEN", "BLUE", "RED", "PURPLE" };
            int k = 0;
            for (int i = 0; i < 6; ++i) if (v == names[i]) k = i;
            m_host->Command(".strips hl " + cs + " " + std::to_string(k));
        }
        else m_host->SetIntersection(cs, v, !m_kpAble);
    };
    if (action == A_KP_CANCEL) m_kpKind = KP_NONE;
    else if (action == A_KP_BACK)
    {
        if (kind == KP_SQUAWK || kind == KP_HDG) { if (!m_kpText.empty()) m_kpText.pop_back(); }
        else if (!m_kpGroup.empty()) m_kpGroup.clear();
        else m_kpKind = KP_NONE;
    }
    else if (action == A_KP_NONE) commit(std::string());
    else if (action == A_KP_AUTO) { if (kind == KP_SQUAWK) commit("AUTO"); }
    else if (action == A_KP_ABLE) { if (kind == KP_INT) m_kpAble = !m_kpAble; }
    else if (action == A_KP_FREE)
    {
        m_kpKind = KP_NONE;
        if (kind == KP_HS) OpenHsEditor(cs);
        else if (kind == KP_INT) OpenIntEditor(cs);
    }
    else if (action == A_KP_OK)
    {
        if (kind == KP_SQUAWK && m_kpText.size() == 4) commit(m_kpText);
        if (kind == KP_HDG && HdgValid(m_kpText)) commit(m_kpText);
    }
    else if (action >= A_KP_BASE)
    {
        int i = action - A_KP_BASE;
        if (i < 0 || i >= (int)m_kpKeys.size()) return;
        const std::string& v = m_kpKeys[i];
        if (kind == KP_SQUAWK) { if (m_kpText.size() < 4) m_kpText += v; }
        else if (kind == KP_HDG)
        {
            // drei Ziffern 001-360: die erste nur 0-3, nach "36" nur noch 0
            std::string t = m_kpText + v;
            if (t.size() <= 3 && t[0] <= '3' && !(t.size() >= 2 && t[0] == '3' && t[1] > '6') &&
                !(t.size() == 3 && t.compare(0, 2, "36") == 0 && t[2] != '0') && t != "000")
                m_kpText = t;
        }
        else if (!v.empty() && v[0] == '#') m_kpGroup = v.substr(1);
        else commit(v);
    }
    Invalidate();
}

void StripWindow::DrawKeypad(HDC hdc, int width, int height)
{
    const StripData* s = FindStrip(m_kpCs);
    if (!s) { m_kpKind = KP_NONE; return; }
    // Tasten der aktuellen Ebene
    std::vector<std::wstring> labels;
    m_kpKeys.clear();
    if (m_kpKind == KP_SQUAWK || m_kpKind == KP_HDG || m_kpKind == KP_HL || m_kpKind == KP_PB || m_kpKind == KP_TOBT ||
        m_kpKind == KP_TLSEQ)
    {
        for (const std::string& o : m_kpOpts) { m_kpKeys.push_back(o); labels.push_back(W(o)); }
    }
    else if (m_kpGroup.empty())
    {
        std::vector<std::string> order;
        std::map<std::string, std::vector<std::string>> groups;
        for (const std::string& o : m_kpOpts)
        {
            std::string pre, suf;
            KpSplit(o, pre, suf);
            if (!groups.count(pre)) order.push_back(pre);
            groups[pre].push_back(o);
        }
        for (const std::string& g : order)
        {
            const std::vector<std::string>& m = groups[g];
            if (m.size() == 1) { m_kpKeys.push_back(m[0]); labels.push_back(W(m[0])); }
            else { m_kpKeys.push_back("#" + g); labels.push_back(W(g) + L" \x2026"); }
        }
    }
    else
    {
        // Reihenfolge: Ziffern aufsteigend, dann Buchstaben (V: 1, 2, 3, 4, C)
        std::vector<std::pair<std::string, std::string>> mem;
        for (const std::string& o : m_kpOpts)
        {
            std::string pre, suf;
            KpSplit(o, pre, suf);
            if (pre != m_kpGroup) continue;
            mem.push_back({ o, suf });
        }
        std::stable_sort(mem.begin(), mem.end(), [](const std::pair<std::string, std::string>& a,
                                                    const std::pair<std::string, std::string>& b) {
            bool da = !a.second.empty() && isdigit((unsigned char)a.second[0]);
            bool db = !b.second.empty() && isdigit((unsigned char)b.second[0]);
            if (da != db) return da;
            if (da) return atoi(a.second.c_str()) < atoi(b.second.c_str());
            return false;
        });
        for (const auto& m : mem)
        {
            m_kpKeys.push_back(m.first);
            labels.push_back(W(m.second.empty() ? m.first : m.second));
        }
    }

    int keyW = Sx(70), keyH = Sx(56), gap = Sx(4), pad = Sx(10), titleH = Sx(34), dispH = Sx(46);
    int n = (int)m_kpKeys.size();
    int cols = (m_kpKind == KP_SQUAWK || m_kpKind == KP_HDG) ? 3 : std::max(3, std::min(5, n));
    int rows = m_kpKind == KP_SQUAWK ? 3 : m_kpKind == KP_HDG ? 4 : std::max(1, (n + cols - 1) / cols);
    int cardW = 2 * pad + cols * keyW + (cols - 1) * gap;
    int cardH = titleH + pad + dispH + gap + rows * (keyH + gap) + keyH + pad;
    int x = std::max(Sx(6), std::min((int)m_kpAnchor.left - cardW / 2, width - cardW - Sx(6)));
    int y = m_kpAnchor.top + Sx(26);
    if (y + cardH > height - Sx(6)) y = m_kpAnchor.top - Sx(26) - cardH;
    y = std::max(Sx(6), std::min(y, height - cardH - Sx(6)));
    m_kpCard = Rc(x, y, x + cardW, y + cardH);
    // flaches Feld mit Kopfzeile wie die Bays
    Fill(hdc, Rc(x + Sx(3), y + Sx(3), x + cardW + Sx(3), y + cardH + Sx(3)), col::shadow);
    RoundBox(hdc, m_kpCard, 0, col::card, col::cardBorder);
    Fill(hdc, Rc(x + 1, y + 1, x + cardW - 1, y + titleH), col::bayHeader);
    std::wstring title = m_kpKind == KP_SQUAWK ? L"SQUAWK" : m_kpKind == KP_HDG ? L"HEADING" : m_kpKind == KP_HS ? L"HOLD SHORT"
                       : m_kpKind == KP_HL ? L"COLOUR" : m_kpKind == KP_PB ? L"PUSH (NOSE)"
                       : m_kpKind == KP_TOBT ? L"TOBT TO vACDM" : m_kpKind == KP_TLSEQ ? L"SEQUENCE" : L"INTERSECTION";
    if (m_kpKind == KP_INT && m_kpAble) title = L"ABLE FOR";
    Text(hdc, Rc(x + pad, y, x + cardW - pad, y + titleH), title + L"  " + W(m_kpCs), m_fHeader, col::text);

    // Anzeige: Squawk-Ziffern, sonst aktueller Wert bzw. gewaehlter Buchstabe
    RECT disp = Rc(x + pad, y + titleH + pad, x + cardW - pad, y + titleH + pad + dispH);
    RoundBox(hdc, disp, 0, col::input, col::cardBorder);
    std::wstring dv;
    if (m_kpKind == KP_SQUAWK)
    {
        for (int k = 0; k < 4; ++k) dv += k < (int)m_kpText.size() ? (wchar_t)m_kpText[k] : L'_';
        dv.insert(2, L" ");
    }
    else if (m_kpKind == KP_HDG)
    {
        if (m_kpText.empty() && s->assignedHeading > 0)
        {
            wchar_t hb[16];
            swprintf(hb, 16, L"%03d\x00B0", s->assignedHeading);
            dv = hb;
        }
        else
        {
            for (int k = 0; k < 3; ++k) dv += k < (int)m_kpText.size() ? (wchar_t)m_kpText[k] : L'_';
            dv += L"\x00B0";
        }
    }
    else if (m_kpKind == KP_PB) dv = s->pushDir.empty() ? std::wstring(L"-") : L"NOSE " + W(s->pushDir);
    else if (m_kpKind == KP_TOBT) dv = L"TSAT " + (s->tsat.empty() ? std::wstring(L"-") : W(s->tsat));
    else if (m_kpKind == KP_TLSEQ)
    {
        long long t = 0;
        for (const TlItem& it : m_tlItems)
            if (it.cs == m_kpCs) t = it.t;
        dv = (s->planUtc > 0 ? L"PLAN " : L"TIME ") + (t > 0 ? Hhmm(t) : std::wstring(L"-"));
    }
    else if (m_kpKind == KP_HL)
    {
        static const wchar_t* hn[6] = { L"-", L"YELLOW", L"GREEN", L"BLUE", L"RED", L"PURPLE" };
        dv = hn[(s->hl > 0 && s->hl < 6) ? s->hl : 0];
    }
    else if (!m_kpGroup.empty()) dv = W(m_kpGroup) + L"\x2026";
    else
    {
        const std::string& cur = m_kpKind == KP_HS ? s->holdShort : s->intersection;
        dv = cur.empty() ? std::wstring(L"-") : W(cur);
    }
    Text(hdc, disp, dv, m_fDetailTitle, col::text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    int ky = disp.bottom + gap;
    for (int i = 0; i < n; ++i)
    {
        int c = i % cols, r = i / cols;
        if (m_kpKind == KP_HDG && i == 9) { c = 1; r = 3; }   // 0 unten in der Mitte
        RECT kr = Rc(x + pad + c * (keyW + gap), ky + r * (keyH + gap), x + pad + c * (keyW + gap) + keyW, ky + r * (keyH + gap) + keyH);
        if (m_kpKind == KP_HL)
        {
            // Farbtasten in ihrer Farbe (OFF neutral), aktuelle Farbe markiert
            COLORREF bgc = i == 0 ? col::button : kHlStrong[i];
            DrawButton(hdc, kr, labels[i], bgc, i == 0 ? col::text : RGB(255, 255, 255), A_KP_BASE + i, s->hl == i);
        }
        else DrawButton(hdc, kr, labels[i], col::button, col::text, A_KP_BASE + i, false);
    }
    if (m_kpKind == KP_HDG)
    {
        RECT kr = Rc(x + pad + 2 * (keyW + gap), ky + 3 * (keyH + gap), x + pad + 2 * (keyW + gap) + keyW, ky + 3 * (keyH + gap) + keyH);
        DrawButton(hdc, kr, L"\x2190", col::buttonAlt, col::text, A_KP_BACK, false);
    }
    if (m_kpKind == KP_SQUAWK)
    {
        // letzte Taste der dritten Reihe: zuruecknehmen
        RECT kr = Rc(x + pad + 2 * (keyW + gap), ky + 2 * (keyH + gap), x + pad + 2 * (keyW + gap) + keyW, ky + 2 * (keyH + gap) + keyH);
        DrawButton(hdc, kr, L"\x2190", col::buttonAlt, col::text, A_KP_BACK, false);
    }
    int by = ky + rows * (keyH + gap);
    int right = x + cardW - pad;
    if (m_kpKind == KP_SQUAWK)
    {
        // C | Auto (TopSky) | OK
        DrawButton(hdc, Rc(x + pad, by, x + pad + keyW, by + keyH), L"C", col::danger, RGB(255, 255, 255), A_KP_CANCEL, false);
        DrawButton(hdc, Rc(x + pad + keyW + gap, by, x + pad + 2 * keyW + gap, by + keyH), L"Auto", RGB(70, 110, 190),
                   RGB(255, 255, 255), A_KP_AUTO, false);
        bool ok = m_kpText.size() == 4;
        DrawButton(hdc, Rc(x + pad + 2 * (keyW + gap), by, right, by + keyH), L"OK", ok ? col::ok : col::button,
                   ok ? RGB(255, 255, 255) : col::textDim, A_KP_OK, false);
    }
    else if (m_kpKind == KP_HDG)
    {
        // C | None (kein Heading) | OK
        DrawButton(hdc, Rc(x + pad, by, x + pad + keyW, by + keyH), L"C", col::danger, RGB(255, 255, 255), A_KP_CANCEL, false);
        DrawButton(hdc, Rc(x + pad + keyW + gap, by, x + pad + 2 * keyW + gap, by + keyH), L"None", col::buttonAlt, col::text,
                   A_KP_NONE, false);
        bool ok = HdgValid(m_kpText);
        DrawButton(hdc, Rc(x + pad + 2 * (keyW + gap), by, right, by + keyH), L"OK", ok ? col::ok : col::button,
                   ok ? RGB(255, 255, 255) : col::textDim, A_KP_OK, false);
    }
    else
    {
        // unten: zurueck (in der zweiten Ebene), Freitext, keine, C
        std::vector<std::pair<std::wstring, int>> bb;
        if (!m_kpGroup.empty()) bb.push_back({ L"\x2190", A_KP_BACK });
        if (m_kpKind == KP_INT) bb.push_back({ L"Able for", A_KP_ABLE });
        if (m_kpKind != KP_HL && m_kpKind != KP_PB && m_kpKind != KP_TOBT && m_kpKind != KP_TLSEQ) bb.push_back({ L"Text", A_KP_FREE });
        if (m_kpKind != KP_TOBT) bb.push_back({ L"None", A_KP_NONE });
        bb.push_back({ L"C", A_KP_CANCEL });
        int k = (int)bb.size();
        int bw = (right - (x + pad) - (k - 1) * gap) / k;
        for (int j = 0; j < k; ++j)
        {
            RECT br = Rc(x + pad + j * (bw + gap), by, j + 1 == k ? right : x + pad + j * (bw + gap) + bw, by + keyH);
            bool cancel = bb[j].second == A_KP_CANCEL;
            bool able = bb[j].second == A_KP_ABLE && m_kpAble;   // aktiv: die naechste Wahl ist nur "able for"
            DrawButton(hdc, br, bb[j].first, cancel ? col::danger : able ? RGB(250, 180, 70) : col::buttonAlt,
                       cancel ? RGB(255, 255, 255) : col::text, bb[j].second, able);
        }
    }
}

// ---------------------------------------------------------------------------
// Einstellungsfenster: jede Schaltflaeche fuehrt einen ".strips ..."-Befehl aus, damit
// Fenster und Kommandozeile immer dasselbe tun
// ---------------------------------------------------------------------------
void StripWindow::DrawSettings(HDC hdc, int width, int height)
{
    Dim(hdc, width, height, col::dimAlpha);
    struct Opt { std::wstring label; std::string cmd; bool active; };
    struct Row { std::wstring title; std::vector<Opt> opts; };
    const Board::Options& o = m_board.opt;
    auto onoff = [](const std::string& base, bool on) {
        return std::vector<Opt>{ { L"On", base + " on", on }, { L"Off", base + " off", !on } };
    };
    auto shortTxt = [](const std::string& v) { return v.size() > 22 ? W(v.substr(0, 21)) + L"\x2026" : W(v.empty() ? std::string("-") : v); };
    int zoomPct = (int)(m_zoom * 100.0 + 0.5);
    std::vector<Row> rows;
    // Platz-Profil (Berlin, Leipzig, Hannover; Auto = nach Callsign / aktivem Platz)
    rows.push_back({ L"Airport", { { L"Auto (" + W(m_board.profile) + L")", ".strips profile auto", !m_board.profileForced },
                                   { L"EDDB", ".strips profile EDDB", m_board.profileForced && m_board.profile == "EDDB" },
                                   { L"EDDP", ".strips profile EDDP", m_board.profileForced && m_board.profile == "EDDP" },
                                   { L"EDDV", ".strips profile EDDV", m_board.profileForced && m_board.profile == "EDDV" } } });
    // Top-down: unbesetzte untere Stationen (DEL, Apron, Ground) in der eigenen Ansicht mitmachen
    rows.push_back({ L"Top-down", { { L"On", ".strips topdown on", m_board.topDown },
                                    { L"Off", ".strips topdown off", !m_board.topDown } } });
    rows.push_back({ L"Theme", { { L"Dark", ".strips theme dark", !m_light }, { L"Light", ".strips theme light", m_light } } });
    rows.push_back({ L"Layout", { { L"Auto", ".strips layout auto", m_layout == 0 }, { L"Columns", ".strips layout columns", m_layout == 1 },
                                  { L"Grid", ".strips layout grid", m_layout == 2 } } });
    rows.push_back({ L"Size", { { L"\x2212", ".strips zoom " + std::to_string(std::max(60, zoomPct - 10)), false },
                                { std::to_wstring(zoomPct) + L" %", "", false },
                                { L"+", ".strips zoom " + std::to_string(std::min(250, zoomPct + 10)), false } } });
    rows.push_back({ L"Strip window", { { L"On top", std::string(".strips ontop ") + (m_topmost ? "off" : "on"), m_topmost },
                                  { L"Fullscreen", std::string(".strips fullscreen ") + (m_fullscreen ? "off" : "on"), m_fullscreen } } });
    // StripBay Remote: auf einem weiteren Windows-Geraet im Netz
    if (m_remoteClient)
        rows.push_back({ L"StripBay Remote", { { m_remoteInfo, "", false }, { L"Disconnect", ".strips remote disconnect", false } } });
    else
    {
        rows.push_back({ L"Remote (other PC)", { { L"On", ".strips remote on", m_board.remoteOn },
                                                 { L"Off", ".strips remote off", !m_board.remoteOn },
                                                 { L"New code", ".strips remote code new", false } } });
        if (m_board.remoteOn)
        {
            rows.push_back({ L"Remote pairing", { { L"Code " + W(m_board.remoteCode), "", false },
                                                  { shortTxt(m_board.remoteAddr.empty() ? std::string("?") : m_board.remoteAddr), "", false },
                                                  { std::to_wstring(m_board.remoteClients) + L" connected", "", false } } });
        }
    }
    rows.push_back({ L"Split runways", onoff(".strips runways", o.splitEnabled) });
    rows.push_back({ L"Taxi in after vacating", onoff(".strips vacate", o.autoVacate) });
    // Zustaendigkeit / Uebergaben: aus = niemand wird als zustaendig eingetragen, die Streifen folgen nur dem Ground State
    rows.push_back({ L"Responsibility / handover", onoff(".strips responsibility", o.respOn) });
    if (o.respOn)
    {
        // Uebergabe an mich: erst nur farbig, dann Blinken, noch spaeter einmal ein Ton
        rows.push_back({ L"Handover blinks after", { { L"10 s", ".strips offerblink 10", o.offerBlink == 10 },
                                                     { L"30 s", ".strips offerblink 30", o.offerBlink == 30 },
                                                     { L"60 s", ".strips offerblink 60", o.offerBlink == 60 } } });
        rows.push_back({ L"Handover sound after", { { L"60 s", ".strips offerremind 60", o.offerRemind == 60 },
                                                    { L"90 s", ".strips offerremind 90", o.offerRemind == 90 },
                                                    { L"120 s", ".strips offerremind 120", o.offerRemind == 120 },
                                                    { L"Off", ".strips offerremind 0", o.offerRemind == 0 } } });
    }
    rows.push_back({ L"Drag onto runway sets", { { L"Line-up", ".strips takeoff lineup", !o.takeoffDepa },
                                                 { L"Takeoff", ".strips takeoff depa", o.takeoffDepa } } });
    rows.push_back({ L"Arrivals appear from", { { L"\x2212", ".strips arrivals " + std::to_string(std::max(3, o.arrNm - 5)), false },
                                                { std::to_wstring(o.arrNm) + L" NM", "", false },
                                                { L"+", ".strips arrivals " + std::to_string(std::min(100, o.arrNm + 5)), false } } });
    rows.push_back({ L"Weather (METAR / ATIS)", onoff(".strips weather", o.wx) });
    rows.push_back({ L"vACDM (TSAT)", onoff(".strips cdm", o.vacdm) });
    {
        std::vector<Opt> v = onoff(".strips cpdlc", o.dlOn);
        v.push_back({ o.dlCode ? L"Logon code \x2713" : L"Logon code\x2026", "edit:.strips cpdlc code \tHOPPIE LOGON CODE\t", false });
        rows.push_back({ L"CPDLC (Hoppie)", v });
    }
    rows.push_back({ L"CPDLC station", { { shortTxt(o.dlStation), "", false },
                                         { L"Change\x2026", "edit:.strips cpdlc station \tCPDLC STATION (auto = airport)\t" + o.dlStation, false } } });
    rows.push_back({ L"CPDLC options", { { L"Auto logon", std::string(".strips cpdlc autologon ") + (o.dlAutoLogon ? "off" : "on"), o.dlAutoLogon },
                                         { L"Sound", std::string(".strips cpdlc sound ") + (o.dlSound ? "off" : "on"), o.dlSound } } });
    rows.push_back({ L"Next bay button", onoff(".strips nextbtn", o.nextBtn) });
    rows.push_back({ L"Tower timeline (DFS)", onoff(".strips timeline", o.timeline) });
    rows.push_back({ L"Timeline ahead", { { L"\x2212", ".strips timeline range " + std::to_string(o.tlPast) + " " + std::to_string(std::max(3, o.tlFuture - 1)), false },
                                          { std::to_wstring(o.tlFuture) + L" min  (past " + std::to_wstring(o.tlPast) + L")", "", false },
                                          { L"+", ".strips timeline range " + std::to_string(o.tlPast) + " " + std::to_string(std::min(60, o.tlFuture + 1)), false } } });
    rows.push_back({ L"Timeline: no TTOT", { { L"Estimate (EST)", ".strips timeline estimate on", o.tlEst },
                                             { L"List", ".strips timeline estimate off", !o.tlEst } } });
    rows.push_back({ L"Timeline colours", { { L"Day", ".strips timeline night off", !o.tlNight },
                                            { L"Night", ".strips timeline night on", o.tlNight } } });

    int pad = Sx(18), gap = Sx(6), rowH = Sx(50), titleH = Sx(48), btnH = Sx(50);
    int n = (int)rows.size();
    int perCol = std::max(4, (height - Sx(40) - 2 * pad - titleH - btnH - Sx(12)) / (rowH + gap));
    int ncols = std::min(3, (n + perCol - 1) / perCol);
    perCol = (n + ncols - 1) / ncols;
    int colW = std::min(Sx(660), (width - Sx(40) - 2 * pad - (ncols - 1) * Sx(24)) / ncols);
    int cardW = 2 * pad + ncols * colW + (ncols - 1) * Sx(24);
    int cardH = 2 * pad + titleH + perCol * (rowH + gap) + Sx(8) + btnH;
    int cx = std::max(Sx(8), (width - cardW) / 2), cy = std::max(Sx(8), (height - cardH) / 2);
    m_setCard = Rc(cx, cy, cx + cardW, cy + cardH);
    RoundBox(hdc, m_setCard, Sx(14), col::card, col::cardBorder);
    Text(hdc, Rc(cx + pad, cy + pad, cx + cardW - pad, cy + pad + Sx(36)), L"SETTINGS", m_fTitle, col::text);
    Text(hdc, Rc(cx + pad, cy + pad, cx + cardW - pad, cy + pad + Sx(36)), L"same as .strips commands, saved with EuroScope",
         m_fDetailLabel, col::textDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    m_setCmds.clear();
    for (int i = 0; i < n; ++i)
    {
        int c = i / perCol, r = i % perCol;
        int x = cx + pad + c * (colW + Sx(24));
        int y = cy + pad + titleH + r * (rowH + gap);
        int labW = colW * 40 / 100;
        Text(hdc, Rc(x, y, x + labW - Sx(6), y + rowH), rows[i].title, m_fHeader, col::text);
        int k = (int)rows[i].opts.size();
        int bx = x + labW, bw = (colW - labW - (k - 1) * Sx(4)) / std::max(1, k);
        for (int j = 0; j < k; ++j)
        {
            const Opt& op = rows[i].opts[j];
            RECT rb = Rc(bx + j * (bw + Sx(4)), y + Sx(3), bx + j * (bw + Sx(4)) + bw, y + rowH - Sx(3));
            if (op.cmd.empty())
            {
                RoundBox(hdc, rb, Sx(8), col::input, col::cardBorder);
                Text(hdc, rb, op.label, m_fButton, col::text, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                continue;
            }
            int act = A_SET_BASE + (int)m_setCmds.size();
            m_setCmds.push_back(op.cmd);
            DrawButton(hdc, rb, op.label, op.active ? col::ok : col::button, op.active ? RGB(255, 255, 255) : col::text, act, false);
        }
    }
    int by = cy + cardH - pad - btnH;
    DrawButton(hdc, Rc(cx + cardW - pad - Sx(220), by, cx + cardW - pad, by + btnH), L"Close", col::button, col::text,
               A_SET_CLOSE, false);
}

void StripWindow::DrawAreaMenu(HDC hdc, int width, int height)
{
    const int acts[3] = { A_AREA_BOTH, A_AREA_A, A_AREA_B };
    const int grp[3] = { 0, RWY_A, RWY_B };
    int itemH = Sx(48), gap = Sx(4), pad = Sx(8);
    int w = std::max(Sx(220), (int)(m_areaBtn.right - m_areaBtn.left));
    for (int k = 0; k < 3; ++k) w = std::max(w, TextWidth(hdc, AreaLabel(grp[k]), m_fButton) + Sx(48));
    int h = pad * 2 + 3 * (itemH + gap) - gap;
    int x = std::max(Sx(8), std::min((int)m_areaBtn.right - w, width - w - Sx(8)));
    int y = m_areaBtn.bottom + Sx(4);
    if (y + h > height - Sx(8)) y = std::max(Sx(8), height - Sx(8) - h);
    RECT card = Rc(x, y, x + w, y + h);
    RoundBox(hdc, card, Sx(12), col::card, col::cardBorder);
    for (int k = 0; k < 3; ++k)
    {
        bool cur = m_rwyFilter == grp[k];
        RECT r = Rc(x + pad, y + pad + k * (itemH + gap), x + w - pad, y + pad + k * (itemH + gap) + itemH);
        COLORREF bg = cur ? (grp[k] ? kRwyAccent[grp[k]] : RGB(120, 130, 140)) : col::button;
        DrawButton(hdc, r, AreaLabel(grp[k]), bg, cur ? col::onAccent : col::text, acts[k], false);
    }
}

// Pistengruppe englisch: "NORD" -> "NORTH", "SUED" -> "SOUTH", sonst wie eingestellt
std::wstring StripWindow::GroupEn(int group) const
{
    if (group != RWY_A && group != RWY_B) return std::wstring();
    std::string n;
    for (char c : m_board.rwyName[group]) n += (char)toupper((unsigned char)c);
    if (n == "NORD" || n == "N" || n == "NORTH") return L"NORTH";
    if (n == "SUED" || n == "SUD" || n == "S" || n == "SOUTH" || n == "S\xDC" "D") return L"SOUTH";
    return W(m_board.rwyName[group]);
}

// Spaltenname; Pistenspalten mit der aktiven Piste (Betriebsrichtung aus EuroScope)
std::wstring StripWindow::ColTitle(int col) const
{
    if (col < 0 || col >= (int)m_cols.size()) return std::wstring();
    const ColumnDef& d = ViewOf(m_view, m_board).cols[m_cols[col].def];
    std::wstring t = d.title;
    int g = m_cols[col].group;
    if ((g == RWY_A || g == RWY_B) && !d.rwyMatch)
        t += L" " + (m_board.rwyActive[g].empty() ? GroupEn(g) : W(m_board.rwyActive[g]));
    return t;
}

int StripWindow::ColAccent(int col) const
{
    if (col < 0 || col >= (int)m_cols.size()) return 0;
    return ViewOf(m_view, m_board).cols[m_cols[col].def].accent;
}

// Rollende Ankunft, die man selbst fuehrt: Tipp auf STAND = geparkt
bool StripWindow::CanPark(const StripData& s, int bay) const
{
    if (s.freeStrip || !s.arrival || m_view == VIEW_TWR || m_view == VIEW_DEL) return false;
    // nur am Gate angekommen und unter 3 kt
    return s.atGate && (bay == BAY_GNDIN || bay == BAY_TAXIIN) && !HandedOver(s, bay);
}

bool StripWindow::SplitColumns() const
{
    for (const Column& c : m_cols)
        if (c.sub != 0) return true;
    return false;
}

bool StripWindow::ColumnHas(int col, const std::string& cs) const
{
    if (col < 0 || col >= (int)m_cols.size()) return false;
    for (const StripRef& r : m_cols[col].strips)
        if (m_board.bays[r.bay][r.idx].callsign == cs) return true;
    return false;
}

// Darf dieser Streifen in die Spalte? Bay muss passen, bei Pistenspalten auch
// die Piste im Tag. Eine andere Piste vergibt man im Tag, nicht hier.
bool StripWindow::AcceptsDrop(int col, const StripData& s) const
{
    if (col < 0 || col >= (int)m_cols.size()) return false;
    const ColumnDef& def = ViewOf(m_view, m_board).cols[m_cols[col].def];
    if (s.freeStrip) return true;   // Notiz- und Designator-Strips duerfen ueberallhin
    if (!CanDrop(s.arrival, (s.arrival && def.dropArr >= 0) ? def.dropArr : def.dropBay)) return false;
    if ((def.kind == 1 && s.arrival) || (def.kind == 2 && !s.arrival)) return false;
    // gesperrte Piste: kein Flieger auf die Piste
    if ((def.dropBay == BAY_RUNWAY || def.dropArr == BAY_RUNWAY) && RwyStateOf(col) == 2) return false;
    int g = m_cols[col].group;
    // getrennte Pisten: auf die andere Seite ziehen wechselt die Piste im Flugplan (2.0.69)
    return g == RWY_NONE || s.rwyGroup == RWY_NONE || s.rwyGroup == g || (m_board.rwySplit && !def.rwyMatch);
}

int StripWindow::RwyStateOf(int col) const
{
    if (col < 0 || col >= (int)m_cols.size()) return 0;
    std::string key;
    for (wchar_t ch : ColTitle(col)) key += (char)(ch < 128 ? ch : '?');
    auto rs = m_board.rwyState.find(key);
    return rs == m_board.rwyState.end() ? 0 : rs->second;
}

// "NORD 24R" (aktive Piste der Gruppe) oder nur "NORD"
std::wstring StripWindow::GroupLabel(int group) const
{
    if (group != RWY_A && group != RWY_B) return std::wstring();
    std::wstring l = GroupEn(group);
    if (!m_board.rwyActive[group].empty()) l += L" " + W(m_board.rwyActive[group]);
    return l;
}

bool StripWindow::PickScrollList() const
{
    return m_pickOpen && (m_pickField == F_CFL || m_pickField == F_RFL) && m_pickOpts.size() <= 100;
}

void StripWindow::ClampPickScroll()
{
    int maxFirst = std::max(0, (int)m_pickOpts.size() - m_pickRowsVis);
    m_pickScroll = std::max(0, std::min(m_pickScroll, maxFirst));
}

bool StripWindow::BayInView(int bay) const
{
    const ViewDef& v = ViewOf(m_view, m_board);
    for (int c = 0; c < v.n; ++c)
        for (int k = 0; k < v.cols[c].nb; ++k)
            if (v.cols[c].bays[k] == bay) return true;
    return false;
}

int StripWindow::ColumnOf(const std::string& cs) const
{
    for (int c = 0; c < (int)m_cols.size(); ++c)
        for (const StripRef& r : m_cols[c].strips)
            if (m_board.bays[r.bay][r.idx].callsign == cs) return c;
    return -1;
}

const StripData& StripWindow::StripAt(int col, int i) const
{
    const StripRef& r = m_cols[col].strips[i];
    return m_board.bays[r.bay][r.idx];
}

WindowSettings StripWindow::GetSettings() const
{
    WindowSettings s;
    s.layout = m_layout;
    s.view = m_view;
    s.rwyFilter = m_rwyFilter;
    s.light = m_light;
    s.fullscreen = m_fullscreen;
    s.topmost = m_topmost;
    s.zoom = m_zoom;
    s.visible = IsVisible();
    s.normalRect = m_normalRect;
    if (m_hwnd && !m_fullscreen)
    {
        DpiScope dpi;
        RECT r;
        if (GetWindowRect(m_hwnd, &r)) s.normalRect = r;
    }
    return s;
}

void StripWindow::Invalidate()
{
    if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
}

// ---------------------------------------------------------------------------
// Skalierung und Schriften
// ---------------------------------------------------------------------------
int StripWindow::Sx(double v) const
{
    return (int)std::lround(v * m_scale);
}

void StripWindow::UpdateScale()
{
    m_scale = (m_dpi / 96.0) * m_zoom;
    DeleteFonts();
    CreateFonts();
}

static HFONT MakeFont(int px, int weight, const wchar_t* face)
{
    return CreateFontW(-px, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE, face);
}

void StripWindow::CreateFonts()
{
    // Bahnschrift ist die DIN-Schrift von Windows 10/11, passt gut zu echten Streifen
    const wchar_t* ui = L"Segoe UI";
    const wchar_t* strip = L"Bahnschrift";
    m_fTitle        = MakeFont(Sx(17), FW_SEMIBOLD, ui);
    m_fHeader       = MakeFont(Sx(14), FW_BOLD, ui);
    m_fCallsign     = MakeFont(Sx(22), FW_BOLD, strip);
    m_fCallsignSmall= MakeFont(Sx(18), FW_BOLD, strip);
    m_fStrip        = MakeFont(Sx(16), FW_NORMAL, strip);
    m_fStripSmall   = MakeFont(Sx(15), FW_NORMAL, strip);
    m_fStripTiny    = MakeFont(Sx(12), FW_NORMAL, strip);
    m_fLabel        = MakeFont(Sx(10), FW_NORMAL, ui);
    m_fDetailTitle  = MakeFont(Sx(30), FW_BOLD, strip);
    m_fDetailLabel  = MakeFont(Sx(12), FW_NORMAL, ui);
    m_fDetailValue  = MakeFont(Sx(20), FW_NORMAL, strip);
    m_fButton       = MakeFont(Sx(16), FW_SEMIBOLD, ui);
}

void StripWindow::DeleteFonts()
{
    HFONT* all[] = { &m_fTitle, &m_fHeader, &m_fCallsign, &m_fCallsignSmall, &m_fStrip, &m_fStripSmall,
                     &m_fStripTiny, &m_fLabel,
                     &m_fDetailTitle, &m_fDetailLabel, &m_fDetailValue, &m_fButton };
    for (HFONT* f : all)
    {
        if (*f) DeleteObject(*f);
        *f = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------
std::string StripWindow::SlotKey(int slot) const
{
    const ViewDef& vd = ViewOf(m_view, m_board);
    std::string k;
    for (const wchar_t* c = vd.name ? vd.name : L""; *c; ++c) k += (*c < 128 && *c != ' ') ? (char)*c : '_';
    return k + "#" + std::to_string(slot);
}

int StripWindow::SlotFactor(int slot) const
{
    std::string k = SlotKey(slot);
    auto lv = m_colWLive.find(k);
    if (lv != m_colWLive.end()) return lv->second;
    auto it = m_board.opt.colWidths.find(k);
    return it == m_board.opt.colWidths.end() ? 100 : std::max(30, std::min(400, it->second));
}

int StripWindow::StripPitch() const
{
    return m_stripH + m_gap;
}

RECT StripWindow::SlotRect(int col, int slot) const
{
    const BayRect& br = m_cols[col].rect;
    int right = br.body.right - m_pad - (br.overflow ? Sx(14) : 0);
    int top = br.body.top + m_pad + slot * StripPitch() - m_scroll[col];
    return Rc(br.body.left + m_pad, top, right, top + m_stripH);
}

void StripWindow::ComputeLayout(int width, int height)
{
    m_width = width;
    m_height = height;
    m_stripH = Sx(66);
    m_gap = Sx(5);
    m_pad = Sx(6);

    // Schmales Fenster: zweite Zeile fuer ATIS und METAR
    m_wxRow = width < Sx(1600) && (!m_board.metar.empty() || !m_board.atis.empty() || !m_board.wxAirport.empty());
    int topH = m_wxRow ? Sx(80) : Sx(48);
    m_topBar = Rc(0, 0, width, topH);

    int margin = Sx(8), gap = Sx(8);
    int left = margin, right = width - margin;
    int top = topH + margin, bottom = height - margin;
    int availW = right - left;

    // Spalten der aktuellen Ansicht
    int n = (int)m_cols.size();
    if (n == 0) return;
    int headerH = Sx(40);

    // Plaetze: jede Spaltendefinition belegt einen Platz, ausser dem unteren Teil einer
    // Doppelspalte (stack 2). Pistenspalten teilen ihren Platz (oben erste Piste, unten
    // zweite), Doppelspalten oben 40 %, unten der Rest. Breiten nach Gewicht.
    const ViewDef& vd = ViewOf(m_view, m_board);
    std::vector<int> slotOf(vd.n, 0);
    int nslots = 0;
    for (int d = 0; d < vd.n; ++d)
        slotOf[d] = (vd.cols[d].stack >= 2 && d > 0) ? slotOf[d - 1] : nslots++;
    std::vector<int> slotWeight(nslots, 1);
    for (int d = 0; d < vd.n; ++d) slotWeight[slotOf[d]] = std::max(slotWeight[slotOf[d]], vd.cols[d].weight);
    // von Hand gezogene Breiten (in % des Standards)
    for (int k = 0; k < nslots; ++k) slotWeight[k] = std::max(1, slotWeight[k] * SlotFactor(k));
    // Zeitstrahl: das gemeinsame Feld braucht Breite (Abstand = Zeit) - seine Spalten zaehlen 1,5-fach
    if (TimelineActive() && m_tlAbs.size() == m_cols.size())
    {
        std::vector<char> ab(nslots, 0);
        for (int i = 0; i < (int)m_cols.size(); ++i)
            if (m_tlAbs[i]) ab[slotOf[m_cols[i].def]] = 1;
        for (int k = 0; k < nslots; ++k)
            if (ab[k]) slotWeight[k] = slotWeight[k] * 3 / 2;
    }

    int bands;
    if (SplitColumns())
    {
        if (m_layout == 1) bands = 1;
        else if (m_layout == 2) bands = 2;
        else bands = ((availW - (nslots - 1) * gap) / std::max(1, nslots) >= Sx(230)) ? 1 : 2;
    }
    else
    {
        int two = (nslots + 1) / 2;
        if (m_layout == 1) bands = 1;
        else if (m_layout == 2) bands = 2;
        else if ((availW - (nslots - 1) * gap) / std::max(1, nslots) >= Sx(300)) bands = 1;
        else if ((availW - (two - 1) * gap) / std::max(1, two) >= Sx(240)) bands = 2;
        else bands = (nslots + 1) / 2;
    }
    if (nslots < 1) return;   // keine Spalte in dieser Ansicht
    if (nslots < 2) bands = 1;
    if (TimelineActive()) bands = 1;   // Zeitstrahl: eine Reihe, die Achse ueber die ganze Hoehe
    int perBand = (nslots + bands - 1) / bands;
    int usedBands = (nslots + perBand - 1) / perBand;
    int bandH = std::max(1, (bottom - top - (usedBands - 1) * gap) / usedBands);
    std::vector<int> slotX(nslots, left), slotW(nslots, 1);
    for (int b0 = 0; b0 < nslots; b0 += perBand)
    {
        int b1 = std::min(nslots, b0 + perBand), sum = 0;
        for (int k = b0; k < b1; ++k) sum += slotWeight[k];
        int usable = std::max(1, availW - (b1 - b0 - 1) * gap), x = left;
        for (int k = b0; k < b1; ++k)
        {
            slotX[k] = x;
            slotW[k] = (k + 1 == b1) ? std::max(1, right - x) : std::max(1, usable * slotWeight[k] / std::max(1, sum));
            x += slotW[k] + gap;
        }
    }
    // Raender zwischen benachbarten Plaetzen derselben Reihe: zum Ziehen
    m_slotEdges.clear();
    m_slotWNow = slotW;
    for (int k = 0; k + 1 < nslots; ++k)
    {
        if ((k / perBand) != ((k + 1) / perBand)) continue;
        int y = top + (k / perBand) * (bandH + gap);
        int ex = slotX[k] + slotW[k];
        m_slotEdges.push_back({ Rc(ex - Sx(4), y + headerH, ex + gap + Sx(4), y + bandH), k, k + 1 });
    }
    int halfH = std::max(1, (bandH - gap) / 2);
    int topPartH = std::max(1, (bandH - gap) * 2 / 5);          // oben klein, unten gross
    int topPartBigH = std::max(1, (bandH - gap) * 7 / 10);      // oben gross, unten klein (stack 3)
    for (int i = 0; i < n; ++i)
    {
        BayRect& br = m_cols[i].rect;
        int d = m_cols[i].def;
        int slot = slotOf[d];
        int x = slotX[slot], w = slotW[slot];
        int y = top + (slot / perBand) * (bandH + gap);
        if (m_cols[i].sub == 1)            br.frame = Rc(x, y, x + w, y + halfH);
        else if (m_cols[i].sub == 2)       br.frame = Rc(x, y + bandH - halfH, x + w, y + bandH);
        else if (vd.cols[d].stack == 4 || (d + 1 < vd.n && vd.cols[d + 1].stack == 4))
        {
            // Gruppe untereinander: Hoehe nach vw
            int g0 = d, g1 = d;
            while (g0 > 0 && vd.cols[g0].stack == 4) --g0;
            while (g1 + 1 < vd.n && vd.cols[g1 + 1].stack == 4) ++g1;
            int tot = 0, before = 0, cnt = g1 - g0 + 1;
            for (int k = g0; k <= g1; ++k) { int vv = std::max(1, vd.cols[k].vw); tot += vv; if (k < d) before += vv; }
            int usable = std::max(1, bandH - (cnt - 1) * gap);
            int y0 = y + usable * before / tot + (d - g0) * gap;
            int y1 = (d == g1) ? y + bandH : y + usable * (before + std::max(1, vd.cols[d].vw)) / tot + (d - g0) * gap;
            br.frame = Rc(x, y0, x + w, y1);
        }
        else if (vd.cols[d].stack == 1)
        {
            bool bigTop = d + 1 < vd.n && vd.cols[d + 1].stack == 3;
            br.frame = Rc(x, y, x + w, y + (bigTop ? topPartBigH : topPartH));
        }
        else if (vd.cols[d].stack == 2)    br.frame = Rc(x, y + topPartH + gap, x + w, y + bandH);
        else if (vd.cols[d].stack == 3)    br.frame = Rc(x, y + topPartBigH + gap, x + w, y + bandH);
        else                               br.frame = Rc(x, y, x + w, y + bandH);
    }

    for (int i = 0; i < n; ++i)
    {
        BayRect& br = m_cols[i].rect;
        br.header = Rc(br.frame.left, br.frame.top, br.frame.right, br.frame.top + headerH);
        br.body = Rc(br.frame.left, br.header.bottom, br.frame.right, br.frame.bottom);

        int cnt = (int)m_cols[i].strips.size();
        if (m_mode == M_DRAG_STRIP)
        {
            if (i == m_dragFromCol) cnt -= 1;
            if (i == m_dropCol) cnt += 1;
        }
        br.contentHeight = cnt > 0 ? 2 * m_pad + cnt * StripPitch() - m_gap : 0;
        br.overflow = br.contentHeight > (br.body.bottom - br.body.top);
        br.track = Rc(br.body.right - Sx(13), br.body.top + Sx(6), br.body.right - Sx(5), br.body.bottom - Sx(6));
        ClampScroll(i);
    }

    // Zeitstrahl: die aufgehenden Spalten werden ein Feld, ihre eigenen Rechtecke sind leer
    m_tlPanel = Rc(0, 0, 0, 0);
    if (TimelineActive() && m_tlAbs.size() == m_cols.size())
    {
        std::vector<char> slotAbs(nslots, 0);
        for (int i = 0; i < n; ++i)
            if (m_tlAbs[i])
            {
                RECT f = m_cols[i].rect.frame;
                if (m_tlPanel.right <= m_tlPanel.left) m_tlPanel = f;
                else
                {
                    m_tlPanel.left = std::min(m_tlPanel.left, f.left); m_tlPanel.top = std::min(m_tlPanel.top, f.top);
                    m_tlPanel.right = std::max(m_tlPanel.right, f.right); m_tlPanel.bottom = std::max(m_tlPanel.bottom, f.bottom);
                }
                slotAbs[slotOf[m_cols[i].def]] = 1;
                m_cols[i].rect = BayRect();
                m_scroll[i] = 0;
            }
        // innerhalb des Feldes keine Bay-Raender zum Ziehen
        m_slotEdges.erase(std::remove_if(m_slotEdges.begin(), m_slotEdges.end(),
                                         [&](const SlotEdge& e) { return slotAbs[e.left] && slotAbs[e.right]; }),
                          m_slotEdges.end());
        LayoutTimeline();
    }
}

void StripWindow::ClampScroll(int col)
{
    if (col < 0 || col >= (int)m_cols.size()) return;
    const BayRect& br = m_cols[col].rect;
    int maxScroll = std::max(0, br.contentHeight - (int)(br.body.bottom - br.body.top));
    m_scroll[col] = std::min(std::max(0, m_scroll[col]), maxScroll);
}

const StripData* StripWindow::FindStrip(const std::string& cs, int* bayOut) const
{
    for (int b = 0; b < BAY_COUNT; ++b)
        for (const StripData& s : m_board.bays[b])
            if (s.callsign == cs)
            {
                if (bayOut) *bayOut = b;
                return &s;
            }
    return nullptr;
}

int StripWindow::HitCol(POINT p) const
{
    if (TimelineActive() && PtInRect(&m_tlPanel, p)) return TlDropCol(p);
    for (int c = 0; c < (int)m_cols.size(); ++c)
        if (PtInRect(&m_cols[c].rect.frame, p)) return c;
    return -1;
}

bool StripWindow::HitStrip(POINT p, int& col, int& index) const
{
    for (int c = 0; c < (int)m_cols.size(); ++c)
    {
        const BayRect& br = m_cols[c].rect;
        if (!PtInRect(&br.body, p)) continue;
        int rel = p.y - (br.body.top + m_pad) + m_scroll[c];
        if (rel < 0) return false;
        int i = rel / StripPitch();
        if (rel % StripPitch() >= m_stripH) return false;
        if (i >= (int)m_cols[c].strips.size()) return false;
        RECT rc = SlotRect(c, i);
        if (p.x < rc.left || p.x >= rc.right) return false;
        col = c;
        index = i;
        return true;
    }
    return false;
}

int StripWindow::HitButton(POINT p) const
{
    // Offener Editor: nur seine Tasten. Offene Details: nur deren Buttons.
    // von oben nach unten: zuletzt gezeichnete Knoepfe (Overlays) zuerst
    for (auto bi = m_buttons.rbegin(); bi != m_buttons.rend(); ++bi)
    {
        const Button& b = *bi;
        // Kopf einer Runway-Bay nur ohne offenes Overlay
        if (b.action >= A_RWYST_BASE && b.action <= A_RWYST_END &&
            (m_editing || m_kpKind != KP_NONE || m_setOpen || m_viewMenu || m_areaMenu || m_pickOpen || m_dlOpen || m_trashOpen ||
             !m_detailCs.empty()))
            continue;
        bool trashAction = b.action == A_TRASH_CLOSE || b.action == A_TRASH_ALL ||
                           (b.action >= A_RESTORE_BASE && b.action < A_FIELD_BASE);
        bool pickAction = (b.action >= A_PICK_PREV && b.action <= A_PICK_CLOSE) ||
                          b.action == A_PICK_UP || b.action == A_PICK_DOWN || b.action == A_PICK_AUTO ||
                          b.action == A_PICK_BTN0 || b.action == A_PICK_BTN1 ||
                          (b.action >= A_PICK_BASE && b.action < A_DL_BASE);
        bool dlAction = b.action >= A_DL_BASE && b.action < A_VIEW_BASE && b.action != A_DL_OPEN;
        bool viewAction = b.action >= A_VIEW_BASE && b.action < A_KEY_BASE;
        bool setAction = (b.action >= A_SET_BASE && b.action < A_KP_OK) || b.action == A_SET_CLOSE;
        bool kpAction = b.action >= A_KP_OK;
        if (m_editing) { if (!IsEditAction(b.action)) continue; }
        else if (m_kpKind != KP_NONE) { if (!kpAction) continue; }
        else if (kpAction) continue;
        else if (m_setOpen) { if (!setAction) continue; }
        else if (setAction) continue;
        else if (m_viewMenu) { if (!viewAction) continue; }
        else if (m_areaMenu) { if (b.action != A_AREA_A && b.action != A_AREA_BOTH && b.action != A_AREA_B) continue; }
        else if (viewAction) continue;
        else if (m_pickOpen) { if (!pickAction) continue; }
        else if (pickAction) continue;
        else if (m_dlOpen) { if (!dlAction) continue; }
        else if (dlAction) continue;
        else if (m_trashOpen) { if (!trashAction) continue; }
        else if (trashAction) continue;
        else if (!m_detailCs.empty() && b.action < A_CLOSE_DETAIL) continue;
        if (PtInRect(&b.rc, p)) return b.action;
    }
    return A_NONE;
}

bool StripWindow::IsEditAction(int action)
{
    return (action >= A_EDIT_OK && action <= A_INK_RECO) || action == A_EDIT_CASE || action == A_EDIT_PASTE || action == A_EDIT_REQ ||
           (action >= A_RT_LEFT && action <= A_RT_RESET) || (action >= A_RT_TOK_BASE && action < A_RT_TOK_BASE + 400) ||
           (action >= A_KEY_BASE && action < A_KEY_BASE + 256);
}

int StripWindow::DropIndex(int col, POINT p) const
{
    const BayRect& br = m_cols[col].rect;
    int n = (int)m_cols[col].strips.size();
    if (col == m_dragFromCol) n -= 1;

    // Mitte des gezogenen Streifens entscheidet, nicht die Fingerspitze
    int centerY = p.y - m_grabOffset.y + m_stripH / 2;
    int rel = centerY - (br.body.top + m_pad) + m_scroll[col];
    double v = (rel - m_stripH / 2.0) / StripPitch();
    int idx = (int)std::ceil(v);
    return std::min(std::max(0, idx), std::max(0, n));
}

// ---------------------------------------------------------------------------
// Eingabe
// ---------------------------------------------------------------------------
void StripWindow::PointerDown(POINT p, bool touch)
{
    if (m_hwnd)
    {
        RECT rc;
        GetClientRect(m_hwnd, &rc);
        ComputeLayout(rc.right, rc.bottom);
    }
    m_touch = touch;
    m_downPt = m_curPt = p;
    m_mode = M_NONE;
    m_tlPress = false;
    m_downTick = GetTickCount();
    if (!m_board.connected && m_remoteClient && PtInRect(&m_offlineCard, p))
    {
        if (m_host) m_host->Command(".strips remote disconnect");   // StripBay Remote: Verbinden-Fenster (anderer PC / Code)
        return;
    }
    if (!m_board.connected && !PtInRect(&m_topBar, p)) return;   // Remote ohne Verbindung: nichts annehmen

    if (m_editing)
    {
        if (m_inkMode && PtInRect(&m_inkPad, p))
        {
            // Handschrift: neuer Strich
            m_mode = M_INK;
            m_inkStrokes.push_back({ p });
            m_inkMsg.clear();
            if (m_hwnd) KillTimer(m_hwnd, kInkTimer);
            return;
        }
        int a = HitButton(p);
        if (a != A_NONE)                    { m_mode = M_PRESS_BUTTON; m_pressedAction = a; }
        else if (!PtInRect(&m_editCard, p)) m_mode = M_PRESS_OVERLAY_BG;
        return;
    }

    if (m_kpKind != KP_NONE)
    {
        int a = HitButton(p);
        if (a != A_NONE)                 { m_mode = M_PRESS_BUTTON; m_pressedAction = a; }
        else if (!PtInRect(&m_kpCard, p)) m_mode = M_PRESS_OVERLAY_BG;
        return;
    }
    if (m_setOpen)
    {
        int a = HitButton(p);
        if (a != A_NONE)                  { m_mode = M_PRESS_BUTTON; m_pressedAction = a; }
        else if (!PtInRect(&m_setCard, p)) m_mode = M_PRESS_OVERLAY_BG;
        return;
    }
    if (m_viewMenu || m_areaMenu)
    {
        int a = HitButton(p);
        if (a != A_NONE) { m_mode = M_PRESS_BUTTON; m_pressedAction = a; }
        else             m_mode = M_PRESS_OVERLAY_BG;   // daneben = zu
        return;
    }
    if (m_pickOpen)
    {
        int a = HitButton(p);
        if (PickScrollList() && PtInRect(&m_pickListRc, p))
        {
            // Hoehenliste: Wischen blaettert, Antippen ohne Bewegung waehlt
            m_mode = M_PICK_DRAG;
            m_pressedAction = a;
            m_pickDragStart = m_pickScroll;
            m_pickDragMoved = false;
            return;
        }
        if (a != A_NONE)                    { m_mode = M_PRESS_BUTTON; m_pressedAction = a; }
        else if (!PtInRect(&m_pickCard, p)) m_mode = M_PRESS_OVERLAY_BG;
        return;
    }
    if (m_dlOpen)
    {
        int a = HitButton(p);
        if (a != A_NONE)                  { m_mode = M_PRESS_BUTTON; m_pressedAction = a; }
        else if (!PtInRect(&m_dlCard, p)) m_mode = M_PRESS_OVERLAY_BG;
        return;
    }
    if (m_trashOpen)
    {
        int a = HitButton(p);
        if (a != A_NONE)                     { m_mode = M_PRESS_BUTTON; m_pressedAction = a; }
        else if (!PtInRect(&m_trashCard, p)) m_mode = M_PRESS_OVERLAY_BG;
        return;
    }

    if (!m_detailCs.empty())
    {
        int a = HitButton(p);
        if (a != A_NONE)               { m_mode = M_PRESS_BUTTON; m_pressedAction = a; }
        else if (!PtInRect(&m_detailCard, p)) m_mode = M_PRESS_OVERLAY_BG;
        return;
    }

    int a = HitButton(p);
    if (a != A_NONE)
    {
        m_mode = M_PRESS_BUTTON;
        m_pressedAction = a;
        return;
    }

    // Rand zwischen zwei Bays: Breite ziehen
    for (const SlotEdge& e : m_slotEdges)
        if (PtInRect(&e.hit, p))
        {
            m_mode = M_RESIZE;
            m_rsLeft = e.left; m_rsRight = e.right;
            m_rsW0 = m_slotWNow[e.left]; m_rsW1 = m_slotWNow[e.right];
            m_rsF0 = SlotFactor(e.left); m_rsF1 = SlotFactor(e.right);
            return;
        }
    int col = HitCol(p);
    if (col >= 0 && m_cols[col].rect.overflow && PtInRect(&m_cols[col].rect.track, p))
    {
        m_mode = M_SCROLL;
        m_scrollCol = col;
        m_scrollStart = m_scroll[col];
        m_scrollByTrack = true;
        return;
    }

    // Zeitstrahl: Label oder Listenzeile (Antippen = naechster Schritt, Ziehen = andere Piste)
    if (TimelineActive())
    {
        std::string tcs;
        RECT trc;
        int tb = -1;
        if (TlHit(p, tcs, trc) && FindStrip(tcs, &tb) && ColumnOf(tcs) >= 0)
        {
            m_mode = M_PRESS_STRIP;
            m_dragCs = tcs;
            m_dragFromCol = ColumnOf(tcs);
            m_dragBay = tb;
            m_dragW = trc.right - trc.left;
            m_grabOffset.x = p.x - trc.left;
            m_grabOffset.y = p.y - trc.top;
            m_tlPress = true;
            m_tlPressRc = trc;
            m_tlPressZone = TlZoneAt(p, tcs);
            return;
        }
    }

    int idx = -1;
    if (HitStrip(p, col, idx))
    {
        RECT rc = SlotRect(col, idx);
        m_mode = M_PRESS_STRIP;
        m_dragCs = StripAt(col, idx).callsign;
        m_dragFromCol = col;
        m_dragBay = m_cols[col].strips[idx].bay;
        m_dragW = rc.right - rc.left;
        m_grabOffset.x = p.x - rc.left;
        m_grabOffset.y = p.y - rc.top;
        return;
    }

    // ins Leere getippt: Auswahl aufheben
    if (m_host && p.y > m_topBar.bottom) m_host->Command(".strips deselect");
    if (col >= 0 && PtInRect(&m_cols[col].rect.body, p) && m_cols[col].rect.overflow)
    {
        // Wischen auf freier Flaeche scrollt die Spalte
        m_mode = M_SCROLL;
        m_scrollCol = col;
        m_scrollStart = m_scroll[col];
        m_scrollByTrack = false;
    }
}

void StripWindow::PointerMove(POINT p)
{
    m_curPt = p;
    switch (m_mode)
    {
    case M_INK:
    {
        if (m_inkStrokes.empty()) break;
        p.x = std::max(m_inkPad.left, std::min(m_inkPad.right, p.x));
        p.y = std::max(m_inkPad.top, std::min(m_inkPad.bottom, p.y));
        std::vector<POINT>& st = m_inkStrokes.back();
        POINT q = st.back();
        if (std::abs(p.x - q.x) + std::abs(p.y - q.y) < 2) break;
        st.push_back(p);
        if (m_hwnd)
        {
            // sofort auf den Bildschirm, ohne das ganze Fenster neu zu zeichnen
            HDC dc = GetDC(m_hwnd);
            HPEN pen = CreatePen(PS_SOLID, std::max(2, Sx(3)), col::text);
            HGDIOBJ old = SelectObject(dc, pen);
            MoveToEx(dc, q.x, q.y, nullptr);
            LineTo(dc, p.x, p.y);
            SelectObject(dc, old);
            DeleteObject(pen);
            ReleaseDC(m_hwnd, dc);
        }
        break;
    }
    case M_PRESS_STRIP:
    {
        int threshold = m_touch ? Sx(14) : Sx(6);
        int dx = p.x - m_downPt.x, dy = p.y - m_downPt.y;
        if (dx * dx + dy * dy < threshold * threshold) break;
        m_mode = M_DRAG_STRIP;
        m_dropCol = m_dragFromCol;
    }
        [[fallthrough]];   // sofort Zielposition berechnen
    case M_DRAG_STRIP:
    {
        RECT rc = Rc(0, 0, m_width, m_height);
        if (m_hwnd) GetClientRect(m_hwnd, &rc);
        ComputeLayout(rc.right, rc.bottom);

        int b = HitCol(p);
        const StripData* dragged = FindStrip(m_dragCs);
        bool ok = b >= 0 && dragged && (b == m_dragFromCol || AcceptsDrop(b, *dragged));
        if (ok)
        {
            m_dropCol = b;
            const BayRect& br = m_cols[b].rect;
            int edge = Sx(40);
            if (br.overflow)
            {
                if (p.y < br.body.top + edge)          m_scroll[b] -= Sx(14);
                else if (p.y > br.body.bottom - edge)  m_scroll[b] += Sx(14);
            }
            ComputeLayout(rc.right, rc.bottom);
            m_dropIndex = DropIndex(b, p);
        }
        else
        {
            m_dropCol = -1;   // ausserhalb oder nicht erlaubt: Loslassen bricht ab
            m_dropIndex = -1;
        }
        Invalidate();
        break;
    }
    case M_RESIZE:
    {
        int dx = p.x - m_downPt.x;
        int minW = Sx(110);
        dx = std::max(minW - m_rsW0, std::min(m_rsW1 - minW, dx));
        // Summe der beiden bleibt, die Gewichte verschieben sich im Verhaeltnis
        int f0 = std::max(30, std::min(400, m_rsF0 * (m_rsW0 + dx) / std::max(1, m_rsW0)));
        int f1 = std::max(30, std::min(400, m_rsF1 * (m_rsW1 - dx) / std::max(1, m_rsW1)));
        m_colWLive[SlotKey(m_rsLeft)] = f0;
        m_colWLive[SlotKey(m_rsRight)] = f1;
        Invalidate();
        break;
    }
    case M_PICK_DRAG:
    {
        int dy = p.y - m_downPt.y;
        if (std::abs(dy) > Sx(8)) m_pickDragMoved = true;
        if (m_pickDragMoved)
        {
            int before = m_pickScroll;
            m_pickScroll = m_pickDragStart - dy / std::max(1, m_pickRowPitch);
            ClampPickScroll();
            if (m_pickScroll != before) Invalidate();
        }
        break;
    }
    case M_SCROLL:
    {
        if (m_scrollCol < 0 || m_scrollCol >= (int)m_cols.size()) break;
        const BayRect& br = m_cols[m_scrollCol].rect;
        int dy = p.y - m_downPt.y;
        if (m_scrollByTrack)
        {
            int trackH = std::max(1, (int)(br.track.bottom - br.track.top));
            double ratio = (double)br.contentHeight / trackH;
            m_scroll[m_scrollCol] = m_scrollStart + (int)(dy * ratio);
        }
        else
        {
            m_scroll[m_scrollCol] = m_scrollStart - dy;
        }
        ClampScroll(m_scrollCol);
        Invalidate();
        break;
    }
    default:
        break;
    }
}

void StripWindow::PointerUp(POINT p)
{
    m_curPt = p;
    Mode mode = m_mode;
    m_mode = M_NONE;

    switch (mode)
    {
    case M_RESIZE:
    {
        // neue Breiten speichern (je Ansicht und Platz)
        std::map<std::string, int> live = m_colWLive;
        if (m_host)
            for (const auto& kv : live) m_host->Command(".strips colw " + kv.first + " " + std::to_string(kv.second));
        m_colWLive.clear();
        Invalidate();
        break;
    }
    case M_INK:
        // nach einer kurzen Pause erkennen (mehrere Striche = ein Wort)
        if (m_hwnd) SetTimer(m_hwnd, kInkTimer, 900, nullptr);
        Invalidate();
        break;
    case M_PRESS_STRIP:
    {
        // Antippen: Details zeigen und in EuroScope anwaehlen.
        // Auf das SP-Feld getippt: gleich den Scratchpad-Editor oeffnen.
        std::string cs = m_dragCs;
        m_dragCs.clear();
        m_dragFromCol = m_dragBay = -1;
        if (m_tlPress)
        {
            // Zeitstrahl: Tipp = naechster Schritt; Rechtsklick, Stifttaste/Radierer oder langes Druecken = Menue
            bool sec = m_secondary || (m_touch && GetTickCount() - m_downTick >= 550);
            RECT anchor = m_tlPressRc;
            m_tlPress = false;
            m_secondary = false;
            int zone = m_tlPressZone;
            m_tlPressZone = 0;
            TlTap(cs, sec, anchor, zone);
            break;
        }
        // Flugzeugtyp antippen: Streifen einfaerben (Farbwahl)
        {
            bool onType = false;
            for (const auto& tc : m_typeCells)
                if (tc.second == cs && PtInRect(&tc.first, m_downPt)) onType = true;
            if (onType)
            {
                if (m_host) m_host->StripTapped(cs);
                OpenKeypad(KP_HL, cs, m_downPt);
                break;
            }
            bool onTsat = false;
            for (const auto& tc2 : m_tsatCells)
                if (tc2.second == cs && PtInRect(&tc2.first, m_downPt)) onTsat = true;
            if (onTsat)
            {
                if (m_host) m_host->StripTapped(cs);
                OpenKeypad(KP_TOBT, cs, m_downPt);
                break;
            }
            bool onPb = false;
            for (const auto& pc : m_pbCells)
                if (pc.second == cs && PtInRect(&pc.first, m_downPt)) onPb = true;
            if (onPb)
            {
                if (m_host) m_host->StripTapped(cs);
                OpenKeypad(KP_PB, cs, m_downPt);
                break;
            }
        }
        bool onSp = false, onClr = false;
        for (const auto& sc : m_spCells)
            if (sc.second == cs && PtInRect(&sc.first, m_downPt)) onSp = true;
        for (const auto& cc : m_clrCells)
            if (cc.second == cs && PtInRect(&cc.first, m_downPt)) onClr = true;
        bool onResp = false;
        for (const auto& rc3 : m_respCells)
            if (rc3.second == cs && PtInRect(&rc3.first, m_downPt)) onResp = true;
        if (onResp)
        {
            // Feld oben rechts: angeboten an mich = annehmen, sonst Menue zum Uebergeben
            const StripData* rs = FindStrip(cs);
            if (rs && rs->offerForMe) { if (m_host) m_host->Command(".strips accept " + cs); Invalidate(); break; }
            if (m_host) m_host->StripTapped(cs);
            OpenFieldFor(cs, F_RESP);
            break;
        }
        bool onDepClr = false;
        for (const auto& dc : m_depClrCells)
            if (dc.second == cs && PtInRect(&dc.first, m_downPt)) onDepClr = true;
        if (onDepClr)
        {
            // Pilot hat per CPDLC nach der Clearance gefragt: gleich die fertige DCL zum Senden
            const StripData* sd = FindStrip(cs);
            if (sd && !sd->clearance && sd->dlDcl == 1 && m_board.dlEnabled)
            {
                if (m_host) m_host->StripTapped(cs);
                // halbautomatisch: PDC vollstaendig -> ein Tipp sendet; sonst das Fenster (zeigt, was fehlt)
                bool complete = !sd->dlDclText.empty() && sd->dlDclText[0] != '!';
                if (complete && m_host) m_host->Datalink(cs, DL_SEND_DCL, std::string());
                // nicht gesendet (nicht vollstaendig, Hoppie nicht verbunden): das Fenster zeigt warum
                const StripData* after = FindStrip(cs);
                if (!complete || (after && after->dlDcl == 1)) OpenDatalink(cs);
                Invalidate();
                break;
            }
            // sonst: Freigabe direkt am Streifen geben / zuruecknehmen
            Invalidate();
            if (m_host) m_host->ToggleClearance(cs);
            break;
        }
        bool onInt = false, onHov = false;
        for (const auto& ic : m_intCells)
            if (ic.second == cs && PtInRect(&ic.first, m_downPt)) onInt = true;
        for (const auto& hc : m_hovCells)
            if (hc.second == cs && PtInRect(&hc.first, m_downPt)) onHov = true;
        bool onGone = false, onPark = false;
        for (const auto& gc : m_goneCells)
            if (gc.second == cs && PtInRect(&gc.first, m_downPt)) onGone = true;
        for (const auto& pc : m_parkCells)
            if (pc.second == cs && PtInRect(&pc.first, m_downPt)) onPark = true;
        if (onGone)
        {
            // uebergeben: Streifen weg (Papierkorb)
            Invalidate();
            if (m_host) m_host->RemoveStrip(cs);
            break;
        }
        bool onNext = false;
        for (const auto& nc : m_nextCells)
            if (nc.second == cs && PtInRect(&nc.first, m_downPt)) onNext = true;
        if (onNext)
        {
            // Weiter-Taste: in die naechste Bay (bzw. Freigabe / geparkt)
            const StripData* sd = FindStrip(cs);
            int fb = -1;
            FindStrip(cs, &fb);
            int nb = -1, k = sd ? NextStep(*sd, fb, nb) : 0;
            Invalidate();
            if (m_host && k == 1) m_host->StripMoved(cs, nb, -1);
            else if (m_host && k == 2) m_host->ToggleRunwayClearance(cs);
            else if (m_host && k == 3) m_host->SetGroundState(cs, GC_PARK);
            else if (m_host && k == 4) m_host->Command(".strips give " + cs);
            else if (m_host && k == 5) m_host->Command(".strips accept " + cs);
            else if (m_host && k == 6) m_host->Command(".strips take " + cs);
            break;
        }
        bool onStdClr = false;
        for (const auto& sc2 : m_stdClrCells)
            if (sc2.second == cs && PtInRect(&sc2.first, m_downPt)) onStdClr = true;
        if (onStdClr)
        {
            // bis zur Parkposition freigegeben: umschalten (mit allen synchron)
            Invalidate();
            if (m_host) m_host->Command(".strips stdclr " + cs);
            break;
        }
        if (onPark)
        {
            Invalidate();
            if (m_host) m_host->SetGroundState(cs, GC_PARK);
            break;
        }
        bool onSqk = false;
        for (const auto& qc : m_sqkCells)
            if (qc.second == cs && PtInRect(&qc.first, m_downPt)) onSqk = true;
        if (onSqk)
        {
            // SQK antippen: Tastenfeld 0-7 mit Auto (TopSky)
            if (m_host) m_host->StripTapped(cs);
            OpenKeypad(KP_SQUAWK, cs, m_downPt);
            break;
        }
        bool onStand = false;
        for (const auto& sc2 : m_standCells)
            if (sc2.second == cs && PtInRect(&sc2.first, m_downPt)) onStand = true;
        if (onStand)
        {
            // Stand fehlt / belegt: gleich die Standauswahl (mit Auto)
            if (m_host) m_host->StripTapped(cs);
            OpenFieldFor(cs, F_STAND);
            break;
        }
        bool onDl = false;
        for (const auto& dc : m_dlCells)
            if (dc.second == cs && PtInRect(&dc.first, m_downPt)) onDl = true;
        if (onDl)
        {
            if (m_host) m_host->StripTapped(cs);
            OpenDatalink(cs);
            break;
        }
        bool onHs = false;
        for (const auto& hc : m_hsCells)
            if (hc.second == cs && PtInRect(&hc.first, m_downPt)) onHs = true;
        for (const auto& rc2 : m_rwyCells)
            if (rc2.second == cs && PtInRect(&rc2.first, m_downPt)) onHs = true;
        if (onHs)
        {
            // Hold Short: Tastenfeld direkt am Streifen (nur moegliche Haltepunkte)
            if (m_host) m_host->StripTapped(cs);
            OpenKeypad(KP_HS, cs, m_downPt);
            break;
        }
        if (onHov)
        {
            // FREQ eines Abflugs in der Luft: weggeschickt
            Invalidate();
            if (m_host) m_host->ToggleHandover(cs);
            break;
        }
        if (onInt)
        {
            if (m_host) m_host->StripTapped(cs);
            OpenKeypad(KP_INT, cs, m_downPt);
            break;
        }
        if (onClr)
        {
            // Freigabefeld T/O bzw. LDG: nur umschalten, keine Details
            Invalidate();
            if (m_host) m_host->ToggleRunwayClearance(cs);
            break;
        }
        if (m_host) m_host->StripTapped(cs);
        if (onSp) OpenEditor(cs);
        else { m_detailCs = cs; m_tlMenu = false; }
        Invalidate();
        break;
    }
    case M_DRAG_STRIP:
    {
        std::string cs = m_dragCs;
        int col = m_dropCol, pos = m_dropIndex, fromCol = m_dragFromCol, fromBay = m_dragBay;
        int target = -1, idx = 0;
        long long plan = TlPlanTarget();   // auf der eigenen Seite entlang der Zeitleiter: Reihenfolge
        if (col >= 0 && col < (int)m_cols.size())
            // Gleiche Spalte: nur innerhalb der eigenen Bay umsortieren
        {
            const ColumnDef& cd = ViewOf(m_view, m_board).cols[m_cols[col].def];
            const StripData* ds = FindStrip(cs);
            target = (col == fromCol) ? fromBay : (ds && ds->arrival && cd.dropArr >= 0) ? cd.dropArr : cd.dropBay;
            // Runway-Bay einer bestimmten Piste (Hannover): Piste bzw. Helipad gleich mit setzen
            if (col != fromCol && ds && cd.rwyMatch && !RwyColMatch(cd.rwyMatch, *ds, false) && m_host)
            {
                std::string cur = ds->arrival ? ds->arrRwy : ds->rwy, pick;
                if (std::string(cd.rwyMatch) == "AREA") pick.clear();
                else if (std::string(cd.rwyMatch) == "HELI") pick = "H2";
                else
                {
                    std::string a = std::string(cd.rwyMatch).substr(0, 3), b2 = std::string(cd.rwyMatch).substr(4, 3);
                    // gleiche Richtung wie bisher (09x bzw. 27x)
                    pick = (cur.size() >= 2 && cur.compare(0, 2, b2, 0, 2) == 0) ? b2 : a;
                    if (cur.size() < 2 || (cur.compare(0, 2, a, 0, 2) != 0 && cur.compare(0, 2, b2, 0, 2) != 0)) pick = a;
                }
                if (!pick.empty()) m_host->SetField(cs, F_RWY, pick);
            }
            // Getrennte Pisten (Berlin, Leipzig): auf die Spalte der anderen Piste gezogen (Final,
            // Runway, Holding ...) -> Piste im Flugplan auf eine Piste dieser Seite
            else if (col != fromCol && ds && m_host && !cd.rwyMatch && m_board.rwySplit &&
                     (m_cols[col].group == RWY_A || m_cols[col].group == RWY_B) && ds->rwyGroup != m_cols[col].group)
                m_host->Command(".strips rwygroup " + cs + (m_cols[col].group == RWY_A ? " A" : " B"));
            // Hannover im Zeitstrahl: auf die andere Bahn gezogen (Taxi, Holding, Final) -> Piste dieser Bahn
            else if (col != fromCol && ds && !cd.rwyMatch && TlSplitEddv() &&
                     (m_cols[col].group == RWY_A || m_cols[col].group == RWY_B) && TlRwyGroup(*ds) != m_cols[col].group)
                TlSetGroup(cs, m_cols[col].group);
            // Zeitstrahl: die Lage ergibt sich aus der Zeit - nichts umsortieren. Auf die andere Seite
            // gezogen (gleiche Art Spalte) wechselt nur die Piste (oben), die Bay bleibt.
            if (TlAbsorbed(col) && (col == fromCol || (TlAbsorbed(fromCol) && (m_cols[col].def == m_cols[fromCol].def ||
                ViewOf(m_view, m_board).cols[m_cols[col].def].dropBay == ViewOf(m_view, m_board).cols[m_cols[fromCol].def].dropBay))))
                target = -1;
        }
        if (target >= 0 && target < BAY_COUNT)
        {

            // Position in der Spalte -> Position in der ganzen Ziel-Bay. Die Bay kann
            // auch Streifen der anderen Piste enthalten, die hier nicht zu sehen sind.
            // Deshalb vor den naechsten sichtbaren Streifen der Ziel-Bay einfuegen.
            std::vector<std::string> bayList;   // Ziel-Bay ohne den gezogenen Streifen
            for (const StripData& s : m_board.bays[target])
                if (s.callsign != cs) bayList.push_back(s.callsign);
            std::vector<StripRef> colList;      // Spalte ohne den gezogenen Streifen
            for (const StripRef& r : m_cols[col].strips)
                if (m_board.bays[r.bay][r.idx].callsign != cs) colList.push_back(r);
            auto posInBay = [&](const StripRef& r) {
                const std::string& c = m_board.bays[r.bay][r.idx].callsign;
                return (int)(std::find(bayList.begin(), bayList.end(), c) - bayList.begin());
            };
            idx = -1;
            for (int k = std::max(0, pos); k < (int)colList.size(); ++k)
                if (colList[k].bay == target) { idx = posInBay(colList[k]); break; }
            if (idx < 0)
                for (int k = std::min(pos, (int)colList.size()) - 1; k >= 0; --k)
                    if (colList[k].bay == target) { idx = posInBay(colList[k]) + 1; break; }
            if (idx < 0) idx = (int)bayList.size();   // nichts aus der Ziel-Bay in der Spalte: hinten an
        }
        m_dragCs.clear();
        m_dragFromCol = m_dragBay = m_dropCol = m_dropIndex = -1;
        m_tlPress = false;
        Invalidate();
        // Notiz-Streifen in eine Pistenspalte: gehoert dann zu dieser Piste
        int dropGroup = (col >= 0 && col < (int)m_cols.size()) ? m_cols[col].group : RWY_NONE;
        const StripData* dragged = FindStrip(cs);
        bool freeDrag = dragged && dragged->freeStrip;
        if (target >= 0 && m_host) m_host->StripMoved(cs, target, idx);
        if (target >= 0 && m_host && freeDrag && dropGroup != RWY_NONE) m_host->SetStripGroup(cs, dropGroup);
        if (plan > 0 && target < 0 && m_host) m_host->Command(".strips tlplan " + cs + " " + HhmmssStr(plan));
        m_tlPressZone = 0;
        break;
    }
    case M_PRESS_BUTTON:
        if (HitButton(p) == m_pressedAction) ExecuteAction(m_pressedAction);
        m_pressedAction = A_NONE;
        break;
    case M_PICK_DRAG:
        if (!m_pickDragMoved && m_pressedAction != A_NONE && HitButton(p) == m_pressedAction) ExecuteAction(m_pressedAction);
        m_pressedAction = A_NONE;
        Invalidate();
        break;
    case M_PRESS_OVERLAY_BG:
        if (m_editing)
        {
            if (!PtInRect(&m_editCard, p)) CloseEditor(false);   // daneben getippt = abbrechen
        }
        else if (m_pickOpen)
        {
            if (!PtInRect(&m_pickCard, p)) { m_pickOpen = false; Invalidate(); }
        }
        else if (m_kpKind != KP_NONE)
        {
            if (!PtInRect(&m_kpCard, p)) { m_kpKind = KP_NONE; Invalidate(); }
        }
        else if (m_setOpen)
        {
            if (!PtInRect(&m_setCard, p)) { m_setOpen = false; Invalidate(); }
        }
        else if (m_viewMenu || m_areaMenu)
        {
            m_viewMenu = false;
            m_areaMenu = false;
            Invalidate();
        }
        else if (m_dlOpen)
        {
            if (!PtInRect(&m_dlCard, p)) { m_dlOpen = false; Invalidate(); }
        }
        else if (m_trashOpen)
        {
            if (!PtInRect(&m_trashCard, p)) { m_trashOpen = false; Invalidate(); }
        }
        else if (!PtInRect(&m_detailCard, p))
        {
            m_detailCs.clear();
            // daneben getippt: auch die Auswahl aufheben
            if (m_host) m_host->Command(".strips deselect");
            Invalidate();
        }
        break;
    default:
        break;
    }
    m_secondary = false;
}

void StripWindow::PointerCancel()
{
    m_mode = M_NONE;
    m_tlPress = false;
    m_secondary = false;
    m_dragCs.clear();
    m_dragFromCol = m_dragBay = m_dropCol = m_dropIndex = -1;
    m_pressedAction = A_NONE;
    Invalidate();
}

void StripWindow::Wheel(POINT pScreen, int delta)
{
    if (PickScrollList())
    {
        m_pickScroll -= delta / std::max(1, (int)WHEEL_DELTA) * 2;
        if (delta != 0 && delta / (int)WHEEL_DELTA == 0) m_pickScroll -= delta > 0 ? 1 : -1;   // Touchpad: kleine Schritte
        ClampPickScroll();
        Invalidate();
        return;
    }
    if (!m_hwnd || !m_detailCs.empty() || m_trashOpen || m_pickOpen) return;
    POINT p = pScreen;
    ScreenToClient(m_hwnd, &p);
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    ComputeLayout(rc.right, rc.bottom);
    int b = HitCol(p);
    if (b < 0) return;
    m_scroll[b] -= delta * StripPitch() / WHEEL_DELTA;
    ClampScroll(b);
    Invalidate();
}

void StripWindow::ExecuteAction(int action)
{
    // Zeitstrahl-Menue: nach einer Aktion zu (ausser "Details"); Unterfenster (Tastenfeld, Liste) bleiben
    bool tlMenu = m_tlMenu && !m_detailCs.empty() && action >= A_CLOSE_DETAIL && action != A_TL_DETAILS && action < A_KP_OK &&
                  !IsEditAction(action);
    std::string cs = m_detailCs;
    ExecuteActionInner(action);
    if (tlMenu && m_tlMenu && m_detailCs == cs)
    {
        m_detailCs.clear();
        m_tlMenu = false;
        Invalidate();
    }
}

void StripWindow::ExecuteActionInner(int action)
{
    // Scratchpad-Editor
    if (action >= A_KEY_BASE && action < A_KEY_BASE + 256) { EditKey((wchar_t)(action - A_KEY_BASE)); return; }
    // Routen-Editor
    if ((action >= A_RT_LEFT && action <= A_RT_RESET) || (action >= A_RT_TOK_BASE && action < A_RT_TOK_BASE + 400))
    {
        RouteAction(action);
        return;
    }
    if (action == A_EDIT_CLEAR && RouteEditing())
    {
        m_editText.clear();
        if (m_rtSel >= 0 && m_rtSel < (int)m_rtTok.size()) m_rtTok[m_rtSel].clear();
        Invalidate();
        return;
    }
    // Einstellungsfenster
    if (action == A_SETTINGS) { m_setOpen = !m_setOpen; m_viewMenu = m_areaMenu = false; m_detailCs.clear(); Invalidate(); return; }
    if (action == A_SET_CLOSE) { m_setOpen = false; Invalidate(); return; }
    if (action >= A_KP_OK) { KeypadKey(action); return; }
    if (action >= A_SET_BASE)
    {
        int i = action - A_SET_BASE;
        if (i < 0 || i >= (int)m_setCmds.size()) return;
        std::string c = m_setCmds[i];
        if (c.compare(0, 5, "edit:") == 0)
        {
            // "edit:<Befehl>\t<Titel>\t<aktueller Wert>": Text eingeben, dann Befehl + Text
            std::vector<std::string> f;
            size_t a = 5;
            for (;;) { size_t tb = c.find('\t', a); f.push_back(c.substr(a, tb == std::string::npos ? std::string::npos : tb - a)); if (tb == std::string::npos) break; a = tb + 1; }
            m_editCs.clear();
            m_editing = true;
            m_editMode = 6;
            m_editLower = false;
            if (m_hwnd)
            {
                DpiScope dpi;
                m_prevForeground = GetForegroundWindow();
                LONG_PTR ex = GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE);
                SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE, ex & ~(LONG_PTR)WS_EX_NOACTIVATE);
                SetForegroundWindow(m_hwnd);
                SetFocus(m_hwnd);
            }
            m_editPrefix = f.size() > 0 ? f[0] : std::string();
            m_editTitle = f.size() > 1 ? W(f[1]) : std::wstring();
            m_editText = f.size() > 2 ? f[2] : std::string();
            Invalidate();
            return;
        }
        if (m_host) m_host->Command(c);
        Invalidate();
        return;
    }
    // CPDLC-Fenster
    if (action >= A_VIEW_BASE && action < A_KEY_BASE)
    {
        int i = action - A_VIEW_BASE;
        m_viewMenu = false;
        if (i >= 0 && i < (int)m_stationItems.size())
        {
            SetView(m_stationItems[i].view);
            if (m_stationItems[i].filter >= 0) SetRwyFilter(m_stationItems[i].filter);
            if (m_host) m_host->WindowStateChanged();
        }
        Invalidate();
        return;
    }
    if (action == A_DL_OPEN) { if (!m_detailCs.empty()) OpenDatalink(m_detailCs); return; }
    if (action == A_DL_CLOSE) { m_dlOpen = false; Invalidate(); return; }
    if (action == A_DL_TEXT) { if (!m_dlCs.empty()) OpenDlTextEditor(m_dlCs); return; }
    if (action >= A_DL_BASE && action < A_DL_BASE + DL_COUNT)
    {
        if (m_host && !m_dlCs.empty()) m_host->Datalink(m_dlCs, action - A_DL_BASE, std::string());
        Invalidate();
        return;
    }
    switch (action)
    {
    case A_EDIT_OPEN:   if (!m_detailCs.empty()) OpenEditor(m_detailCs); return;
    case A_INT_OPEN:    if (!m_detailCs.empty()) OpenKeypad(KP_INT, m_detailCs, m_downPt); return;
    case A_EDIT_ABLE:   m_editAssign = false; CloseEditor(true); return;
    case A_EDIT_NOTE:   m_editNote = true; CloseEditor(true); return;
    case A_EDIT_DESIG:  m_editDesig = true; CloseEditor(true); return;
    case A_EDIT_REQ:
    {
        // Streifen uebernehmen (auch einen, der hier ausgeblendet ist)
        std::string t = m_editText;
        CloseEditor(false);
        if (m_host && !t.empty()) m_host->Command(".strips take " + t);
        return;
    }
    case A_HS_OPEN:     if (!m_detailCs.empty()) OpenKeypad(KP_HS, m_detailCs, m_downPt); return;
    case A_EDIT_RWY:
    {
        // Hold Short vor der Piste des Fliegers
        const StripData* sd = FindStrip(m_editCs);
        if (sd) m_editText = sd->arrival ? sd->arrRwy : sd->rwy;
        Invalidate();
        return;
    }
    case A_EDIT_OK:     CloseEditor(true); return;
    case A_EDIT_CANCEL: CloseEditor(false); return;
    case A_EDIT_CLEAR:  m_editText.clear(); Invalidate(); return;
    case A_EDIT_INK:
        m_inkMode = !m_inkMode;
        m_inkStrokes.clear();
        m_inkMsg.clear();
        Invalidate();
        return;
    case A_INK_CLEAR:
        m_inkStrokes.clear();
        m_inkMsg.clear();
        if (m_hwnd) KillTimer(m_hwnd, kInkTimer);
        Invalidate();
        return;
    case A_INK_RECO:    InkRecognize(); return;
    case A_EDIT_CASE:   m_editLower = !m_editLower; Invalidate(); return;
    case A_EDIT_PASTE:  PasteClipboard(); return;
    case A_EDIT_BACK:   EditKey(L'\b'); return;
    default: break;
    }

    std::string cs = m_detailCs;   // Kopie: der Host kann das Board sofort ersetzen

    if (action >= A_STATE_BASE && action < A_STATE_BASE + GC_COUNT)
    {
        if (m_host && !cs.empty()) m_host->SetGroundState(cs, action - A_STATE_BASE);
        Invalidate();
        return;
    }
    if (action >= A_FIELD_BASE && action < A_FIELD_BASE + F_COUNT)
    {
        OpenField(action - A_FIELD_BASE);
        return;
    }
    if (action >= A_PICK_TAB_BASE && action < A_DL_BASE)
    {
        int t = action - A_PICK_TAB_BASE;
        if (m_standMode) m_pickTab = t;          // Standauswahl: immer eine Kategorie
        else m_pickTab = (t == m_pickTab) ? -1 : t;   // nochmal antippen = alle
        m_pickPage = 0;
        Invalidate();
        return;
    }
    if (action >= A_PICK_BASE && action < A_PICK_TAB_BASE)
    {
        std::vector<int> vis = PickVisible();
        int i = m_pickPage * m_pickPerPage + (action - A_PICK_BASE);
        if (i < (int)vis.size())
        {
            std::string v = m_pickOpts[vis[i]], pcs = m_pickCs;
            int fld = m_pickField;
            m_pickOpen = false;
            Invalidate();
            if (m_host) m_host->SetField(pcs, fld, v);
        }
        return;
    }
    switch (action)
    {
    case A_PICK_PREV:  if (m_pickPage > 0) --m_pickPage; Invalidate(); return;
    case A_RWY_SWITCH:
        if (m_host) m_host->Command(".strips rwyswitch");
        Invalidate();
        return;
    case A_PICK_AUTO:
    case A_PICK_BTN0:
    case A_PICK_BTN1:
    {
        std::string v, pcs = m_pickCs;
        if (action == A_PICK_AUTO) v = "AUTO";
        else
        {
            // Direkt-Buttons sind die ersten Gruppen (button = true), in der Reihenfolge Apron 1, GAT
            int want = action - A_PICK_BTN0, k = 0;
            for (const StandGroup& g : m_standGroups)
                if (g.button) { if (k == want) v = StandButtonValue(g); ++k; }
        }
        if (v.empty()) return;
        m_pickOpen = false;
        Invalidate();
        if (m_host) m_host->SetField(pcs, F_STAND, v);
        return;
    }
    case A_PICK_UP:    m_pickScroll -= std::max(1, m_pickRowsVis - 1); ClampPickScroll(); Invalidate(); return;
    case A_PICK_DOWN:  m_pickScroll += std::max(1, m_pickRowsVis - 1); ClampPickScroll(); Invalidate(); return;
    case A_PICK_NEXT:  ++m_pickPage; Invalidate(); return;
    case A_PICK_CLOSE: m_pickOpen = false; Invalidate(); return;
    case A_PICK_FREE:  m_pickOpen = false; OpenFieldEditor(m_pickField); return;
    case A_PICK_CLEAR:
    {
        std::string pcs = m_pickCs;
        int fld = m_pickField;
        m_pickOpen = false;
        Invalidate();
        if (m_host) m_host->SetField(pcs, fld, "");
        return;
    }
    default: break;
    }
    if (action >= A_RESTORE_BASE && action < A_FIELD_BASE)
    {
        int i = action - A_RESTORE_BASE;
        if (i < (int)m_board.trash.size())
        {
            std::string id = m_board.trash[i].id;   // Kopie, das Board wird gleich ersetzt
            if (m_host) m_host->RestoreStrip(id);
        }
        if (m_board.trash.empty()) m_trashOpen = false;
        Invalidate();
        return;
    }
    if (action >= A_MOVE_BASE && action < A_MOVE_BASE + BAY_COUNT)
    {
        if (m_host && !cs.empty()) m_host->StripMoved(cs, action - A_MOVE_BASE, -1);
        Invalidate();
        return;
    }

    switch (action)
    {
    case A_ZOOM_OUT:
        SetZoom(m_zoom - 0.1);
        if (m_host) m_host->WindowStateChanged();
        break;
    case A_ZOOM_IN:
        SetZoom(m_zoom + 0.1);
        if (m_host) m_host->WindowStateChanged();
        break;
    case A_FULLSCREEN:
        ToggleFullscreen();
        if (m_host) m_host->WindowStateChanged();
        break;
    case A_HIDE:
        Show(false);
        if (m_host) m_host->WindowStateChanged();
        break;
    case A_VIEW:
        // Stationsauswahl auf- bzw. zuklappen
        m_viewMenu = !m_viewMenu;
        m_areaMenu = false;
        if (m_viewMenu) BuildStationItems();
        Invalidate();
        break;
    case A_NEW_STRIP:
        OpenNewEditor();
        break;
    case A_TRASH_OPEN:
        m_detailCs.clear();
        m_trashOpen = true;
        Invalidate();
        break;
    case A_TRASH_CLOSE:
        m_trashOpen = false;
        Invalidate();
        break;
    case A_TRASH_ALL:
        m_trashOpen = false;
        Invalidate();
        if (m_host) m_host->RestoreStrip("*");
        break;
    case A_THEME:
        SetLight(!m_light);
        if (m_host) m_host->WindowStateChanged();
        break;
    case A_AREA_MENU:
        m_areaMenu = !m_areaMenu;
        m_viewMenu = false;
        Invalidate();
        break;
    case A_AREA_A:
    case A_AREA_BOTH:
    case A_AREA_B:
        m_areaMenu = false;
        SetRwyFilter(action == A_AREA_A ? RWY_A : action == A_AREA_B ? RWY_B : 0);
        if (m_host) m_host->WindowStateChanged();
        break;
    case A_RWY_FILTER:
        SetRwyFilter((m_rwyFilter + 1) % 3);  // beide -> erste Piste -> zweite Piste -> beide
        if (m_host) m_host->WindowStateChanged();
        break;
    case A_CLOSE_DETAIL:
        m_detailCs.clear();
        Invalidate();
        break;
    case A_TIMELINE:
        if (m_host) m_host->Command(".strips timeline toggle");
        Invalidate();
        break;
    case A_TL_DETAILS:
        m_tlMenu = false;   // gleicher Streifen, ganzes Detailfenster
        Invalidate();
        break;
    case A_TL_COLOUR:
        if (!cs.empty()) OpenKeypad(KP_HL, cs, m_downPt);
        break;
    case A_TL_SEQ:
        if (!cs.empty()) OpenKeypad(KP_TLSEQ, cs, m_downPt);
        break;
    case A_TL_DAYNIGHT:
        if (m_host) m_host->Command(".strips timeline night toggle");
        Invalidate();
        break;
    case A_TL_PLANRESET:
        if (m_host && !cs.empty()) m_host->Command(".strips tlplan " + cs + " NONE");
        Invalidate();
        break;

    case A_TL_OTHERSIDE:
        if (m_host && !cs.empty())
        {
            int cc = ColumnOf(cs);
            int g = cc >= 0 ? m_cols[cc].group : RWY_NONE;
            TlSetGroup(cs, g == RWY_A ? RWY_B : RWY_A);
        }
        Invalidate();
        break;
    case A_CLEARANCE:
        if (m_host && !cs.empty()) m_host->ToggleClearance(cs);
        Invalidate();
        break;
    case A_RWY_CLEAR:
        if (m_host && !cs.empty()) m_host->ToggleRunwayClearance(cs);
        Invalidate();
        break;
    case A_GOAROUND:
        if (m_host && !cs.empty()) m_host->GoAround(cs);
        Invalidate();
        break;
    case A_HOV:
        if (m_host && !cs.empty()) m_host->ToggleHandover(cs);
        Invalidate();
        break;
    default:
        if (action >= A_RWYST_BASE && action <= A_RWYST_END)
        {
            int b = action - A_RWYST_BASE;
            if (b < (int)m_cols.size() && m_host)
            {
                const ColumnDef& def = ViewOf(m_view, m_board).cols[m_cols[b].def];
                std::string key;
                for (wchar_t ch : ColTitle(b)) key += (char)(ch < 128 ? ch : '?');
                auto rs = m_board.rwyState.find(key);
                int st = ((rs == m_board.rwyState.end() ? 0 : rs->second) + 1) % 3;
                std::string match = def.rwyMatch ? std::string(def.rwyMatch).substr(0, std::string(def.rwyMatch).find(' ')) : "-";
                m_host->Command(".strips rwystate " + std::to_string(st) + " " + std::to_string(m_cols[b].group) + " " + match + " " + key);
            }
            Invalidate();
            return;
        }
        break;
    case A_HL_BASE: case A_HL_BASE + 1: case A_HL_BASE + 2: case A_HL_BASE + 3: case A_HL_BASE + 4: case A_HL_BASE + 5:
        if (m_host && !cs.empty()) m_host->Command(".strips hl " + cs + " " + std::to_string(action - A_HL_BASE));
        Invalidate();
        break;
    case A_REQ:
        if (m_host && !cs.empty()) m_host->Command(".strips take " + cs);
        Invalidate();
        break;
    case A_UNDO:
        if (m_host) m_host->Command(".strips undo");
        Invalidate();
        break;
    case A_REJECT:
        if (m_host && !cs.empty()) m_host->Command(".strips reject " + cs);
        Invalidate();
        break;
    case A_FLIPDIR:
        if (m_host && !cs.empty()) m_host->Command(".strips flip " + cs);
        Invalidate();
        break;
    case A_DIVERT:
        if (m_host && !cs.empty()) m_host->Command(".strips divert " + cs);
        Invalidate();
        break;
    case A_NEXTRESP:
        if (m_host && !cs.empty())
        {
            const StripData* ns = FindStrip(cs);
            if (ns && ns->offerForMe) m_host->Command(".strips accept " + cs);
            else if (ns && !ns->offerTo.empty()) m_host->Command(".strips offer " + cs + " CANCEL");
            else m_host->Command(".strips give " + cs);
        }
        Invalidate();
        break;
    case A_REMOVE:
        m_detailCs.clear();
        Invalidate();
        if (m_host && !cs.empty()) m_host->RemoveStrip(cs);
        break;
    }
}

// ---------------------------------------------------------------------------
// Fensterprozedur
// ---------------------------------------------------------------------------
LRESULT CALLBACK StripWindow::WndProcStatic(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_NCCREATE)
    {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
    }
    StripWindow* self = (StripWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (self) return self->WndProc(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT StripWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    // Fenster nimmt EuroScope nie den Fokus weg, Tippen geht also direkt weiter.
    // Ausnahme: der Scratchpad-Editor ist offen, dann geht auch die echte Tastatur.
    case WM_MOUSEACTIVATE:
        return m_editing ? MA_ACTIVATE : MA_NOACTIVATE;
    case WM_POINTERACTIVATE:
        return m_editing ? 1 /* PA_ACTIVATE */ : PA_NOACTIVATE;

    case WM_CHAR:
        if (m_editing)
        {
            EditKey((wchar_t)wp);
            return 0;
        }
        break;

    case WM_POINTERDOWN:
    case WM_POINTERUPDATE:
    case WM_POINTERUP:
    {
        UINT32 id = LOWORD(wp);
        DWORD type = 0;
        if (!s_GetPointerType || !s_GetPointerType(id, &type) || type == kPointerTypeMouse) break;
        if (!(HIWORD(wp) & kPointerFlagPrimary)) return 0;   // nur der erste Finger zaehlt
        POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(hwnd, &p);
        if (msg == WM_POINTERDOWN && type == 3 /* PT_PEN */)
        {
            // Stifttaste (zweite Taste) oder Radierer: im Zeitstrahl das Menue statt des naechsten Schritts
            if (HIWORD(wp) & 0x0020 /* POINTER_MESSAGE_FLAG_SECONDBUTTON */) m_secondary = true;
#ifdef PEN_FLAG_ERASER
            POINTER_PEN_INFO pi = {};
            if (s_GetPointerPenInfo && s_GetPointerPenInfo(id, &pi) &&
                (pi.penFlags & (PEN_FLAG_ERASER | PEN_FLAG_INVERTED | PEN_FLAG_BARREL)))
                m_secondary = true;
#endif
        }
        if (msg == WM_POINTERDOWN)        PointerDown(p, true);
        else if (msg == WM_POINTERUPDATE) PointerMove(p);
        else                              PointerUp(p);
        return 0;  // keine Maus-Emulation, kein Rechtsklick durch langes Druecken
    }
    case WM_POINTERCAPTURECHANGED:
        PointerCancel();
        return 0;

    case WM_LBUTTONDOWN:
    {
        POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        SetCapture(hwnd);
        PointerDown(p, false);
        return 0;
    }
    case WM_MOUSEMOVE:
    {
        POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        if (m_mode != M_NONE) PointerMove(p);
        // Maus ueber einem Bay-Rand: Pfeil links/rechts (Breite ziehen)
        else
        {
            for (const SlotEdge& e : m_slotEdges)
                if (PtInRect(&e.hit, p)) { SetCursor(LoadCursor(nullptr, IDC_SIZEWE)); break; }
            // Zeitstrahl: Klickfeld unter der Maus umrahmen (freie Flaeche zaehlt zum Status)
            std::string hcs;
            int hk = 0;
            bool overlay = m_editing || m_kpKind != KP_NONE || m_setOpen || m_viewMenu || m_areaMenu || m_pickOpen || m_dlOpen ||
                           m_trashOpen || !m_detailCs.empty();
            RECT hrc;
            if (!overlay && TimelineActive() && TlHit(p, hcs, hrc))
            {
                hk = TlZoneAt(p, hcs);
                if (hk == TLZ_BODY) hk = TLZ_STATUS;
                SetCursor(LoadCursor(nullptr, IDC_HAND));
            }
            else hcs.clear();
            if (hcs != m_tlHoverCs || hk != m_tlHoverKind)
            {
                m_tlHoverCs = hcs;
                m_tlHoverKind = hk;
                Invalidate();
            }
        }
        if (m_mode == M_RESIZE) SetCursor(LoadCursor(nullptr, IDC_SIZEWE));
        return 0;
    }
    case WM_LBUTTONUP:
    {
        POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        PointerUp(p);
        if (GetCapture() == hwnd) ReleaseCapture();
        return 0;
    }
    case WM_CAPTURECHANGED:
        if ((HWND)lp != hwnd && m_mode != M_NONE && !m_touch) PointerCancel();
        return 0;
    case WM_RBUTTONDOWN:
    {
        // Rechtsklick auf ein Zeitstrahl-Label: Menue am Label (sonst nichts)
        POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        std::string tcs;
        RECT trc;
        bool overlay = m_editing || m_kpKind != KP_NONE || m_setOpen || m_viewMenu || m_areaMenu || m_pickOpen || m_dlOpen ||
                       m_trashOpen || !m_detailCs.empty();
        if (m_mode == M_NONE && !overlay && TimelineActive() && TlHit(p, tcs, trc))
        {
            m_secondary = true;
            SetCapture(hwnd);
            PointerDown(p, false);
        }
        return 0;
    }
    case WM_RBUTTONUP:
    {
        POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        if (m_secondary) PointerUp(p);
        if (GetCapture() == hwnd) ReleaseCapture();
        return 0;
    }
    case WM_MOUSEWHEEL:
    {
        POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        Wheel(p, GET_WHEEL_DELTA_WPARAM(wp));
        return 0;
    }

    case WM_TIMER:
        if (wp == kClockTimer) InvalidateRect(hwnd, m_anyBlink ? nullptr : &m_topBar, FALSE);
        if (wp == kInkTimer) { KillTimer(hwnd, kInkTimer); InkRecognize(); }
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        int w = rc.right, h = rc.bottom;
        if (w > 0 && h > 0)
        {
            if (!m_memDC || w != m_memW || h != m_memH)
            {
                if (m_memDC)
                {
                    SelectObject(m_memDC, m_memOld);
                    DeleteObject(m_memBmp);
                    DeleteDC(m_memDC);
                }
                m_memDC = CreateCompatibleDC(hdc);
                m_memBmp = CreateCompatibleBitmap(hdc, w, h);
                m_memOld = (HBITMAP)SelectObject(m_memDC, m_memBmp);
                m_memW = w;
                m_memH = h;
            }
            RenderTo(m_memDC, w, h);
            BitBlt(hdc, 0, 0, w, h, m_memDC, 0, 0, SRCCOPY);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_SIZE:
        Invalidate();
        return 0;

    case WM_GETMINMAXINFO:
    {
        MINMAXINFO* mmi = (MINMAXINFO*)lp;
        mmi->ptMinTrackSize.x = 480;
        mmi->ptMinTrackSize.y = 320;
        return 0;
    }

    case WM_DPICHANGED:
    {
        m_dpi = HIWORD(wp);
        if (m_dpi == 0) m_dpi = 96;
        UpdateScale();
        if (!m_fullscreen)
        {
            RECT* r = (RECT*)lp;
            SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        Invalidate();
        return 0;
    }

    case WM_EXITSIZEMOVE:
        if (!m_fullscreen) GetWindowRect(hwnd, &m_normalRect);
        if (m_host) m_host->WindowStateChanged();
        return 0;

    case WM_CLOSE:
        // Schliessen blendet nur aus, ".strips show" holt das Fenster zurueck
        ShowWindow(hwnd, SW_HIDE);
        if (m_host) m_host->WindowStateChanged();
        return 0;

    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// Zeichnen
// ---------------------------------------------------------------------------
void StripWindow::RenderTo(HDC hdc, int width, int height)
{
    ComputeLayout(width, height);
    m_buttons.clear();
    m_spCells.clear();
    m_typeCells.clear();
    m_pbCells.clear();
    m_tsatCells.clear();
    m_clrCells.clear();
    m_intCells.clear();
    m_depClrCells.clear();
    m_stdClrCells.clear();
    m_nextCells.clear();
    m_anyBlink = false;
    m_respCells.clear();
    m_hovCells.clear();
    m_hsCells.clear();
    m_rwyCells.clear();
    m_dlCells.clear();
    m_goneCells.clear();
    m_parkCells.clear();
    m_standCells.clear();
    m_sqkCells.clear();

    m_tlHits.clear();
    bool tl = TimelineActive();
    // DFS-Schema: das ganze Fenster dunkel (Tag dunkelgrau, Nacht fast schwarz), unabhaengig von Hell/Dunkel
    bool dfs = tl && m_board.opt.tlDfs;
    if (dfs)
    {
        SetTheme(false);
        bool n = m_board.opt.tlNight;
        col::app = n ? RGB(8, 9, 10) : RGB(30, 31, 34);
        col::topBar = n ? RGB(18, 19, 21) : RGB(40, 42, 46);
        col::bay = n ? RGB(16, 17, 19) : RGB(36, 38, 42);
        col::bayBorder = n ? RGB(44, 47, 52) : RGB(70, 74, 80);
        col::bayHeader = n ? RGB(32, 34, 38) : RGB(58, 61, 66);
        col::card = n ? RGB(24, 25, 28) : RGB(46, 48, 53);
        col::cardBorder = n ? RGB(56, 59, 64) : RGB(92, 96, 104);
        col::button = n ? RGB(40, 42, 46) : RGB(64, 67, 73);
        col::scrollTrack = n ? RGB(20, 21, 24) : RGB(42, 44, 49);
        col::scrollThumb = n ? RGB(60, 63, 68) : RGB(96, 100, 108);
        col::emptyText = n ? RGB(70, 74, 80) : RGB(110, 114, 122);
        if (n)
        {
            col::text = RGB(184, 188, 194);
            col::textDim = RGB(112, 116, 122);
        }
    }
    Fill(hdc, Rc(0, 0, width, height), col::app);
    DrawTopBar(hdc);
    for (int c = 0; c < (int)m_cols.size(); ++c)
        if (!(tl && c < (int)m_tlAbs.size() && m_tlAbs[c])) DrawColumn(hdc, c);
    if (tl)
    {
        BuildTlItems((long long)std::time(nullptr));
        DrawTimeline(hdc);
        m_anyBlink = true;   // die Labels wandern: jede halbe Sekunde ganz neu zeichnen
    }
    else m_tlMenu = false;

    // Gezogener Streifen schwebt ueber allem
    if (m_mode == M_DRAG_STRIP)
    {
        int bay = -1;
        const StripData* s = FindStrip(m_dragCs, &bay);
        if (s)
        {
            int x = m_curPt.x - m_grabOffset.x, y = m_curPt.y - m_grabOffset.y;
            if (m_tlPress)
            {
                // aus dem Zeitstrahl gezogen: als Label (ohne Spitze). Bleibt es auf seiner Seite, zeigt eine
                // gestrichelte Linie die neue Zeit (DFS: Reihenfolge durch Verschieben entlang der Zeitleiter).
                RECT rc = Rc(x, y, x + std::max(Sx(140), m_dragW), y + Sx(60));
                TlItem dummy;
                for (const TlItem& it : m_tlItems) if (it.cs == m_dragCs) dummy = it;
                DrawTlGhostPlan(hdc, rc, dummy);
                DrawTlLabel(hdc, rc, 0, 0, 0, *s, bay, &dummy, true);
            }
            else
            {
                RECT rc = Rc(x, y, x + m_dragW, y + m_stripH);
                DrawStrip(hdc, rc, *s, bay, true);
            }
        }
    }

    if (!m_detailCs.empty())
    {
        if (m_tlMenu) DrawTlMenu(hdc, width, height);
        else DrawDetail(hdc, width, height);
    }
    if (m_trashOpen) DrawTrash(hdc, width, height);
    if (m_dlOpen) DrawDatalink(hdc, width, height);
    if (m_setOpen) DrawSettings(hdc, width, height);
    if (m_kpKind != KP_NONE) DrawKeypad(hdc, width, height);
    if (m_viewMenu) DrawViewMenu(hdc, width, height);
    if (m_areaMenu) DrawAreaMenu(hdc, width, height);
    if (m_pickOpen) DrawPicker(hdc, width, height);
    if (m_editing) DrawEditor(hdc, width, height);
    DrawNoteAndOffline(hdc, width, height);
    if (dfs) SetTheme(m_light);
}

void StripWindow::DrawButton(HDC hdc, const RECT& rc, const std::wstring& text, COLORREF bg, COLORREF fg,
                             int action, bool active)
{
    bool pressed = (m_mode == M_PRESS_BUTTON && m_pressedAction == action && PtInRect(&rc, m_curPt));
    COLORREF fill = bg;
    if (pressed) fill = RGB(GetRValue(bg) * 3 / 4, GetGValue(bg) * 3 / 4, GetBValue(bg) * 3 / 4);
    // flache Taste mit feinem Rand, aktiv mit kraeftigem Rand
    COLORREF edge = RGB(GetRValue(fill) * 7 / 10, GetGValue(fill) * 7 / 10, GetBValue(fill) * 7 / 10);
    RoundBox(hdc, rc, 3, fill, edge);
    if (active) Frame(hdc, rc, col::text, Sx(2));
    Text(hdc, rc, text, m_fButton, fg, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    Button b;
    b.rc = rc;
    b.action = action;
    m_buttons.push_back(b);
}

// Passt der Text nicht in die Zelle, die schmalere Schrift nehmen
void StripWindow::FitText(HDC hdc, const RECT& rc, const std::wstring& text, HFONT normal, HFONT narrow, COLORREF color)
{
    HFONT f = normal;
    if (TextWidth(hdc, text, normal) > rc.right - rc.left) f = narrow;
    Text(hdc, rc, text, f, color);
}

void StripWindow::DrawTopBar(HDC hdc)
{
    Fill(hdc, m_topBar, col::topBar);
    Fill(hdc, Rc(0, m_topBar.bottom - 1, m_width, m_topBar.bottom), col::bayBorder);
    const int row1 = Sx(48);   // Zeile mit Titel und Buttons

    int x = Sx(16);

    std::wstring apt = m_board.airports.empty() ? L"no airport active" : W(m_board.airports);
    Text(hdc, Rc(x, 0, x + Sx(320), row1), apt, m_fTitle, col::text);
    x += std::min(Sx(320), TextWidth(hdc, apt, m_fTitle)) + Sx(14);

    // Streifen ohne Pistengruppe stehen bei beiden Pisten, nur einmal zaehlen
    std::set<std::string> unique;
    for (const Column& c : m_cols)
        for (const StripRef& r : c.strips) unique.insert(m_board.bays[r.bay][r.idx].callsign);
    wchar_t cnt[64];
    swprintf(cnt, 64, L"%d strips", (int)unique.size());
    Text(hdc, Rc(x, 0, x + Sx(110), row1), cnt, m_fHeader, col::textDim);
    x += std::min(Sx(110), TextWidth(hdc, cnt, m_fHeader)) + Sx(14);
    // Betriebsrichtung gewechselt: ein Tipp stellt alle noch nicht rollenden Abfluege um
    if (m_board.rwySwitchCount > 0)
    {
        wchar_t lab[96];
        swprintf(lab, 96, L"Runway change: %d \x2192 %ls", m_board.rwySwitchCount, W(m_board.rwySwitchTo).c_str());
        int bw = TextWidth(hdc, lab, m_fButton) + Sx(24);
        int bh2 = Sx(34);
        DrawButton(hdc, Rc(x, (row1 - bh2) / 2, x + bw, (row1 - bh2) / 2 + bh2), lab, RGB(250, 180, 70), col::onAccent,
                   A_RWY_SWITCH, false);
        x += bw + Sx(10);
    }

    // Buttons rechts
    int bh = Sx(34), by = (row1 - bh) / 2;
    int r = m_width - Sx(10);
    RECT rHide  = Rc(r - Sx(44), by, r, by + bh);               r = rHide.left - Sx(8);
    RECT rFull  = Rc(r - Sx(104), by, r, by + bh);              r = rFull.left - Sx(8);
    RECT rIn    = Rc(r - Sx(44), by, r, by + bh);               r = rIn.left - Sx(6);
    RECT rOut   = Rc(r - Sx(44), by, r, by + bh);               r = rOut.left - Sx(8);
    RECT rTheme = Rc(r - Sx(104), by, r, by + bh);              r = rTheme.left - Sx(8);
    std::wstring trashLab = L"Recover";
    if (!m_board.trash.empty()) trashLab += L" " + std::to_wstring(m_board.trash.size());
    RECT rTrash = Rc(r - std::max(Sx(110), TextWidth(hdc, trashLab, m_fButton) + Sx(20)), by, r, by + bh);
    r = rTrash.left - Sx(8);
    RECT rNew   = Rc(r - Sx(100), by, r, by + bh);              r = rNew.left - Sx(8);
    // Tower: Zeitstrahl (DFS-Planungsanzeige) oder Spalten
    if (m_view == VIEW_TWR)
    {
        std::wstring tl = L"Timeline";
        int tw = TextWidth(hdc, tl, m_fButton) + Sx(28);
        RECT rTl = Rc(r - tw, by, r, by + bh);
        r = rTl.left - Sx(8);
        DrawButton(hdc, rTl, tl, m_board.opt.timeline ? col::ok : col::button, m_board.opt.timeline ? RGB(255, 255, 255) : col::text,
                   A_TIMELINE, false);
        // wie in der Statusleiste der DFS-Anzeige: Tag / Nacht direkt umschalten
        if (m_board.opt.timeline)
        {
            bool dfs = m_board.opt.tlDfs;
            bool night = dfs ? m_board.opt.tlNight : !m_light;
            std::wstring dn = night ? L"Day" : L"Night";
            int dw = TextWidth(hdc, dn, m_fButton) + Sx(24);
            RECT rDn = Rc(r - dw, by, r, by + bh);
            r = rDn.left - Sx(8);
            DrawButton(hdc, rDn, dn, col::button, col::text, dfs ? A_TL_DAYNIGHT : A_THEME, false);
        }
    }
    // Rueckgaengig (nur wenn es etwas gibt)
    if (!m_board.undoLabel.empty())
    {
        std::wstring ul = L"\x21B6 Undo";
        RECT rUndo = Rc(r - (TextWidth(hdc, ul, m_fButton) + Sx(24)), by, r, by + bh);
        r = rUndo.left - Sx(8);
        DrawButton(hdc, rUndo, ul, col::button, col::text, A_UNDO, false);
    }
    // Rechts der Bereich (Dropdown, nur bei getrennten Pisten), links davon die Station (Dropdown)
    // Bereich (Nord/Sued): beim Tower immer, bei Ground/Apron/Delivery nur mit getrenntem Ground (Berlin)
    if (m_board.rwySplit && m_view != VIEW_DEL && (m_board.splitGnd || m_view == VIEW_TWR || m_view == VIEW_ALL))
    {
        std::wstring area = AreaLabel(m_rwyFilter) + L"  \x25BE";
        int aw = std::max(Sx(130), TextWidth(hdc, area, m_fButton) + Sx(28));
        RECT rArea = Rc(r - aw, by, r, by + bh);
        r = rArea.left - Sx(6);
        m_areaBtn = rArea;
        DrawButton(hdc, rArea, area, col::button, col::text, A_AREA_MENU, m_areaMenu);
        if (m_rwyFilter) Fill(hdc, Rc(rArea.left + 1, rArea.top + 1, rArea.left + Sx(6), rArea.bottom - 1), kRwyAccent[m_rwyFilter]);
    }
    else m_areaMenu = false;
    std::wstring station = StationLabel() + L"  \x25BE";
    int vw = std::max(Sx(150), TextWidth(hdc, station, m_fButton) + Sx(28));
    RECT rView  = Rc(r - vw, by, r, by + bh);                   r = rView.left - Sx(8);
    m_viewBtn = rView;
    r -= Sx(8);
    int va = ViewOf(m_view, m_board).accent;
    DrawButton(hdc, rView, station, col::button, col::text, A_VIEW, m_viewMenu);
    if (va >= 0) Fill(hdc, Rc(rView.left + 1, rView.top + 1, rView.left + Sx(6), rView.bottom - 1), kBayAccent[va]);
    DrawButton(hdc, rTheme, L"Settings", col::button, col::text, A_SETTINGS, m_setOpen);
    DrawButton(hdc, rNew, L"+ Strip", col::button, col::text, A_NEW_STRIP, false);
    DrawButton(hdc, rTrash, trashLab, col::button, m_board.trash.empty() ? col::textDim : col::text, A_TRASH_OPEN, false);
    DrawButton(hdc, rOut, L"\x2212", col::button, col::text, A_ZOOM_OUT, false);
    DrawButton(hdc, rIn, L"+", col::button, col::text, A_ZOOM_IN, false);
    DrawButton(hdc, rFull, m_fullscreen ? L"Exit full" : L"Fullscreen", col::button, col::text, A_FULLSCREEN, m_fullscreen);
    DrawButton(hdc, rHide, L"\x00D7", col::button, col::text, A_HIDE, false);

    SYSTEMTIME st;
    GetSystemTime(&st);
    wchar_t clock[32];
    swprintf(clock, 32, L"%02d:%02d:%02dZ", st.wHour, st.wMinute, st.wSecond);
    int clockW = TextWidth(hdc, clock, m_fTitle) + Sx(8);
    bool clockRow2 = m_wxRow && r - clockW < x;   // schmales Fenster: Uhr rechts in der zweiten Zeile
    if (!clockRow2) Text(hdc, Rc(r - clockW, 0, r, row1), clock, m_fTitle, col::text, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    int wxRight = clockRow2 ? r : r - clockW - Sx(16);
    // vACDM-Fehler: rote Marke neben der Uhr (Text der Meldung, gekuerzt)
    if (!m_board.vacdmError.empty())
    {
        std::wstring t = L"vACDM: " + W(m_board.vacdmError.substr(0, 28));
        int pw = TextWidth(hdc, t, m_fButton) + Sx(16);
        RECT pr = Rc(wxRight - pw, (row1 - Sx(28)) / 2, wxRight, (row1 - Sx(28)) / 2 + Sx(28));
        RoundBox(hdc, pr, Sx(6), RGB(200, 50, 40), RGB(200, 50, 40));
        Text(hdc, pr, t, m_fButton, RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        wxRight = pr.left - Sx(10);
    }
    int wxTop = 0, wxBottom = row1, wxBy = by;
    if (m_wxRow)
    {
        // zweite Zeile ueber die ganze Breite
        x = Sx(16);
        wxRight = m_width - Sx(10);
        wxTop = row1 - Sx(4);
        wxBottom = m_topBar.bottom - Sx(2);
        wxBy = wxTop + (wxBottom - wxTop - Sx(28)) / 2;
        if (clockRow2)
        {
            Text(hdc, Rc(wxRight - clockW, wxTop, wxRight, wxBottom), clock, m_fTitle, col::text, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            wxRight -= clockW + Sx(14);
        }
    }

    // ATIS-Buchstabe als Kasten, nach einem Wechsel eine Minute lang orange
    if (!m_board.atis.empty() && wxRight - x > Sx(80))
    {
        std::wstring at = L"ATIS " + W(m_board.atis);
        int aw = TextWidth(hdc, at, m_fTitle) + Sx(20);
        RECT ra = Rc(x, wxBy, std::min(x + aw, wxRight), wxBy + (m_wxRow ? Sx(28) : bh));
        bool fresh = m_atisChanged > 0 && (long long)std::time(nullptr) - m_atisChanged < 60;
        COLORREF bg = fresh ? RGB(250, 160, 40) : kBayAccent[BAY_ONFREQ];
        RoundBox(hdc, ra, Sx(8), bg, bg);
        Text(hdc, ra, at, m_fTitle, col::onAccent, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        x = ra.right + Sx(12);
    }
    // METAR im restlichen Platz (wird bei Bedarf mit ... gekuerzt)
    if (wxRight - x > Sx(60))
    {
        std::wstring mt = m_board.metar.empty()
                              ? (m_board.wxAirport.empty() ? std::wstring() : W(m_board.wxAirport) + L": no METAR")
                              : W(m_board.metar);
        // passt der METAR nicht, nur das Wichtigste: Wind und QNH
        if (!m_board.metar.empty() && TextWidth(hdc, mt, m_fHeader) > wxRight - x) mt = W(ShortMetar(m_board.metar));
        Text(hdc, Rc(x, wxTop, wxRight, wxBottom), mt, m_fHeader, m_board.metar.empty() ? col::textDim : col::text);
    }
}

void StripWindow::DrawColumn(HDC hdc, int b)
{
    const BayRect& br = m_cols[b].rect;
    const ColumnDef& def = ViewOf(m_view, m_board).cols[m_cols[b].def];
    const COLORREF accent = kBayAccent[def.accent];
    bool dropTarget = (m_mode == M_DRAG_STRIP && m_dropCol == b);

    RoundBox(hdc, br.frame, Sx(8), col::bay, dropTarget ? accent : col::bayBorder);
    if (dropTarget) Frame(hdc, Inset(br.frame, 1, 1), accent, Sx(2));

    // Kopfzeile
    RECT hdr = Inset(br.header, Sx(2), Sx(2));
    hdr.bottom = br.header.bottom;
    Fill(hdc, hdr, col::bayHeader);
    Fill(hdc, Rc(br.header.left, br.header.bottom - 1, br.header.right, br.header.bottom), col::bayBorder);

    // Runway-Bay: Kopf antippen schaltet die Piste gelb / rot / frei (Kopf in der Farbe)
    if (def.dropBay == BAY_RUNWAY && b < 32)
    {
        int st = RwyStateOf(b);
        if (st == 1) Fill(hdc, hdr, RGB(250, 210, 60));
        else if (st == 2)
        {
            // gesperrt: rot, die ganze Bay schraffiert
            Fill(hdc, hdr, RGB(220, 60, 50));
            HPEN pen = CreatePen(PS_SOLID, std::max(1, Sx(2)), RGB(235, 170, 165));
            HGDIOBJ op = SelectObject(hdc, pen);
            int sv = SaveDC(hdc);
            IntersectClipRect(hdc, br.body.left, br.body.top, br.body.right, br.body.bottom);
            for (int xx = br.body.left - (br.body.bottom - br.body.top); xx < br.body.right; xx += Sx(18))
            {
                MoveToEx(hdc, xx, br.body.bottom, nullptr);
                LineTo(hdc, xx + (br.body.bottom - br.body.top), br.body.top);
            }
            RestoreDC(hdc, sv);
            SelectObject(hdc, op);
            DeleteObject(pen);
        }
        Button hb;
        hb.rc = Rc(hdr.left, hdr.top, hdr.right - Sx(50), hdr.bottom);
        hb.action = A_RWYST_BASE + b;
        m_buttons.push_back(hb);
    }
    // Pistenspalte: aktive Piste im Namen ("RUNWAY 25R"); Bay-Namen ohne Farben (2.0.43)
    int titleRight = hdr.right - Sx(50);
    {
        std::wstring ttl = ColTitle(b);
        int st = def.dropBay == BAY_RUNWAY ? RwyStateOf(b) : 0;
        if (st == 1) ttl += L"  \x00B7  CAUTION";
        else if (st == 2) ttl += L"  \x00B7  CLOSED";
        // Caution (Gelb): immer dunkle Schrift - helle Schrift im Dunkelschema ist darauf kaum zu lesen
        Text(hdc, Rc(hdr.left + Sx(14), hdr.top, titleRight, hdr.bottom), ttl, m_fHeader,
             st == 2 ? RGB(255, 255, 255) : st == 1 ? RGB(24, 22, 16) : col::text);
    }

    int n = (int)m_cols[b].strips.size();
    wchar_t cnt[16];
    swprintf(cnt, 16, L"%d", n);
    RECT pill = Rc(hdr.right - Sx(44), hdr.top, hdr.right - Sx(10), hdr.bottom);
    int hst = def.dropBay == BAY_RUNWAY && b < 32 ? RwyStateOf(b) : 0;
    COLORREF cntCol = hst == 1 ? RGB(24, 22, 16) : hst == 2 ? RGB(255, 255, 255) : n ? col::text : col::textDim;
    Text(hdc, pill, cnt, m_fHeader, cntCol, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    // Streifen
    int saved = SaveDC(hdc);
    IntersectClipRect(hdc, br.body.left + 1, br.body.top, br.body.right - 1, br.body.bottom - 1);

    bool dragging = (m_mode == M_DRAG_STRIP);
    int slot = 0;
    auto drawPlaceholder = [&](int s) {
        RECT rc = SlotRect(b, s);
        HPEN pen = CreatePen(PS_DOT, 1, accent);
        HGDIOBJ op = SelectObject(hdc, pen);
        HGDIOBJ ob = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        SetBkMode(hdc, TRANSPARENT);
        RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, Sx(6), Sx(6));
        SelectObject(hdc, ob);
        SelectObject(hdc, op);
        DeleteObject(pen);
    };

    for (const StripRef& ref : m_cols[b].strips)
    {
        const StripData& s = m_board.bays[ref.bay][ref.idx];
        if (dragging && s.callsign == m_dragCs) continue;
        if (dragging && b == m_dropCol && slot == m_dropIndex) drawPlaceholder(slot++);
        RECT rc = SlotRect(b, slot);
        if (rc.bottom >= br.body.top && rc.top <= br.body.bottom) DrawStrip(hdc, rc, s, ref.bay, false);
        ++slot;
    }
    if (dragging && b == m_dropCol && slot == m_dropIndex) drawPlaceholder(slot++);

    if (slot == 0)
        Text(hdc, Rc(br.body.left, br.body.top + Sx(14), br.body.right, br.body.top + Sx(40)),
             L"no strips", m_fHeader, col::emptyText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    RestoreDC(hdc, saved);

    // Scrollleiste
    if (br.overflow)
    {
        RECT t = br.track;
        RoundBox(hdc, t, Sx(8), col::scrollTrack, col::scrollTrack);
        int bodyH = br.body.bottom - br.body.top;
        int trackH = t.bottom - t.top;
        int thumbH = std::max(Sx(30), trackH * bodyH / std::max(1, br.contentHeight));
        int maxScroll = std::max(1, br.contentHeight - bodyH);
        int thumbY = t.top + (trackH - thumbH) * m_scroll[b] / maxScroll;
        RoundBox(hdc, Rc(t.left, thumbY, t.right, thumbY + thumbH), Sx(8), col::scrollThumb, col::scrollThumb);
    }
}

// ---------------------------------------------------------------------------
// Zeitstrahl (Tower) nach der DFS-Planungsanzeige "WFF TWR-HMI"
// ---------------------------------------------------------------------------
// Vorbild: Bergner (DFS), Koenig, Hofmann (TU Darmstadt), Ebert (delair): "Entwurf einer integrierten
// Planungsanzeige fuer den Tower-Controller", DLRK 2008; Koenig et al.: "Inkrementelle nutzergerechte
// Etablierung eines Towerlotsen-HMI", Mensch & Computer 2009; iF design award 2009 (FRA, MUC, BER).
// Uebernommen:
//  - Farbkonzept auf dunkelgrauem Grund; Landungen dunkelgrau, Starts hellgrau; Status in hoch
//    gesaettigten Farben, Abflugstrecken (SID) in Pastell; gut erkennbar und blendfrei bei Tag und Nacht
//  - waagerechte Zeitleiter, 3 min Vergangenheit bis 6 min Zukunft, Jetzt links der Mitte; Abstand = Zeit
//  - Ankuenfte direkt an der Zeitleiter, Abfluege weiter aussen; erste Piste oben, zweite unten
//  - Labelkopf zeigt den Status; Statusbalken links; Klick auf den Kopf = naechster Status;
//    die Felder sind einzeln waehlbar (virtuelle Tastatur oder Liste); Menue am Label (Radierer)
//  - Ziehen entlang der Zeitleiter aendert die Reihenfolge der Starts bzw. Landungen
//  - Bereiche: Hand Over In / Hand Over Out je Piste, Statusleisten mit Tag/Nacht
// Berlin: Nordbahn 24R/06L oben, Suedbahn 24L/06R unten (seit 3.10.2024; Name aus der aktiven Piste).

static bool IsTransientScratchW(const std::string& sp);   // weiter unten (Editor)

static std::wstring Hhmm(long long t)
{
    if (t <= 0) return L"\x2013";
    wchar_t b[8];
    swprintf(b, 8, L"%02d%02d", (int)(t % 86400 / 3600), (int)(t % 3600 / 60));
    return b;
}

// "HHMM" (UTC) -> Zeitpunkt nahe bei now (gestern, heute oder morgen)
static long long HhmmNear(const std::string& s, long long now)
{
    if (s.size() < 4) return 0;
    for (int i = 0; i < 4; ++i) if (!isdigit((unsigned char)s[i])) return 0;
    int h = (s[0] - '0') * 10 + (s[1] - '0'), m = (s[2] - '0') * 10 + (s[3] - '0');
    if (h > 23 || m > 59) return 0;
    long long t = now - now % 86400 + h * 3600LL + m * 60LL;
    if (t - now > 43200) t -= 86400;
    else if (now - t > 43200) t += 86400;
    return t;
}

static std::string HhmmssStr(long long t)
{
    char b[8];
    snprintf(b, sizeof(b), "%02d%02d%02d", (int)(t % 86400 / 3600), (int)(t % 3600 / 60), (int)(t % 60));
    return b;
}

// SID-Gruppe: Pastellton nach dem Wegpunkt der SID (gleicher Wegpunkt = gleiche Farbe, auf allen Plaetzen gleich)
static COLORREF SidPastel(const std::string& sid)
{
    static const COLORREF k[8] = { RGB(255, 208, 208), RGB(255, 224, 182), RGB(248, 240, 160), RGB(204, 238, 192),
                                   RGB(186, 228, 240), RGB(204, 212, 252), RGB(236, 206, 246), RGB(212, 232, 212) };
    std::string base;
    for (char c : sid)
    {
        if (isdigit((unsigned char)c)) break;
        base += (char)toupper((unsigned char)c);
    }
    if (base.empty()) return RGB(236, 236, 236);
    unsigned h = 2166136261u;
    for (char c : base) { h ^= (unsigned char)c; h *= 16777619u; }
    return k[h % 8];
}

static COLORREF TlMix(COLORREF a, COLORREF b, int pb)
{
    pb = std::max(0, std::min(100, pb));
    int pa = 100 - pb;
    return RGB((GetRValue(a) * pa + GetRValue(b) * pb) / 100, (GetGValue(a) * pa + GetGValue(b) * pb) / 100,
               (GetBValue(a) * pa + GetBValue(b) * pb) / 100);
}

// Schrift auf einer Farbe: schwarz auf hell, weiss auf dunkel
static COLORREF TlOn(COLORREF c)
{
    return (GetRValue(c) * 299 + GetGValue(c) * 587 + GetBValue(c) * 114) / 1000 >= 140 ? RGB(0, 0, 0) : RGB(255, 255, 255);
}

static void TlUnion(RECT& a, const RECT& b)
{
    if (a.right <= a.left || a.bottom <= a.top) { a = b; return; }
    a.left = std::min(a.left, b.left); a.top = std::min(a.top, b.top);
    a.right = std::max(a.right, b.right); a.bottom = std::max(a.bottom, b.bottom);
}

static void TlTri(HDC hdc, POINT a, POINT b, POINT c, COLORREF fill, COLORREF edge)
{
    HGDIOBJ ob = SelectObject(hdc, GetStockObject(DC_BRUSH)), op = SelectObject(hdc, GetStockObject(DC_PEN));
    SetDCBrushColor(hdc, fill);
    SetDCPenColor(hdc, edge);
    POINT pts[3] = { a, b, c };
    Polygon(hdc, pts, 3);
    SelectObject(hdc, ob);
    SelectObject(hdc, op);
}

static void TlLine(HDC hdc, int x0, int y0, int x1, int y1, COLORREF c, int w, int style = PS_SOLID)
{
    HPEN pen = CreatePen(style, w, c);
    HGDIOBJ op = SelectObject(hdc, pen);
    SetBkMode(hdc, TRANSPARENT);
    MoveToEx(hdc, x0, y0, nullptr);
    LineTo(hdc, x1, y1);
    SelectObject(hdc, op);
    DeleteObject(pen);
}

static void TlHatch(HDC hdc, const RECT& r, COLORREF c, int step, int w)
{
    HPEN pen = CreatePen(PS_SOLID, w, c);
    HGDIOBJ op = SelectObject(hdc, pen);
    int sv = SaveDC(hdc);
    IntersectClipRect(hdc, r.left, r.top, r.right, r.bottom);
    int h = r.bottom - r.top;
    for (int xx = r.left - h; xx < r.right; xx += step)
    {
        MoveToEx(hdc, xx, r.bottom, nullptr);
        LineTo(hdc, xx + h, r.top);
    }
    RestoreDC(hdc, sv);
    SelectObject(hdc, op);
    DeleteObject(pen);
}

// Papier, Schrift, Rand und Klickfelder eines Labels (Ankunft dunkelgrau, Abflug hellgrau)
static void TlColors(const StripData& s, bool preview, const TlPal& P, COLORREF& body, COLORREF& ink, COLORREF& dim,
                     COLORREF& edge, COLORREF& field, COLORREF& fieldEdge)
{
    if (s.arrival)
    {
        body = P.arrBody; ink = P.arrInk; dim = P.arrDim; edge = P.arrEdge; field = P.arrField; fieldEdge = P.arrFieldEdge;
    }
    else
    {
        body = P.depBody; ink = P.depInk; dim = P.depDim; edge = P.depEdge; field = P.depField; fieldEdge = P.depFieldEdge;
    }
    if (s.freeStrip)
    {
        body = TlMix(P.depBody, RGB(236, 232, 200), 45);
        ink = P.depInk; dim = P.depDim; edge = P.depEdge;
        field = TlMix(body, RGB(0, 0, 0), 8); fieldEdge = TlMix(body, RGB(0, 0, 0), 30);
    }
    if (s.hl > 0 && s.hl < 6)
    {
        body = TlMix(kHlPaper[s.hl], RGB(0, 0, 0), 100 - P.statusPct);
        ink = RGB(0, 0, 0); dim = RGB(60, 60, 60); edge = kHlStrong[s.hl];
        field = TlMix(body, RGB(0, 0, 0), 8); fieldEdge = TlMix(body, RGB(0, 0, 0), 32);
    }
    else if (s.offerForMe)
    {
        bool on = s.offerBlink && (GetTickCount() / 500) % 2 == 0;
        body = on ? RGB(255, 196, 110) : RGB(255, 228, 186);
        ink = RGB(0, 0, 0); dim = RGB(70, 60, 40); edge = RGB(236, 140, 30);
        field = TlMix(body, RGB(0, 0, 0), 8); fieldEdge = TlMix(body, RGB(0, 0, 0), 30);
    }
    if (preview)
    {
        // Hand Over In: noch bei Ground bzw. beim Anflug - blass
        body = TlMix(body, P.bg, 45);
        ink = TlMix(ink, body, 35);
        dim = TlMix(dim, body, 35);
        edge = TlMix(edge, P.bg, 40);
        field = TlMix(field, P.bg, 45);
        fieldEdge = TlMix(fieldEdge, P.bg, 45);
    }
}

TlPal StripWindow::TlPalette() const
{
    TlPal P;
    if (m_board.opt.tlDfs && !m_board.opt.tlNight)
    {
        // DFS, Tag: dunkelgrauer Grund
        P.bg = RGB(44, 46, 50);  P.side = RGB(33, 35, 38);  P.past = RGB(52, 54, 59);  P.axisBg = RGB(72, 75, 81);
        P.grid = RGB(56, 59, 64); P.sep = RGB(86, 90, 97);  P.conn = RGB(150, 155, 163);
        P.text = RGB(232, 234, 238); P.textDim = RGB(150, 155, 163); P.frame = RGB(255, 255, 255);
        P.hover = RGB(0, 204, 255); P.nowC = RGB(232, 72, 52); P.plan = RGB(80, 210, 255);
        P.arrBody = RGB(98, 102, 110); P.arrInk = RGB(255, 255, 255); P.arrDim = RGB(208, 212, 218); P.arrEdge = RGB(132, 137, 145);
        P.arrField = RGB(84, 88, 95);  P.arrFieldEdge = RGB(138, 143, 151);
        P.depBody = RGB(196, 199, 204); P.depInk = RGB(10, 11, 13); P.depDim = RGB(66, 70, 76); P.depEdge = RGB(228, 230, 234);
        P.depField = RGB(180, 183, 189); P.depFieldEdge = RGB(140, 144, 151);
        P.statusPct = 100;
    }
    else if (m_board.opt.tlDfs)
    {
        // DFS, Nacht: fast schwarz, alles gedimmt
        P.bg = RGB(15, 16, 18);  P.side = RGB(9, 10, 11);  P.past = RGB(22, 23, 26);  P.axisBg = RGB(38, 40, 44);
        P.grid = RGB(26, 28, 31); P.sep = RGB(48, 51, 56); P.conn = RGB(100, 104, 110);
        P.text = RGB(184, 188, 194); P.textDim = RGB(112, 116, 122); P.frame = RGB(214, 216, 220);
        P.hover = RGB(0, 150, 200); P.nowC = RGB(186, 56, 40); P.plan = RGB(60, 160, 200);
        P.arrBody = RGB(66, 69, 75); P.arrInk = RGB(224, 226, 230); P.arrDim = RGB(160, 164, 170); P.arrEdge = RGB(88, 92, 98);
        P.arrField = RGB(56, 59, 64); P.arrFieldEdge = RGB(94, 98, 104);
        P.depBody = RGB(138, 141, 146); P.depInk = RGB(8, 9, 10); P.depDim = RGB(44, 46, 50); P.depEdge = RGB(160, 163, 168);
        P.depField = RGB(124, 127, 132); P.depFieldEdge = RGB(98, 101, 106);
        P.statusPct = 78;
    }
    else if (m_light)
    {
        P.bg = RGB(243, 244, 246); P.side = RGB(231, 234, 238); P.past = RGB(229, 231, 235); P.axisBg = RGB(212, 216, 222);
        P.grid = RGB(222, 225, 230); P.sep = RGB(192, 198, 206); P.conn = RGB(116, 122, 132);
        P.text = col::text; P.textDim = col::textDim; P.frame = RGB(0, 0, 0);
        P.hover = RGB(0, 132, 220); P.nowC = RGB(222, 64, 44); P.plan = RGB(0, 110, 180);
        P.arrBody = RGB(84, 88, 96); P.arrInk = RGB(255, 255, 255); P.arrDim = RGB(206, 210, 216); P.arrEdge = RGB(50, 54, 60);
        P.arrField = RGB(72, 76, 84); P.arrFieldEdge = RGB(118, 123, 131);
        P.depBody = RGB(208, 211, 215); P.depInk = RGB(10, 12, 14); P.depDim = RGB(70, 76, 84); P.depEdge = RGB(140, 146, 154);
        P.depField = RGB(194, 197, 202); P.depFieldEdge = RGB(156, 161, 168);
        P.statusPct = 100;
    }
    else
    {
        P.bg = RGB(33, 35, 38); P.side = RGB(28, 30, 33); P.past = RGB(39, 41, 45); P.axisBg = RGB(52, 55, 60);
        P.grid = RGB(43, 46, 50); P.sep = RGB(64, 68, 74); P.conn = RGB(150, 155, 163);
        P.text = col::text; P.textDim = col::textDim; P.frame = RGB(255, 255, 255);
        P.hover = RGB(0, 190, 255); P.nowC = RGB(222, 64, 44); P.plan = RGB(80, 200, 255);
        P.arrBody = RGB(98, 103, 112); P.arrInk = RGB(255, 255, 255); P.arrDim = RGB(206, 210, 216); P.arrEdge = RGB(60, 64, 72);
        P.arrField = RGB(86, 91, 100); P.arrFieldEdge = RGB(132, 137, 146);
        P.depBody = RGB(172, 176, 182); P.depInk = RGB(10, 12, 14); P.depDim = RGB(60, 66, 74); P.depEdge = RGB(120, 126, 134);
        P.depField = RGB(158, 162, 168); P.depFieldEdge = RGB(120, 125, 132);
        P.statusPct = 100;
    }
    return P;
}

bool StripWindow::TimelineActive() const
{
    return m_view == VIEW_TWR && m_board.opt.timeline && !m_tlSideGroup.empty();
}

// Hannover (ein Tower, drei Bahnen): im Zeitstrahl trotzdem zwei Seiten - oben die Nordbahn 09L/27R
// mit der kleinen 09C/27C (liegt dicht daneben, Abfluege von 09L/27R kreuzen sie), unten die Suedbahn
// 09R/27L. Die Seite haengt allein an der Piste im Flugplan; Hubschrauber bleiben in ihren Spalten.
bool StripWindow::TlSplitEddv() const
{
    return !m_board.rwySplit && m_board.profile == "EDDV" && m_view == VIEW_TWR && m_board.opt.timeline;
}

static std::string TlNormRwy(const std::string& x)
{
    std::string r;
    for (char ch : x) if (isalnum((unsigned char)ch)) r += (char)toupper((unsigned char)ch);
    if (r.size() == 2 && isalpha((unsigned char)r[1])) r = "0" + r;
    return r;
}

int StripWindow::TlRwyGroup(const StripData& s) const
{
    if (m_board.rwySplit || !TlSplitEddv()) return s.rwyGroup;
    if (s.heli || s.freeStrip) return RWY_NONE;
    std::string r = TlNormRwy(s.arrival ? s.arrRwy : s.rwy);
    if (r == "09L" || r == "27R" || r == "09C" || r == "27C") return RWY_A;
    if (r == "09R" || r == "27L") return RWY_B;
    return RWY_NONE;
}

// Auf die andere Seite: getrennte Tower (Berlin, Leipzig) ueber die Pistengruppe, Hannover direkt die Piste
// der anderen Bahn in derselben Richtung (sonst die aktive, sonst 27 = Vorzugsrichtung).
void StripWindow::TlSetGroup(const std::string& cs, int group)
{
    if (!m_host || (group != RWY_A && group != RWY_B)) return;
    if (m_board.rwySplit) { m_host->Command(".strips rwygroup " + cs + (group == RWY_A ? " A" : " B")); return; }
    const StripData* s = FindStrip(cs);
    if (!s || s->freeStrip || s->heli) return;
    std::string cur = TlNormRwy(s->arrival ? s->arrRwy : s->rwy), dir;
    if (cur.size() >= 2 && (cur.compare(0, 2, "09") == 0 || cur.compare(0, 2, "27") == 0)) dir = cur.substr(0, 2);
    if (dir.empty() && m_board.rwyActive[group].size() >= 2) dir = m_board.rwyActive[group].substr(0, 2);
    if (dir != "09") dir = "27";
    std::string pick = group == RWY_A ? (dir == "09" ? "09L" : "27R") : (dir == "09" ? "09R" : "27L");
    if (pick != cur) m_host->SetField(cs, F_RWY, pick);
}

// Runway-Spalte eines Streifens auf seiner Seite (Hannover: 09C/27C hat eine eigene, mit eigenem Status)
int StripWindow::TlRwyColFor(const StripData& s, int side) const
{
    if (side < 0 || side >= (int)m_tlRwyCol.size()) return -1;
    if (side < (int)m_tlRwyCols.size())
        for (int c : m_tlRwyCols[side])
        {
            const ColumnDef& d = ViewOf(m_view, m_board).cols[m_cols[c].def];
            if (d.rwyMatch && RwyColMatch(d.rwyMatch, s, false)) return c;
        }
    return m_tlRwyCol[side];
}

long long StripWindow::TlX0() const
{
    return m_tlNow - std::max(1, m_board.opt.tlPast) * 60LL;
}

int StripWindow::TlX(long long t) const
{
    long long span = (std::max(1, m_board.opt.tlPast) + std::max(3, m_board.opt.tlFuture)) * 60LL;
    double f = (double)(t - TlX0()) / (double)span;
    return m_tlAx0 + (int)std::lround(f * (double)(m_tlAx1 - m_tlAx0));
}

long long StripWindow::TlTimeAt(int x) const
{
    long long span = (std::max(1, m_board.opt.tlPast) + std::max(3, m_board.opt.tlFuture)) * 60LL;
    if (m_tlAx1 <= m_tlAx0) return m_tlNow;
    return TlX0() + (long long)std::llround((double)(x - m_tlAx0) * (double)span / (double)(m_tlAx1 - m_tlAx0));
}

// Klickfeld merken (Ziel beim Antippen) und - Maus darueber - umrahmen
// Seit 4.0.0 gibt es im Label keine einzelnen Klickfelder mehr: das ganze Label ist eine Taste (wie bei der DFS)
int StripWindow::TlZoneAt(POINT, const std::string&) const
{
    return TLZ_BODY;
}

// Label auf seiner Seite entlang der Zeitleiter gezogen: neue Zeit fuer die Reihenfolge (auf 10 s, nicht vor jetzt).
// Gilt fuer alles, was noch nicht gestartet bzw. gelandet ist (nicht fuer Abfluege auf der Piste).
long long StripWindow::TlPlanTarget() const
{
    if (!m_tlPress || !TlAbsorbed(m_dropCol) || m_dropCol != m_dragFromCol) return 0;
    if (m_curPt.x < m_tlAxis.left || m_curPt.x >= m_tlAxis.right) return 0;
    int bay = -1;
    const StripData* s = FindStrip(m_dragCs, &bay);
    if (!s || s->freeStrip || s->designator || s->airborne || (s->arrival && s->onGround)) return 0;
    if (!s->arrival && bay == BAY_RUNWAY) return 0;
    long long t = 0;
    for (const TlItem& it : m_tlItems)
        if (it.cs == m_dragCs && it.where == 0 && it.t > 0) t = TlTimeAt(TlX(it.t) + (m_curPt.x - m_downPt.x));
    if (t <= 0) t = TlTimeAt(m_curPt.x);
    long long now = m_tlNow > 0 ? m_tlNow : (long long)std::time(nullptr);
    t = std::max(now, t);
    return (t + 5) / 10 * 10;
}

// Vorschau beim Ziehen: gestrichelte Linie zur neuen Zeit und die Zeit auf der Achse
void StripWindow::DrawTlGhostPlan(HDC hdc, const RECT& rc, TlItem& it)
{
    long long pt = TlPlanTarget();
    if (pt <= 0) return;
    const TlPal P = TlPalette();
    int tx = TlX(pt);
    bool above = (rc.top + rc.bottom) / 2 < (m_tlAxis.top + m_tlAxis.bottom) / 2;
    int ay = above ? (int)m_tlAxis.top : (int)m_tlAxis.bottom;
    int ly = above ? (int)rc.bottom : (int)rc.top;
    int lx = std::max<int>(rc.left + Sx(14), std::min<int>(rc.right - Sx(12), tx));
    TlLine(hdc, lx, ly, tx, ay, P.plan, 1, PS_DASH);
    Fill(hdc, Rc(tx - Sx(1), m_tlAxis.top, tx + Sx(1) + 1, m_tlAxis.bottom), P.plan);
    std::wstring lab = L"\x2192 " + Hhmm(pt);
    int w2 = TextWidth(hdc, lab, m_fStripTiny) + Sx(12);
    RECT nr = Rc(tx - w2 / 2, m_tlAxis.top + Sx(5), tx - w2 / 2 + w2, m_tlAxis.bottom - Sx(5));
    RoundBox(hdc, nr, Sx(3), P.plan, P.plan);
    Text(hdc, nr, lab, m_fStripTiny, TlOn(P.plan), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    it.t = pt;
    it.planned = true;
}

// Status als Farbe (kraeftig) und Kuerzel
COLORREF StripWindow::TlStatus(const StripData& s, int bay, std::wstring& text) const
{
    if (s.freeStrip) { text = s.designator ? L"\x2588" : L"NOTE"; return RGB(130, 130, 130); }
    bool pv = Preview(s, bay) && !s.respMine;
    if (s.arrival)
    {
        if (s.goAround)       { text = L"G/A"; return RGB(214, 48, 40); }
        if (s.onGround)       { text = L"LND"; return RGB(126, 72, 204); }
        if (s.landingCleared) { text = L"LDG"; return RGB(28, 156, 64); }
        if (pv)               { text = L"APP"; return RGB(150, 150, 150); }
        text = L"FIN";
        return RGB(236, 124, 0);
    }
    if (s.airborne)
    {
        text = s.handedOver ? L"OUT" : L"AIR";
        return s.handedOver ? RGB(150, 150, 150) : RGB(0, 160, 204);
    }
    if (bay == BAY_RUNWAY)
    {
        if (s.state == GC_DEPA) { text = L"T/O"; return RGB(28, 156, 64); }
        text = L"L/U";
        return RGB(232, 180, 0);
    }
    if (pv) { text = L"GND"; return RGB(150, 150, 150); }
    text = bay == BAY_TAXI ? L"TAXI" : L"HOLD";
    return RGB(44, 112, 220);
}

void StripWindow::BuildTlItems(long long now)
{
    m_tlItems.clear();
    m_tlNow = now;
    const long long past = std::max(1, m_board.opt.tlPast) * 60LL, fut = std::max(3, m_board.opt.tlFuture) * 60LL;
    std::set<std::string> seen;
    for (int c = 0; c < (int)m_cols.size() && c < (int)m_tlAbs.size(); ++c)
    {
        if (!m_tlAbs[c]) continue;
        int side = (int)(std::find(m_tlSideGroup.begin(), m_tlSideGroup.end(), m_cols[c].group) - m_tlSideGroup.begin());
        if (side >= (int)m_tlSideGroup.size()) side = 0;
        for (const StripRef& r : m_cols[c].strips)
        {
            const StripData& s = m_board.bays[r.bay][r.idx];
            if (!seen.insert(s.callsign).second) continue;
            TlItem it;
            it.cs = s.callsign;
            it.bay = r.bay;
            it.side = side;
            it.lane = s.arrival ? 1 : 0;
            it.order = r.idx;
            if (s.freeStrip) { it.where = 3; m_tlItems.push_back(it); continue; }
            bool done = false;   // gelandet bzw. abgehoben: Zeitpunkt, seit dem er weg ist
            auto since = [&]() { auto f = m_tlSeen.find(s.callsign); return f != m_tlSeen.end() ? f->second : now; };
            if (s.arrival)
            {
                if (s.onGround) { it.t = since(); done = true; }
                else if (s.groundSpeed >= 40 && s.distDest > 0.01) it.t = now + (long long)(s.distDest / s.groundSpeed * 3600.0);
                else if (r.bay == BAY_RUNWAY) it.t = now;
            }
            else if (s.airborne) { it.t = since(); done = true; }
            else if (r.bay == BAY_RUNWAY) it.t = now;    // Line-up / Startfreigabe: belegt die Piste jetzt
            else
            {
                // rollend / am Rollhalt: TTOT (vACDM), sonst der Slot
                it.t = HhmmNear(s.ttot, now);
                if (it.t <= 0 && s.ctotUtc > 0) it.t = s.ctotUtc;
                if (it.t > 0 && it.t < now) { it.t = now; it.overdue = true; }
                // nichts davon: geschaetzt - am Rollhalt (bei mir) in 1 min, rollend in 2,5 min, noch bei Ground in 4 min;
                // die Reihenfolge danach mit Staffelung hinter allem, was schon eine Zeit hat (TlEstimate)
                bool pvw = Preview(s, r.bay) && !s.respMine;
                it.earliest = now + (r.bay == BAY_HANDOFF ? 60 : pvw ? 240 : 150);
                if (it.t <= 0 && m_board.opt.tlEst && s.planUtc <= 0) it.est = true;
            }
            // Reihenfolge per Ziehen (DFS: Label entlang der Zeitleiter verschieben), fuer alle gleich
            if (!done && s.planUtc > 0 && !(!s.arrival && r.bay == BAY_RUNWAY))
            {
                it.t = std::max(now, s.planUtc);
                it.planned = true;
                it.overdue = false;
            }
            if (done && it.t < now - past) it.where = 2;              // laengst weg und nicht abgegeben
            else if (it.t <= 0 || it.t > now + fut) it.where = 1;     // spaeter oder ohne Zeit (Hand Over In)
            it.done = done;
            it.rwy = TlNormRwy(s.arrival ? s.arrRwy : s.rwy);
            m_tlItems.push_back(it);
        }
    }
    TlEstimate();
    for (TlItem& it : m_tlItems)
        if (it.est) it.where = it.t > now + fut ? 1 : 0;
}

// Abfluege ohne TTOT/Slot: geschaetzte Zeit - fruehestens am Rollhalt in 1 min, rollend in 2:30, noch bei Ground
// in 4 min; je Seite und Piste der Reihe nach (Rollhalt vor Rollen, dann die Reihenfolge der Bay) mindestens 1 min
// hinter der vorigen Schaetzung, damit sie nebeneinander lesbar bleiben. Nur Anzeige, keine Staffelung.
void StripWindow::TlEstimate()
{
    std::vector<TlItem*> est;
    for (TlItem& it : m_tlItems)
        if (it.est) est.push_back(&it);
    if (est.empty()) return;
    std::stable_sort(est.begin(), est.end(), [](const TlItem* a, const TlItem* b) {
        if (a->earliest != b->earliest) return a->earliest < b->earliest;
        return a->order < b->order;
    });
    std::map<std::string, long long> last;
    for (TlItem* e : est)
    {
        std::string k = std::to_string(e->side) + "|" + e->rwy;
        long long t = e->earliest;
        auto f = last.find(k);
        if (f != last.end()) t = std::max(t, f->second + 60);
        e->t = (t + 9) / 10 * 10;
        last[k] = e->t;
    }
}

void StripWindow::LayoutTimeline()
{
    m_tlLanes.clear();
    const RECT pr = m_tlPanel;
    int pw = pr.right - pr.left;
    int leftW = std::max(Sx(150), std::min(Sx(210), pw * 13 / 100));
    int rightW = std::max(Sx(176), std::min(Sx(250), pw * 17 / 100));
    if (pw - leftW - rightW < Sx(320)) { leftW = std::max(Sx(90), pw / 7); rightW = std::max(Sx(110), pw / 6); }
    int g = Sx(6);
    m_tlLeft = Rc(pr.left, pr.top, pr.left + leftW, pr.bottom);
    m_tlRight = Rc(pr.right - rightW, pr.top, pr.right, pr.bottom);
    int axisH = Sx(34);
    int cy = (pr.top + pr.bottom) / 2;
    m_tlAxis = Rc(m_tlLeft.right + g, cy - axisH / 2, m_tlRight.left - g, cy - axisH / 2 + axisH);
    m_tlAx0 = m_tlAxis.left + Sx(6);
    m_tlAx1 = m_tlAxis.right - Sx(6);
    int x0 = m_tlAxis.left, x1 = m_tlAxis.right;
    int top = pr.top + Sx(4), bot = pr.bottom - Sx(4);
    if (m_tlSideGroup.size() >= 2)
    {
        // erste Piste oben (Abfluege aussen, Ankuenfte an der Achse), zweite Piste unten gespiegelt
        int aMid = (top + m_tlAxis.top) / 2, bMid = (m_tlAxis.bottom + bot) / 2;
        m_tlLanes.push_back({ Rc(x0, top, x1, aMid), 0, 0, true });
        m_tlLanes.push_back({ Rc(x0, aMid, x1, m_tlAxis.top), 0, 1, true });
        m_tlLanes.push_back({ Rc(x0, m_tlAxis.bottom, x1, bMid), 1, 1, false });
        m_tlLanes.push_back({ Rc(x0, bMid, x1, bot), 1, 0, false });
    }
    else
    {
        // eine Piste: Abfluege oberhalb, Ankuenfte unterhalb der Achse
        m_tlLanes.push_back({ Rc(x0, top, x1, m_tlAxis.top), 0, 0, true });
        m_tlLanes.push_back({ Rc(x0, m_tlAxis.bottom, x1, bot), 0, 1, false });
    }
}

bool StripWindow::TlHit(POINT p, std::string& cs, RECT& rc) const
{
    for (auto it = m_tlHits.rbegin(); it != m_tlHits.rend(); ++it)
        if (PtInRect(&it->first, p)) { cs = it->second; rc = it->first; return true; }
    return false;
}

// Ziehen im Zeitstrahl: obere Haelfte = erste Piste, untere = zweite. Die Bay bleibt, nur die Piste wechselt.
int StripWindow::TlDropCol(POINT p) const
{
    if (m_tlSideGroup.empty() || m_cols.size() != m_tlAbs.size()) return -1;
    int side = 0;
    if (m_tlSideGroup.size() >= 2) side = p.y < (m_tlAxis.top + m_tlAxis.bottom) / 2 ? 0 : 1;
    int g = m_tlSideGroup[side];
    const ViewDef& v0 = ViewOf(m_view, m_board);
    int def = -1;
    if (m_dragFromCol >= 0 && m_dragFromCol < (int)m_tlAbs.size() && m_tlAbs[m_dragFromCol]) def = m_cols[m_dragFromCol].def;
    else
    {
        // von aussen (z. B. Startup, Taxi In): Abflug an den Rollhalt, Ankunft ins Final
        const StripData* s = FindStrip(m_dragCs);
        const ViewDef& v = ViewOf(m_view, m_board);
        int want = (s && s->arrival) ? BAY_FINAL : BAY_TAXI;
        for (int c = 0; c < (int)m_cols.size() && def < 0; ++c)
            if (m_tlAbs[c] && v.cols[m_cols[c].def].dropBay == want) def = m_cols[c].def;
        for (int c = 0; c < (int)m_cols.size() && def < 0; ++c)
            if (m_tlAbs[c]) def = m_cols[c].def;
    }
    for (int c = 0; c < (int)m_cols.size(); ++c)
        if (m_tlAbs[c] && m_cols[c].def == def && m_cols[c].group == g) return c;
    // keine gleiche Spalte auf der Seite (Hannover: jede Bahn hat ihre eigene Runway-Spalte): gleiche Bay
    if (def >= 0)
    {
        int want = v0.cols[def].dropBay;
        for (int c = 0; c < (int)m_cols.size(); ++c)
            if (m_tlAbs[c] && m_cols[c].group == g && v0.cols[m_cols[c].def].dropBay == want) return c;
    }
    return -1;
}

// Antippen: Statusfeld bzw. freie Flaeche = naechster logischer Schritt (DFS: Klick auf den Labelkopf),
// Rufzeichen = Menue am Label, die anderen Felder oeffnen ihre Eingabe (Tastenfeld oder Liste).
void StripWindow::TlTap(const std::string& cs, bool secondary, const RECT& anchor, int zone)
{
    int bay = -1;
    const StripData* sp = FindStrip(cs, &bay);
    if (!sp) return;
    const StripData s = *sp;   // Kopie: der Host ersetzt das Board sofort
    int cc = ColumnOf(cs), side = 0;
    if (cc >= 0)
        for (int si = 0; si < (int)m_tlSideGroup.size(); ++si)
            if (m_tlSideGroup[si] == m_cols[cc].group) side = si;
    int rc0 = TlRwyColFor(s, side);
    bool closed = rc0 >= 0 && RwyStateOf(rc0) == 2;
    if (m_host) m_host->StripTapped(cs);   // in EuroScope anwaehlen (und bestaetigen)
    auto menu = [&]() {
        m_detailCs = cs;
        m_tlMenu = true;
        m_tlMenuAnchor = anchor;
        Invalidate();
    };
    if (secondary || !m_host) { menu(); return; }
    (void)zone;
    if (s.freeStrip) { menu(); return; }
    // das ganze Label ist eine Taste (DFS): naechster logischer Schritt; gibt es keinen, das Menue
    if (Outgoing(s)) { menu(); return; }   // angeboten: wartet auf die naechste Stelle
    if (s.offerForMe) { m_host->Command(".strips accept " + cs); Invalidate(); return; }
    if (Preview(s, bay) && !s.respMine) { menu(); return; }   // noch bei Ground / beim Anflug
    if (s.arrival)
    {
        if (s.onGround)
        {
            if (bay == BAY_RUNWAY) { m_host->StripMoved(cs, BAY_GNDIN, -1); Invalidate(); return; }   // Piste frei: an Ground
            menu();
            return;
        }
        if (!s.landingCleared && (bay == BAY_FINAL || bay == BAY_RUNWAY)) { m_host->ToggleRunwayClearance(cs); Invalidate(); return; }
        menu();
        return;
    }
    if (s.airborne)
    {
        if (!s.handedOver) { m_host->ToggleHandover(cs); Invalidate(); return; }   // an Departure
        menu();
        return;
    }
    if (bay == BAY_RUNWAY)
    {
        if (s.state != GC_DEPA) { m_host->ToggleRunwayClearance(cs); Invalidate(); return; }   // Startfreigabe
        menu();
        return;
    }
    if ((bay == BAY_TAXI || bay == BAY_TOGND || bay == BAY_HANDOFF) && !closed)
    {
        m_host->StripMoved(cs, BAY_RUNWAY, -1);   // Line-up
        Invalidate();
        return;
    }
    menu();
}

// Label im Zeitstrahl, aufgebaut wie in der DFS-Planungsanzeige: rechteckiger Koerper, Spitze zur Achse,
// Statusfarbe am linken Rand. Zeile 1 Rufzeichen, Zeile 2 Typ und klein Zeit/Slot und Bemerkung, die
// letzte Zeile (gegenueber der Spitze) SID und Ziel bzw. Stand und Herkunft.
// Das ganze Label ist eine Taste: Tippen = naechster Schritt; Rechtsklick, Radierer oder langes Druecken
// oeffnet das Menue am Label (dort alles Weitere: Reihenfolge, Hold Short, Intersection, SID, Stand ...).
void StripWindow::DrawTlLabel(HDC hdc, const RECT& body, int tipX, int apexY, int tipDir, const StripData& s, int bay,
                              const TlItem* it, bool ghost)
{
    const TlPal P = TlPalette();
    bool pv = !s.freeStrip && Preview(s, bay) && !s.respMine;
    COLORREF paper, ink, dim, edge, fld, fldEdge;
    TlColors(s, pv, P, paper, ink, dim, edge, fld, fldEdge);
    int tipHalf = Sx(8);
    if (ghost)
    {
        RECT sh = body;
        OffsetRect(&sh, Sx(5), Sx(6));
        Fill(hdc, sh, RGB(0, 0, 0));
    }
    std::wstring stTxt;
    COLORREF stCol = TlMix(TlStatus(s, bay, stTxt), RGB(0, 0, 0), 100 - P.statusPct);
    // Kopf: Spitze zur Achse in der Statusfarbe (gehoert zum Statusfeld)
    if (tipDir != 0)
    {
        int baseY = tipDir > 0 ? body.bottom - 1 : body.top;
        COLORREF tf = s.designator ? paper : stCol;
        TlTri(hdc, POINT{ tipX - tipHalf, baseY }, POINT{ tipX + tipHalf, baseY }, POINT{ tipX, apexY }, tf, tf);
    }
    RoundBox(hdc, body, Sx(3), paper, edge);
    if (s.designator)
    {
        COLORREF band = (s.hl > 0 && s.hl < 6) ? kHlStrong[s.hl] : RGB(60, 70, 95);
        band = TlMix(band, RGB(0, 0, 0), 100 - P.statusPct);
        Fill(hdc, Inset(body, 1, 1), band);
        TlHatch(hdc, Inset(body, 1, 1), TlMix(band, RGB(255, 255, 255), 14), Sx(14), std::max(1, Sx(3)));
        std::wstring t = W(s.callsign);
        while (!t.empty() && t.back() == L'*') t.pop_back();
        Text(hdc, Inset(body, Sx(6), 0), t, m_fCallsignSmall, RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (!ghost) m_tlHits.push_back({ body, s.callsign });
        return;
    }
    // Statusbalken am linken Rand (Farbe der Freigabe)
    int barW = Sx(11);
    RECT bar = Rc(body.left + 1, body.top + 1, body.left + 1 + barW, body.bottom - 1);
    Fill(hdc, bar, stCol);
    if (it && it->planned)   // Reihenfolge gesetzt: Kerbe im Balken
        Fill(hdc, Rc(bar.left + Sx(3), bar.top + Sx(4), bar.right - Sx(3), bar.top + Sx(7)), RGB(255, 255, 255));

    int x0 = bar.right + Sx(5), x1 = body.right - Sx(4);
    int h = body.bottom - body.top;
    int hCs = Sx(22), hT = Sx(17), hI = Sx(17), gap = Sx(1);
    int y = body.top + std::max(Sx(1), (h - hCs - hT - hI - 2 * gap) / 2);
    bool infoFirst = tipDir > 0;   // die Zeile SID/Ziel bzw. Stand liegt gegenueber der Spitze
    RECT rI, rC, rT;
    if (infoFirst)
    {
        rI = Rc(x0, y, x1, y + hI);
        rC = Rc(x0, rI.bottom + gap, x1, rI.bottom + gap + hCs);
        rT = Rc(x0, rC.bottom + gap, x1, rC.bottom + gap + hT);
    }
    else
    {
        rC = Rc(x0, y, x1, y + hCs);
        rT = Rc(x0, rC.bottom + gap, x1, rC.bottom + gap + hT);
        rI = Rc(x0, rT.bottom + gap, x1, rT.bottom + gap + hI);
    }
    // kleine farbige Marke (kein eigenes Klickfeld: das ganze Label ist eine Taste)
    auto tag = [&](int& x, int right, const RECT& row, const std::wstring& t, HFONT f, COLORREF bgc, COLORREF fg) {
        int w0 = std::min(right - x, TextWidth(hdc, t, f) + Sx(8));
        if (w0 < Sx(12)) return;
        RECT r = Rc(x, row.top + Sx(1), x + w0, row.bottom - Sx(1));
        RoundBox(hdc, r, Sx(2), bgc, bgc);
        Text(hdc, r, t, f, fg, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        x += w0 + Sx(4);
    };
    auto plain = [&](int& x, int right, const RECT& row, const std::wstring& t, HFONT f, COLORREF fg) {
        if (t.empty() || right - x < Sx(10)) return;
        int need = TextWidth(hdc, t, f) + Sx(2), w0 = std::min(right - x, need);
        Text(hdc, Rc(x, row.top, x + w0, row.bottom), t, f, fg, DT_LEFT | DT_VCENTER | DT_SINGLELINE | (w0 < need ? DT_END_ELLIPSIS : 0));
        x += w0 + Sx(5);
    };

    // Zeile 1: Rufzeichen (das Wichtigste, gross), rechts der Status als Kuerzel
    {
        int right = rC.right;
        if (!s.freeStrip)
        {
            int stW = TextWidth(hdc, stTxt, m_fLabel) + Sx(10);
            RECT stR = Rc(rC.right - stW, rC.top + Sx(3), rC.right, rC.bottom - Sx(3));
            RoundBox(hdc, stR, Sx(2), stCol, stCol);
            Text(hdc, stR, stTxt, m_fLabel, RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            right = stR.left - Sx(4);
        }
        FitText(hdc, Rc(rC.left, rC.top, right, rC.bottom), W(s.callsign), m_fCallsignSmall, m_fStrip, ink);
    }

    // Zeile 2: Typ und Wirbelschleppe, dann klein Zeit bzw. Slot, Hold Short und Bemerkung
    if (!s.freeStrip)
    {
        int x = rT.left, right = rT.right;
        bool heavy = s.wtc == "H" || s.wtc == "J";
        std::wstring type = W(s.acType);
        if (!s.wtc.empty() && (heavy || rT.right - rT.left > Sx(150))) type += L"/" + W(s.wtc);
        plain(x, right, rT, type, m_fStripTiny, heavy ? (s.arrival ? RGB(255, 206, 110) : RGB(150, 64, 0)) : ink);
        long long t = it ? it->t : 0;
        std::wstring tt;
        COLORREF tf = dim;
        if (it && it->planned) tag(x, right, rT, L"PLN " + Hhmm(t), m_fStripTiny, P.plan, TlOn(P.plan));
        else
        {
            if (it && it->est) tt = L"EST " + Hhmm(t);
            else if (s.arrival) tt = (s.onGround ? L"ALDT " : L"ETA ") + Hhmm(t);
            else if (s.airborne) tt = L"ATOT " + Hhmm(t);
            else if (s.ctotUtc > 0) tt = L"CTOT " + Hhmm(s.ctotUtc);   // Slot geht vor
            else if (!s.ttot.empty()) tt = L"TTOT " + W(s.ttot);
            if (it && it->overdue) tf = s.arrival ? RGB(255, 140, 120) : RGB(200, 30, 24);
            if (!s.arrival && !s.airborne && s.ctotUtc > 0 && !(it && it->est))
            {
                int cst = CtotState(s);
                COLORREF cb = TlMix((cst > 0 && cst < 5) ? kCtotFill[cst] : RGB(255, 214, 153), RGB(0, 0, 0), 100 - P.statusPct);
                tag(x, right, rT, tt, m_fStripTiny, cb, RGB(20, 20, 20));
            }
            else plain(x, right, rT, tt, m_fStripTiny, tf);
        }
        if (!s.holdShort.empty()) tag(x, right, rT, L"HS " + W(s.holdShort), m_fLabel, col::warn, RGB(255, 255, 255));
        if (!IsTransientScratchW(s.scratch)) plain(x, right, rT, W(s.scratch), m_fStripTiny, ink);
    }
    else
    {
        // Notiz: Hold Short und Text
        int x = rT.left;
        if (!s.holdShort.empty()) tag(x, rT.right, rT, L"HS " + W(s.holdShort), m_fLabel, col::warn, RGB(255, 255, 255));
        plain(x, rT.right, rT, W(s.scratch), m_fStripTiny, ink);
    }

    // Zeile gegenueber der Spitze: Abflug SID (Pastell nach SID-Gruppe), Intersection, Ziel;
    // Ankunft Stand, Rollweg, Herkunft
    if (!s.freeStrip)
    {
        int x = rI.left, right = rI.right;
        // Piste, wenn sie nicht die Hauptpiste der Seite ist (Hannover 27C, Pistenwechsel) oder fehlt
        {
            std::wstring rt;
            bool unk = false;
            std::string r = TlNormRwy(s.arrival ? s.arrRwy : s.rwy);
            if (!s.heli)
            {
                if (r.empty()) { rt = L"RWY?"; unk = true; }
                else if (it && it->side < (int)m_tlSideGroup.size())
                {
                    int g = m_tlSideGroup[it->side];
                    std::string main = (g == RWY_A || g == RWY_B) ? m_board.rwyActive[g] : std::string();
                    if (main.empty() && it->side < (int)m_tlRwyCol.size() && m_tlRwyCol[it->side] >= 0)
                    {
                        const ColumnDef& d = ViewOf(m_view, m_board).cols[m_cols[m_tlRwyCol[it->side]].def];
                        if (d.rwyMatch) main = d.rwyMatch;
                    }
                    if (!main.empty() && (" " + main + " ").find(" " + r + " ") == std::string::npos) rt = W(r);
                }
            }
            if (!rt.empty())
                tag(x, right, rI, rt, m_fStripTiny, unk ? RGB(250, 170, 60) : TlMix(RGB(255, 255, 255), RGB(0, 0, 0), 100 - P.statusPct), RGB(20, 20, 20));
        }
        if (s.arrival)
        {
            // Stand: fehlt er oder ist er belegt, faellt er auf
            if (s.standConflict || s.standBusy) tag(x, right, rI, L"STD " + W(s.stand), m_fStripTiny, col::warn, RGB(255, 255, 255));
            else if (s.stand.empty()) tag(x, right, rI, L"STD ?", m_fStripTiny, RGB(250, 170, 60), RGB(20, 20, 20));
            else plain(x, right, rI, L"STD " + W(s.stand), m_fStripTiny, ink);
            if (!s.intersection.empty()) plain(x, right, rI, W(s.intersection), m_fStripTiny, ink);
        }
        else
        {
            bool vfr = s.rules == "V";
            std::wstring sid = s.sid.empty() ? std::wstring(vfr ? L"VFR" : L"NO SID") : W(s.sid);
            COLORREF sb = s.sid.empty() ? (vfr ? RGB(226, 230, 234) : RGB(250, 170, 60)) : SidPastel(s.sid);
            if (!s.vsidErr.empty()) sb = RGB(250, 150, 140);
            tag(x, right, rI, sid, m_fStripTiny, TlMix(sb, RGB(0, 0, 0), 100 - P.statusPct), RGB(20, 20, 20));
            if (!s.intersection.empty()) tag(x, right, rI, W(s.intersection), m_fStripTiny, RGB(255, 255, 255), RGB(20, 20, 20));
        }
        std::wstring ap = s.arrival ? W(s.divertFrom.empty() ? s.adep : s.adep + " DIV") : W(s.ades);
        plain(x, right, rI, ap, m_fStripTiny, s.arrival ? dim : ink);
    }

    // Rahmen: angewaehlt / Menue offen, Angebot laeuft, angefordert, Vorankuendigung
    bool sel = s.selected || (!m_detailCs.empty() && m_detailCs == s.callsign);
    if (sel) Frame(hdc, body, P.frame, Sx(2));
    else if (!s.offerTo.empty()) Frame(hdc, body, RGB(236, 140, 30), Sx(2));
    if (pv)
    {
        HPEN pen = CreatePen(PS_DOT, 1, P.textDim);
        HGDIOBJ op = SelectObject(hdc, pen);
        HGDIOBJ ob = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, body.left, body.top, body.right, body.bottom);
        SelectObject(hdc, ob);
        SelectObject(hdc, op);
        DeleteObject(pen);
    }
    if (it && it->overdue && !it->planned)   // TTOT vorbei: rote Ecke
        TlTri(hdc, POINT{ body.right - Sx(12), body.top + 1 }, POINT{ body.right - 1, body.top + 1 }, POINT{ body.right - 1, body.top + Sx(12) },
              col::warn, col::warn);
    if (!ghost)
    {
        RECT hit = body;
        if (tipDir > 0) hit.bottom = std::max<LONG>(hit.bottom, apexY);
        else if (tipDir < 0) hit.top = std::min<LONG>(hit.top, apexY);
        m_tlHits.push_back({ hit, s.callsign });
    }
}

// Einzeilige Zeile in den Listen (Hand Over In / Out): Statusbalken links, Kopf rechts
void StripWindow::DrawTlRow(HDC hdc, const RECT& rc, const StripData& s, int bay, const TlItem& it)
{
    const TlPal P = TlPalette();
    bool pv = !s.freeStrip && Preview(s, bay) && !s.respMine;
    COLORREF paper, ink, dim, edge, fld, fldEdge;
    TlColors(s, pv, P, paper, ink, dim, edge, fld, fldEdge);
    std::wstring stTxt;
    COLORREF stCol = TlMix(TlStatus(s, bay, stTxt), RGB(0, 0, 0), 100 - P.statusPct);
    RoundBox(hdc, rc, Sx(3), paper, edge);
    if (s.designator)
    {
        COLORREF band = TlMix((s.hl > 0 && s.hl < 6) ? kHlStrong[s.hl] : RGB(60, 70, 95), RGB(0, 0, 0), 100 - P.statusPct);
        Fill(hdc, Inset(rc, 1, 1), band);
        TlHatch(hdc, Inset(rc, 1, 1), TlMix(band, RGB(255, 255, 255), 14), Sx(12), std::max(1, Sx(2)));
        std::wstring t = W(s.callsign);
        while (!t.empty() && t.back() == L'*') t.pop_back();
        Text(hdc, Inset(rc, Sx(4), 0), t, m_fHeader, RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        m_tlHits.push_back({ rc, s.callsign });
        return;
    }
    int headW = Sx(14);
    RECT bar = Rc(rc.left + 1, rc.top + 1, rc.left + 1 + Sx(8), rc.bottom - 1);
    RECT head = Rc(rc.right - headW, rc.top + 1, rc.right - 1, rc.bottom - 1);
    Fill(hdc, bar, stCol);
    Fill(hdc, head, stCol);
    int x0 = bar.right + Sx(5), x1 = head.left - Sx(4);
    RECT row = Rc(x0, rc.top + Sx(2), x1, rc.bottom - Sx(2));
    if (s.freeStrip)
        Text(hdc, row, W(s.callsign), m_fStripSmall, ink, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    else
    {
        // feste Spalten: Rufzeichen | Typ | Zeit - gleiche Angaben stehen in der Liste untereinander
        std::wstring tt = it.planned ? L"PLN " + Hhmm(it.t) : it.est ? L"~" + Hhmm(it.t) : Hhmm(it.t);
        int tw = TextWidth(hdc, L"PLN 0000", m_fStripTiny) + Sx(4);
        int yw = std::min((int)(x1 - x0) / 4, TextWidth(hdc, L"A388", m_fLabel) + Sx(6));
        RECT rTm = Rc(row.right - tw, row.top, row.right, row.bottom);
        RECT rTy = Rc(rTm.left - Sx(4) - yw, row.top, rTm.left - Sx(4), row.bottom);
        RECT rCs = Rc(row.left, row.top, rTy.left - Sx(4), row.bottom);
        FitText(hdc, rCs, W(s.callsign), m_fHeader, m_fStripTiny, ink);
        Text(hdc, rTy, W(s.acType), m_fLabel, dim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        COLORREF tf = it.planned ? P.plan : it.overdue ? col::warn : ink;
        Text(hdc, rTm, tt, m_fStripTiny, tf, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
    bool sel = s.selected || (!m_detailCs.empty() && m_detailCs == s.callsign);
    if (sel) Frame(hdc, rc, P.frame, Sx(2));
    else if (!s.offerTo.empty()) Frame(hdc, rc, RGB(236, 140, 30), Sx(2));
    m_tlHits.push_back({ rc, s.callsign });
}

void StripWindow::DrawTimeline(HDC hdc)
{
    const RECT pr = m_tlPanel;
    if (pr.right - pr.left < Sx(240) || pr.bottom - pr.top < Sx(160)) return;
    const long long now = m_tlNow;
    const TlPal P = TlPalette();
    const int past = std::max(1, m_board.opt.tlPast), fut = std::max(3, m_board.opt.tlFuture);

    auto sideName = [&](int si) -> std::wstring {
        int g = si < (int)m_tlSideGroup.size() ? m_tlSideGroup[si] : RWY_NONE;
        if (g == RWY_A || g == RWY_B) return m_board.rwyActive[g].empty() ? GroupEn(g) : W(m_board.rwyActive[g]);
        return std::wstring();
    };
    auto rwyState = [&](int si) { return si < (int)m_tlRwyCol.size() && m_tlRwyCol[si] >= 0 ? RwyStateOf(m_tlRwyCol[si]) : 0; };

    RoundBox(hdc, pr, Sx(6), P.bg, P.sep);
    Fill(hdc, Rc(m_tlLeft.left + 1, m_tlLeft.top + 1, m_tlLeft.right, m_tlLeft.bottom - 1), P.side);
    Fill(hdc, Rc(m_tlRight.left, m_tlRight.top + 1, m_tlRight.right - 1, m_tlRight.bottom - 1), P.side);
    Fill(hdc, Rc(m_tlLeft.right, pr.top + 1, m_tlLeft.right + 1, pr.bottom - 1), P.sep);
    Fill(hdc, Rc(m_tlRight.left - 1, pr.top + 1, m_tlRight.left, pr.bottom - 1), P.sep);
    int xNow = TlX(now);
    Fill(hdc, Rc(m_tlAxis.left, pr.top + 1, std::max<int>(m_tlAxis.left, xNow), pr.bottom - 1), P.past);

    // Pistenstatus je Seite: gesperrt = rot schraffiert, Caution = gelber Rand an der Achse
    for (int si = 0; si < (int)m_tlSideGroup.size(); ++si)
    {
        int st = rwyState(si);
        if (st == 0) continue;
        RECT area = { 0, 0, 0, 0 };
        for (const TlLane& L : m_tlLanes)
            if (L.side == si) TlUnion(area, L.rc);
        if (st == 2)
        {
            WashRect(hdc, area, TlMix(RGB(220, 60, 50), RGB(0, 0, 0), 100 - P.statusPct), 46);
            TlHatch(hdc, area, TlMix(RGB(200, 70, 60), P.bg, 30), Sx(22), std::max(1, Sx(2)));
        }
        else
        {
            bool above = area.bottom <= m_tlAxis.top + 1;
            RECT band = above ? Rc(area.left, area.bottom - Sx(6), area.right, area.bottom) : Rc(area.left, area.top, area.right, area.top + Sx(6));
            Fill(hdc, band, TlMix(RGB(250, 210, 60), RGB(0, 0, 0), 100 - P.statusPct));
        }
    }

    // Minutenraster (Beschriftung je nach Platz jede, jede zweite oder jede fuenfte Minute)
    long long t0 = TlX0(), t1 = now + fut * 60LL;
    int pxMin = (int)((long long)(m_tlAx1 - m_tlAx0) * 60 / ((past + fut) * 60LL));
    int step = pxMin >= Sx(46) ? 1 : pxMin >= Sx(24) ? 2 : 5;
    for (long long m = (t0 + 59) / 60; m * 60 <= t1; ++m)
    {
        int x = TlX(m * 60);
        if (x < m_tlAxis.left || x >= m_tlAxis.right) continue;
        Fill(hdc, Rc(x, pr.top + 1, x + 1, m_tlAxis.top), P.grid);
        Fill(hdc, Rc(x, m_tlAxis.bottom, x + 1, pr.bottom - 1), P.grid);
    }
    // Reihen: Trennlinien und Beschriftung (Piste, Abflug / Ankunft)
    for (const TlLane& L : m_tlLanes)
    {
        if (L.above && L.lane == 0 && m_tlSideGroup.size() >= 2) Fill(hdc, Rc(L.rc.left, L.rc.bottom, L.rc.right, L.rc.bottom + 1), P.sep);
        if (!L.above && L.lane == 0 && m_tlSideGroup.size() >= 2) Fill(hdc, Rc(L.rc.left, L.rc.top, L.rc.right, L.rc.top + 1), P.sep);
        std::wstring cap = (L.lane ? L"ARR" : L"DEP");
        std::wstring sn = sideName(L.side);
        if (!sn.empty()) cap = sn + L"  " + cap;
        RECT cr = L.above ? Rc(L.rc.left + Sx(8), L.rc.top + Sx(2), L.rc.left + Sx(200), L.rc.top + Sx(18))
                          : Rc(L.rc.left + Sx(8), L.rc.bottom - Sx(18), L.rc.left + Sx(200), L.rc.bottom - Sx(2));
        Text(hdc, cr, cap, m_fLabel, P.textDim);
    }

    // Achse mit Minuten
    Fill(hdc, m_tlAxis, P.axisBg);
    Fill(hdc, Rc(m_tlAxis.left, m_tlAxis.top, m_tlAxis.right, m_tlAxis.top + 1), P.sep);
    Fill(hdc, Rc(m_tlAxis.left, m_tlAxis.bottom - 1, m_tlAxis.right, m_tlAxis.bottom), P.sep);
    for (long long m = (t0 + 59) / 60; m * 60 <= t1; ++m)
    {
        int x = TlX(m * 60);
        if (x < m_tlAxis.left || x >= m_tlAxis.right) continue;
        bool major = m % step == 0;
        int tick = major ? Sx(7) : Sx(4);
        Fill(hdc, Rc(x, m_tlAxis.top, x + 1, m_tlAxis.top + tick), P.textDim);
        Fill(hdc, Rc(x, m_tlAxis.bottom - tick, x + 1, m_tlAxis.bottom), P.textDim);
        if (!major) continue;
        wchar_t lab[8];
        swprintf(lab, 8, L"%02d:%02d", (int)(m % 1440 / 60), (int)(m % 60));
        if (std::abs(x - xNow) < Sx(66)) continue;   // nicht unter der Jetzt-Marke
        Text(hdc, Rc(x - Sx(30), m_tlAxis.top, x + Sx(30), m_tlAxis.bottom), lab, m_fStripTiny, P.text,
             DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    // Labels je Reihe: Abstand = Zeit. Ueberlappende weiter nach aussen; Linie zum Zeitpunkt auf der Achse.
    int lw = std::max(Sx(152), std::min(Sx(188), (int)(m_tlAxis.right - m_tlAxis.left) / 6));
    int lh = Sx(60), tipH = Sx(9), slotH = lh + tipH + Sx(5);
    struct Pl { const TlItem* it; RECT body; int tipX, apexY, x, k; };
    std::vector<std::vector<Pl>> placed(m_tlLanes.size());
    for (size_t li = 0; li < m_tlLanes.size(); ++li)
    {
        const TlLane& L = m_tlLanes[li];
        std::vector<const TlItem*> its;
        for (const TlItem& it : m_tlItems)
            if (it.where == 0 && it.side == L.side && it.lane == L.lane && !(m_mode == M_DRAG_STRIP && it.cs == m_dragCs))
                its.push_back(&it);
        std::stable_sort(its.begin(), its.end(), [](const TlItem* a, const TlItem* b) { return a->t < b->t; });
        int K = std::max(1, (int)(L.rc.bottom - L.rc.top - Sx(4)) / slotH);
        std::vector<std::vector<std::pair<int, int>>> occ(K);
        const int axL = (int)m_tlAxis.left + Sx(2), axR = (int)m_tlAxis.right - Sx(2) - lw, gapX = Sx(4);
        for (const TlItem* it : its)
        {
            int x = TlX(it->t);
            int l0 = std::max(axL, std::min(x - lw / 2, axR));
            // freie Stelle: zuerst ueber dem Zeitpunkt, sonst seitlich versetzt (die Linie zeigt weiter genau auf die
            // Zeit) oder eine Reihe weiter aussen - was am wenigsten verschiebt
            int best = -1, bestL = l0;
            long long bestCost = -1;
            for (int k = 0; k < K; ++k)
            {
                std::vector<int> cands = { l0 };
                for (const auto& o : occ[k]) { cands.push_back(o.second + gapX); cands.push_back(o.first - gapX - lw); }
                for (int c : cands)
                {
                    if (c < axL || c > axR) continue;
                    bool free = true;
                    for (const auto& o : occ[k])
                        if (c < o.second + gapX && c + lw + gapX > o.first) { free = false; break; }
                    if (!free) continue;
                    long long cost = std::abs(c - l0) + (long long)k * lw * 6 / 10;
                    if (bestCost < 0 || cost < bestCost) { bestCost = cost; best = k; bestL = c; }
                }
            }
            if (best < 0)
            {
                // kein Platz: die Reihe mit der kleinsten Ueberdeckung
                int bestOv = -1;
                best = 0;
                for (int k = 0; k < K; ++k)
                {
                    int ov = 0;
                    for (const auto& o : occ[k]) ov += std::max(0, std::min(l0 + lw, o.second) - std::max(l0, o.first) + gapX);
                    if (bestOv < 0 || ov < bestOv) { bestOv = ov; best = k; }
                }
                bestL = l0;
            }
            int l = bestL, r = l + lw;
            occ[best].push_back({ l, r });
            int apexY = L.above ? (int)L.rc.bottom - Sx(2) - best * slotH : (int)L.rc.top + Sx(2) + best * slotH;
            RECT body = L.above ? Rc(l, apexY - tipH - lh, r, apexY - tipH) : Rc(l, apexY + tipH, r, apexY + tipH + lh);
            int tipX = std::max(l + Sx(14), std::min(r - Sx(12), x));
            placed[li].push_back({ it, body, tipX, apexY, x, best });
        }
    }
    // erst alle Linien zur Achse (liegen unter den Labels), mit Punkt am Zeitpunkt
    for (size_t li = 0; li < m_tlLanes.size(); ++li)
        for (const Pl& p : placed[li])
        {
            int ay = m_tlLanes[li].above ? (int)m_tlAxis.top : (int)m_tlAxis.bottom;
            COLORREF lc = p.it->planned ? P.plan : P.conn;
            TlLine(hdc, p.tipX, p.apexY, p.x, ay, lc, 1, p.it->est ? PS_DOT : PS_SOLID);
            Fill(hdc, Rc(p.x - Sx(2), m_tlLanes[li].above ? ay - Sx(3) : ay, p.x + Sx(2) + 1, m_tlLanes[li].above ? ay : ay + Sx(3)), lc);
        }

    // Jetzt-Linie
    Fill(hdc, Rc(xNow - Sx(1), pr.top + 1, xNow + Sx(1) + 1, pr.bottom - 1), P.nowC);
    {
        wchar_t lab[16];
        swprintf(lab, 16, L"%02d:%02d:%02d", (int)(now % 86400 / 3600), (int)(now % 3600 / 60), (int)(now % 60));
        int w2 = TextWidth(hdc, lab, m_fStripTiny) + Sx(12);
        RECT nr = Rc(xNow - w2 / 2, m_tlAxis.top + Sx(5), xNow - w2 / 2 + w2, m_tlAxis.bottom - Sx(5));
        RoundBox(hdc, nr, Sx(3), P.nowC, P.nowC);
        Text(hdc, nr, lab, m_fStripTiny, RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    // Labels (aussen zuerst, die an der Achse liegen oben)
    for (size_t li = 0; li < m_tlLanes.size(); ++li)
    {
        std::vector<Pl> ps = placed[li];
        std::stable_sort(ps.begin(), ps.end(), [](const Pl& a, const Pl& b) { return a.k > b.k; });
        for (const Pl& p : ps)
        {
            int bay = -1;
            const StripData* s = FindStrip(p.it->cs, &bay);
            if (s) DrawTlLabel(hdc, p.body, p.tipX, p.apexY, m_tlLanes[li].above ? 1 : -1, *s, bay, p.it, false);
        }
    }

    // Listen: rechts Hand Over In (spaeter / ohne Zeit, je Reihe), links Hand Over Out und der Kopf je Piste
    int rowH = Sx(28), pitch = Sx(31);
    auto drawList = [&](RECT cell, const std::wstring& title, std::vector<const TlItem*> its) {
        Fill(hdc, Rc(cell.left, cell.top + Sx(17), cell.right, cell.top + Sx(18)), P.sep);
        Text(hdc, Rc(cell.left + Sx(2), cell.top, cell.right, cell.top + Sx(17)), title, m_fLabel, P.textDim);
        int y = cell.top + Sx(21);
        int maxRows = std::max(0, (int)(cell.bottom - y + Sx(3)) / pitch);
        int n = (int)its.size();
        int show = n > maxRows ? std::max(0, maxRows - 1) : n;
        for (int i = 0; i < show; ++i)
        {
            if (m_mode == M_DRAG_STRIP && its[i]->cs == m_dragCs) continue;
            int bay = -1;
            const StripData* s = FindStrip(its[i]->cs, &bay);
            if (s) DrawTlRow(hdc, Rc(cell.left, y, cell.right, y + rowH), *s, bay, *its[i]);
            y += pitch;
        }
        if (n > show)
        {
            wchar_t more[32];
            swprintf(more, 32, L"+%d more", n - show);
            Text(hdc, Rc(cell.left, y, cell.right, y + rowH), more, m_fHeader, P.textDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    };
    for (size_t li = 0; li < m_tlLanes.size(); ++li)
    {
        const TlLane& L = m_tlLanes[li];
        std::wstring sn = sideName(L.side);
        // rechts: Hand Over In - kommt spaeter oder hat keine Zeit (Notizen der Piste oben in der Abflugreihe)
        {
            std::vector<const TlItem*> its;
            for (const TlItem& it : m_tlItems)
                if (it.side == L.side && ((it.where == 1 && it.lane == L.lane) || (it.where == 3 && L.lane == 0))) its.push_back(&it);
            std::stable_sort(its.begin(), its.end(), [](const TlItem* a, const TlItem* b) {
                if ((a->where == 3) != (b->where == 3)) return a->where == 3;
                if ((a->t > 0) != (b->t > 0)) return a->t > 0;
                if (a->t != b->t) return a->t < b->t;
                return a->order < b->order;
            });
            RECT cell = Rc(m_tlRight.left + Sx(6), L.rc.top + Sx(4), m_tlRight.right - Sx(6), L.rc.bottom - Sx(4));
            std::wstring title = std::wstring(L"HAND OVER IN ") + (sn.empty() ? L"" : sn + L" ") + (L.lane ? L"ARR" : L"DEP");
            drawList(cell, title, its);
        }
        // links: Hand Over Out, in der Ankunftsreihe an der Achse der Kopf der Piste (antippen = Status)
        {
            RECT cell = Rc(m_tlLeft.left + Sx(6), L.rc.top + Sx(4), m_tlLeft.right - Sx(6), L.rc.bottom - Sx(4));
            if (L.lane == 1)
            {
                int hh = Sx(54);
                RECT hr = L.above ? Rc(cell.left, cell.bottom - hh, cell.right, cell.bottom) : Rc(cell.left, cell.top, cell.right, cell.top + hh);
                int g = L.side < (int)m_tlSideGroup.size() ? m_tlSideGroup[L.side] : RWY_NONE;
                int st = rwyState(L.side);
                COLORREF acc = TlMix(kRwyAccent[g >= 0 && g < 3 ? g : 0], RGB(0, 0, 0), 100 - P.statusPct);
                COLORREF hb = st == 2 ? TlMix(RGB(220, 60, 50), RGB(0, 0, 0), 100 - P.statusPct)
                            : st == 1 ? TlMix(RGB(250, 210, 60), RGB(0, 0, 0), 100 - P.statusPct) : P.axisBg;
                COLORREF hf = st == 2 ? RGB(255, 255, 255) : st == 1 ? RGB(20, 20, 20) : P.text;
                RoundBox(hdc, hr, Sx(3), hb, acc);
                Fill(hdc, Rc(hr.left + 1, hr.top + 1, hr.left + Sx(8), hr.bottom - 1), acc);
                std::wstring big = sn.empty() ? (L.side < (int)m_tlRwyCol.size() && m_tlRwyCol[L.side] >= 0 ? ColTitle(m_tlRwyCol[L.side]) : L"RUNWAY") : sn;
                // keine aktive Piste bekannt (Hannover): Name der Bahn statt zweimal NORTH
                if ((g == RWY_A || g == RWY_B) && m_board.rwyActive[g].empty() && L.side < (int)m_tlRwyCol.size() && m_tlRwyCol[L.side] >= 0)
                {
                    std::wstring t = ColTitle(m_tlRwyCol[L.side]);
                    if (t.compare(0, 4, L"RWY ") == 0) big = t.substr(4);
                }
                Text(hdc, Rc(hr.left + Sx(14), hr.top + Sx(2), hr.right - Sx(4), hr.top + Sx(32)), big, m_fCallsign, hf);
                std::wstring sub = (g == RWY_A || g == RWY_B) ? GroupEn(g) : std::wstring();
                if (st == 1) sub += sub.empty() ? L"CAUTION" : L"  \x00B7  CAUTION";
                else if (st == 2) sub += sub.empty() ? L"CLOSED" : L"  \x00B7  CLOSED";
                Text(hdc, Rc(hr.left + Sx(14), hr.top + Sx(32), hr.right - Sx(4), hr.bottom - Sx(2)), sub, m_fLabel, hf);
                if (L.side < (int)m_tlRwyCol.size() && m_tlRwyCol[L.side] >= 0 && m_tlRwyCol[L.side] < 32)
                {
                    Button b;
                    b.rc = hr;
                    b.action = A_RWYST_BASE + m_tlRwyCol[L.side];
                    m_buttons.push_back(b);
                }
                if (L.above) cell.bottom = hr.top - Sx(6);
                else cell.top = hr.bottom + Sx(6);
                // weitere Bahnen dieser Seite (Hannover 09C/27C): eigener kleiner Kopf mit Status
                if (L.side < (int)m_tlRwyCols.size())
                    for (int rcx : m_tlRwyCols[L.side])
                    {
                        if (rcx == m_tlRwyCol[L.side] || rcx >= 32) continue;
                        int ch = Sx(26);
                        if (cell.bottom - cell.top < ch + Sx(10)) break;
                        RECT xr = L.above ? Rc(cell.left, cell.bottom - ch, cell.right, cell.bottom) : Rc(cell.left, cell.top, cell.right, cell.top + ch);
                        int xs = RwyStateOf(rcx);
                        COLORREF xb = xs == 2 ? TlMix(RGB(220, 60, 50), RGB(0, 0, 0), 100 - P.statusPct)
                                    : xs == 1 ? TlMix(RGB(250, 210, 60), RGB(0, 0, 0), 100 - P.statusPct) : P.axisBg;
                        COLORREF xf = xs == 2 ? RGB(255, 255, 255) : xs == 1 ? RGB(20, 20, 20) : P.text;
                        RoundBox(hdc, xr, Sx(3), xb, acc);
                        std::wstring t = ColTitle(rcx);
                        if (t.compare(0, 4, L"RWY ") == 0) t = t.substr(4);
                        if (xs == 1) t += L"  CAUTION";
                        else if (xs == 2) t += L"  CLOSED";
                        Text(hdc, Rc(xr.left + Sx(14), xr.top, xr.right - Sx(4), xr.bottom), t, m_fHeader, xf);
                        Button b;
                        b.rc = xr;
                        b.action = A_RWYST_BASE + rcx;
                        m_buttons.push_back(b);
                        if (L.above) cell.bottom = xr.top - Sx(6);
                        else cell.top = xr.bottom + Sx(6);
                    }
            }
            std::vector<const TlItem*> its;
            for (const TlItem& it : m_tlItems)
                if (it.side == L.side && it.lane == L.lane && it.where == 2) its.push_back(&it);
            std::stable_sort(its.begin(), its.end(), [](const TlItem* a, const TlItem* b) { return a->t < b->t; });
            if (cell.bottom - cell.top > Sx(40))
                drawList(cell, std::wstring(L"HAND OVER OUT ") + (sn.empty() ? L"" : sn + L" ") + (L.lane ? L"ARR" : L"DEP"), its);
        }
    }

    // Ziehen: Zielseite hervorheben; in derselben Spalte zeigt eine gestrichelte Linie die neue Zeit
    if (m_mode == M_DRAG_STRIP && TlAbsorbed(m_dropCol))
        for (int si = 0; si < (int)m_tlSideGroup.size(); ++si)
        {
            if (m_tlSideGroup[si] != m_cols[m_dropCol].group) continue;
            RECT area = { 0, 0, 0, 0 };
            for (const TlLane& L : m_tlLanes)
                if (L.side == si) TlUnion(area, L.rc);
            area.left = pr.left + 1;
            area.right = pr.right - 1;
            Frame(hdc, area, P.hover, Sx(2));
        }
}

// Menue direkt am Label (DFS: "folgt dem Label, kurze Wege"): die wichtigsten Schritte, dazu alle Details
void StripWindow::DrawTlMenu(HDC hdc, int width, int height)
{
    int bay = -1;
    const StripData* sp = FindStrip(m_detailCs, &bay);
    if (!sp) return;
    const StripData s = *sp;
    const TlPal P = TlPalette();
    struct B { std::wstring t; int a; COLORREF bg, fg; };
    std::vector<B> b;
    const COLORREF nb = col::button, nt = col::text, white = RGB(255, 255, 255);
    bool pv = !s.freeStrip && Preview(s, bay) && !s.respMine;
    int cc = ColumnOf(s.callsign), side = 0;
    if (cc >= 0)
        for (int si = 0; si < (int)m_tlSideGroup.size(); ++si)
            if (m_tlSideGroup[si] == m_cols[cc].group) side = si;
    int rc0 = TlRwyColFor(s, side);
    bool closed = rc0 >= 0 && RwyStateOf(rc0) == 2;
    if (s.offerForMe)
    {
        b.push_back({ L"Accept", A_NEXTRESP, RGB(250, 170, 50), RGB(30, 20, 0) });
        b.push_back({ L"Reject", A_REJECT, col::danger, white });
    }
    if (pv && m_board.opt.respOn && !s.offerForMe) b.push_back({ L"Take over", A_REQ, RGB(250, 180, 70), RGB(30, 20, 0) });
    if (!s.freeStrip && s.arrival)
    {
        if (!s.onGround)
        {
            b.push_back({ s.landingCleared ? L"Cancel landing clearance" : L"Landing clearance", A_RWY_CLEAR,
                          s.landingCleared ? nb : col::ok, s.landingCleared ? nt : white });
            b.push_back({ L"Go around", A_GOAROUND, RGB(214, 60, 40), white });
            b.push_back({ L"Vacate via\x2026", A_INT_OPEN, nb, nt });
        }
        else if (bay == BAY_RUNWAY) b.push_back({ L"Vacated \x2192 Ground", A_MOVE_BASE + BAY_GNDIN, kBayAccent[BAY_GNDIN], white });
        b.push_back({ L"Stand\x2026", A_FIELD_BASE + F_STAND, nb, nt });
    }
    else if (!s.freeStrip)
    {
        if (s.airborne) b.push_back({ s.handedOver ? L"Handed over \x2713 (undo)" : L"Hand over (FREQ)", A_HOV, nb, nt });
        else if (bay == BAY_RUNWAY)
        {
            b.push_back({ s.state == GC_DEPA ? L"Cancel takeoff clearance" : L"Takeoff clearance", A_RWY_CLEAR,
                          s.state == GC_DEPA ? nb : col::ok, s.state == GC_DEPA ? nt : white });
            b.push_back({ L"Back to holding", A_MOVE_BASE + BAY_HANDOFF, nb, nt });
        }
        else if (!closed) b.push_back({ L"Line up", A_MOVE_BASE + BAY_RUNWAY, RGB(232, 180, 0), RGB(20, 20, 20) });
        if (!s.airborne)
        {
            b.push_back({ L"Hold short\x2026", A_HS_OPEN, nb, nt });
            b.push_back({ L"Intersection\x2026", A_INT_OPEN, nb, nt });
        }
    }
    if (!s.freeStrip && !s.airborne && !(s.arrival && s.onGround))
        b.push_back({ L"Sequence time\x2026", A_TL_SEQ, nb, nt });
    if (!s.freeStrip && s.planUtc > 0) b.push_back({ L"Reset sequence", A_TL_PLANRESET, nb, P.plan });
    if (!s.freeStrip && !s.arrival && !s.airborne) b.push_back({ L"SID\x2026", A_FIELD_BASE + F_SID, nb, nt });
    if (!s.freeStrip && !s.heli && !s.airborne && !(s.arrival && s.onGround)) b.push_back({ L"Runway\x2026", A_FIELD_BASE + F_RWY, nb, nt });
    if ((m_board.rwySplit || TlSplitEddv()) && m_tlSideGroup.size() >= 2 && !s.freeStrip && !s.heli && !s.airborne &&
        !(s.arrival && s.onGround))
    {
        int og = m_tlSideGroup[side == 0 ? 1 : 0];
        std::wstring on = m_board.rwyActive[og].empty() ? GroupEn(og) : W(m_board.rwyActive[og]) + L" " + GroupEn(og);
        b.push_back({ L"\x2192 Runway " + on, A_TL_OTHERSIDE, nb, nt });
    }
    if (!s.designator) b.push_back({ L"Remark\x2026", A_EDIT_OPEN, nb, nt });
    b.push_back({ L"Colour\x2026", A_TL_COLOUR, nb, nt });
    if (s.freeStrip) b.push_back({ L"Remove", A_REMOVE, col::danger, white });
    if (!s.freeStrip) b.push_back({ L"History\x2026", A_FIELD_BASE + F_HISTORY, nb, nt });
    b.push_back({ L"All details\x2026", A_TL_DETAILS, nb, nt });

    const int ncol = b.size() > 7 ? 2 : 1, nrow = ((int)b.size() + ncol - 1) / ncol;
    int w = Sx(ncol == 2 ? 440 : 280), bh = Sx(42), gap = Sx(5), pad = Sx(10), titleH = Sx(44);
    int h = pad + titleH + nrow * (bh + gap) - gap + pad;
    RECT a = m_tlMenuAnchor;
    int x = a.right + Sx(12);
    bool right = true;
    if (x + w > width - Sx(6)) { x = a.left - Sx(12) - w; right = false; }
    if (x < Sx(6)) x = Sx(6);
    int y = std::max((int)m_topBar.bottom + Sx(6), std::min((int)a.top - Sx(10), height - Sx(6) - h));
    m_detailCard = Rc(x, y, x + w, y + h);
    Fill(hdc, Rc(x + Sx(5), y + Sx(6), x + w + Sx(5), y + h + Sx(6)), col::shadow);
    RoundBox(hdc, m_detailCard, Sx(6), col::card, col::cardBorder);
    // Spitze zum Label
    {
        int ty = std::max(y + Sx(14), std::min(y + h - Sx(14), (int)(a.top + a.bottom) / 2));
        if (right) TlTri(hdc, POINT{ x + 1, ty - Sx(9) }, POINT{ x + 1, ty + Sx(9) }, POINT{ x - Sx(10), ty }, col::card, col::cardBorder);
        else       TlTri(hdc, POINT{ x + w - 2, ty - Sx(9) }, POINT{ x + w - 2, ty + Sx(9) }, POINT{ x + w + Sx(10), ty }, col::card, col::cardBorder);
    }
    std::wstring stTxt;
    COLORREF stCol = TlMix(TlStatus(s, bay, stTxt), RGB(0, 0, 0), 100 - P.statusPct);
    Fill(hdc, Rc(x + 1, y + 1, x + Sx(6), y + titleH + pad - Sx(4)), stCol);
    std::wstring title = W(s.callsign);
    if (!s.freeStrip && !s.acType.empty()) title += L"  " + W(s.acType);
    Text(hdc, Rc(x + pad + Sx(4), y + pad, x + w - pad - Sx(56), y + pad + titleH - Sx(8)), title, m_fCallsignSmall, col::text);
    if (!s.freeStrip)
    {
        int sw = TextWidth(hdc, stTxt, m_fLabel) + Sx(14);
        RECT sr = Rc(x + w - pad - sw, y + pad + Sx(6), x + w - pad, y + pad + titleH - Sx(14));
        RoundBox(hdc, sr, Sx(3), stCol, stCol);
        Text(hdc, sr, stTxt, m_fLabel, white, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    const int by = y + pad + titleH, cw = (w - 2 * pad - (ncol - 1) * gap) / ncol;
    for (int i = 0; i < (int)b.size(); ++i)
    {
        int cx = x + pad + (i / nrow) * (cw + gap), cy = by + (i % nrow) * (bh + gap);
        DrawButton(hdc, Rc(cx, cy, cx + cw, cy + bh), b[i].t, b[i].bg, b[i].fg, b[i].a, false);
    }
}

void StripWindow::DrawStrip(HDC hdc, const RECT& rcIn, const StripData& s, int bay, bool ghost)
{
    RECT rc = rcIn;
    int w = rc.right - rc.left, h = rc.bottom - rc.top;

    if (ghost)
    {
        RECT sh = rc;
        OffsetRect(&sh, Sx(5), Sx(6));
        Fill(hdc, sh, col::shadow);
    }

    // Ankuenfte auf hellblauem Papier, Abfluege auf beigem
    // angewaehlt: nur umrahmt, Papier bleibt (2.0.45)
    COLORREF paper = s.arrival ? col::paperArr : col::paper;
    if (s.freeStrip) paper = RGB(232, 232, 228);
    // farblich hervorgehoben (synchron): Papier in der Markerfarbe
    if (s.hl > 0 && s.hl < 6) paper = kHlPaper[s.hl];
    // mir zur Uebergabe angeboten: leicht hervorgehoben
    else if (s.offerForMe)
    {
        // nach ein paar Sekunden ohne Annahme: blinken (kraeftiger Orangeton im Wechsel)
        bool on = s.offerBlink && (GetTickCount() / 500) % 2 == 0;
        paper = on ? RGB(255, 196, 110) : RGB(255, 236, 200);
    }
    COLORREF line = s.arrival ? col::lineArr : col::paperLine;
    if (s.designator)
    {
        // Designator-Strip: Trennstreifen ueber die ganze Breite, Farbe (Typ antippen geht nicht: Details -> Farbe),
        // schraeg schraffiert, Text in der Mitte
        COLORREF band = (s.hl > 0 && s.hl < 6) ? kHlStrong[s.hl] : RGB(60, 70, 95);
        Fill(hdc, rc, band);
        HPEN pen = CreatePen(PS_SOLID, std::max(1, Sx(3)),
                             RGB(std::min(255, GetRValue(band) + 28), std::min(255, GetGValue(band) + 28), std::min(255, GetBValue(band) + 28)));
        HGDIOBJ op = SelectObject(hdc, pen);
        int sv = SaveDC(hdc);
        IntersectClipRect(hdc, rc.left, rc.top, rc.right, rc.bottom);
        for (int xx = rc.left - h; xx < rc.right; xx += Sx(16))
        {
            MoveToEx(hdc, xx, rc.bottom, nullptr);
            LineTo(hdc, xx + h, rc.top);
        }
        RestoreDC(hdc, sv);
        SelectObject(hdc, op);
        DeleteObject(pen);
        std::wstring t = W(s.callsign);
        while (!t.empty() && t.back() == L'*') t.pop_back();   // eindeutiger Name, ohne die Sterne
        RECT tr = Rc(rc.left + Sx(8), rc.top, rc.right - Sx(8), rc.bottom);
        int tw2 = TextWidth(hdc, t, m_fCallsign) + Sx(24);
        int cx = (rc.left + rc.right) / 2;
        RECT plate = Rc(std::max<int>(rc.left + Sx(4), cx - tw2 / 2), rc.top + h / 5, std::min<int>(rc.right - Sx(4), cx + tw2 / 2), rc.bottom - h / 5);
        Fill(hdc, plate, band);   // Text auf ruhigem Grund
        Text(hdc, tr, t, TextWidth(hdc, t, m_fCallsign) > tr.right - tr.left ? m_fCallsignSmall : m_fCallsign, RGB(255, 255, 255),
             DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        if (s.selected || (!m_detailCs.empty() && m_detailCs == s.callsign)) Frame(hdc, rcIn, RGB(20, 20, 20), Sx(2));
        return;
    }
    Fill(hdc, rc, paper);

    // Flugregeln als Rahmen oben und unten (nicht mehr als Balken an der Seite)
    COLORREF bar = col::ifr;
    if (s.freeStrip) bar = RGB(130, 130, 130);
    else if (s.rules == "V") bar = col::vfr;
    else if (s.rules == "Y" || s.rules == "Z") bar = col::mixed;
    int bandH = std::max(2, Sx(4));
    Fill(hdc, Rc(rc.left, rc.top, rc.right, rc.top + bandH), bar);
    Fill(hdc, Rc(rc.left, rc.bottom - bandH, rc.right, rc.bottom), bar);
    rc.top += bandH;
    rc.bottom -= bandH;
    h = rc.bottom - rc.top;
    int barW = Sx(2);

    int x0 = rc.left + barW;
    int innerW = rc.right - x0;
    int row1H = h * 45 / 100;
    int yMid = rc.top + row1H;
    Fill(hdc, Rc(x0, yMid, rc.right, yMid + 1), line);

    // ---- Zeile 1: Callsign | Typ/WTC | Ziel (Abflug) bzw. Herkunft (Ankunft) | Flags
    int c1 = x0 + innerW * 39 / 100;
    int c2 = x0 + innerW * 61 / 100;
    int c3 = x0 + innerW * 79 / 100;
    if (!s.freeStrip)
    {
        Fill(hdc, Rc(c1, rc.top, c1 + 1, yMid), line);
        Fill(hdc, Rc(c2, rc.top, c2 + 1, yMid), line);
    }
    Fill(hdc, Rc(c3, rc.top, c3 + 1, yMid), line);

    int px = Sx(6);
    // Notiz-Streifen: der Text darf bis zu den Marken rechts laufen
    FitText(hdc, Rc(x0 + px, rc.top, (s.freeStrip ? c3 : c1) - Sx(2), yMid), W(s.callsign), m_fCallsign, m_fCallsignSmall,
            col::ink);
    std::wstring type = W(s.acType);
    if (!s.wtc.empty()) type += L"/" + W(s.wtc);
    FitText(hdc, Rc(c1 + Sx(4), rc.top, c2 - Sx(2), yMid), type, m_fStrip, m_fStripTiny, col::ink);
    if (!ghost && !s.freeStrip) m_typeCells.push_back({ Rc(c1, rc.top, c2, yMid), s.callsign });
    FitText(hdc, Rc(c2 + Sx(4), rc.top, c3 - Sx(2), yMid), W(s.arrival ? s.adep : s.ades), m_fStrip, m_fStripTiny,
            col::ink);

    // Runway-Bay: rechts oben das Freigabefeld, wie das Abhaken auf Papier.
    // Abflug T/O = Startfreigabe (DEPA), Ankunft LDG = Landefreigabe (CTL).
    // Antippen schaltet um, der Status geht an alle (Ground State bzw. vSID).
    // Weiter-Taste ganz rechts oben: Streifen in die naechste Bay (einstellbar)
    int nextKind = 0, nextBay = -1;
    if (m_board.opt.nextBtn && !s.freeStrip) nextKind = NextStep(s, bay, nextBay);
    int r1Right = (int)rc.right - (nextKind ? Sx(28) : 0);
    if (nextKind)
    {
        RECT nb = Rc(r1Right + Sx(2), rc.top + Sx(4), rc.right - Sx(4), yMid - Sx(4));
        // blau = naechster Schritt, orange = annehmen (Haken)
        if (nextKind == 5) RoundBox(hdc, nb, Sx(6), RGB(250, 170, 50), RGB(200, 110, 20));
        else if (nextKind == 6) RoundBox(hdc, nb, Sx(6), RGB(236, 236, 232), RGB(120, 120, 120));
        else RoundBox(hdc, nb, Sx(6), RGB(70, 110, 190), RGB(40, 70, 140));
        Text(hdc, nb, nextKind >= 5 ? L"\x2713" : L"\x25B6", m_fLabel, nextKind >= 5 ? RGB(30, 20, 0) : RGB(255, 255, 255),
             DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (!ghost) m_nextCells.push_back({ Rc(nb.left - Sx(2), rc.top, rc.right, yMid), s.callsign });
    }
    if (bay == BAY_RUNWAY && !s.freeStrip)
    {
        bool on = s.arrival ? s.landingCleared : (s.state == GC_DEPA);
        const wchar_t* lab = s.arrival ? L"LDG" : L"T/O";
        RECT box = Rc(c3 + Sx(4), rc.top + Sx(4), r1Right - Sx(4), yMid - Sx(4));
        if (on) RoundBox(hdc, box, Sx(6), col::ok, RGB(20, 90, 40));
        else    RoundBox(hdc, box, Sx(6), paper, RGB(90, 84, 70));
        int bh = box.bottom - box.top;
        HFONT f = (TextWidth(hdc, lab, m_fHeader) + bh + Sx(10) <= box.right - box.left) ? m_fHeader : m_fLabel;
        int tw = TextWidth(hdc, lab, f);
        int chk = on ? bh * 6 / 10 : 0;                // Haken nur, wenn erteilt
        int gapW = on ? Sx(4) : 0;
        int x = box.left + std::max(Sx(3), (int)(box.right - box.left - chk - gapW - tw) / 2);
        if (on)
        {
            int cy = (box.top + box.bottom) / 2;
            POINT pts[3] = { { x, cy }, { x + chk / 3, cy + chk / 3 }, { x + chk, cy - chk / 2 } };
            HPEN pen = CreatePen(PS_SOLID, std::max(2, Sx(2.5)), RGB(255, 255, 255));
            HGDIOBJ op = SelectObject(hdc, pen);
            Polyline(hdc, pts, 3);
            SelectObject(hdc, op);
            DeleteObject(pen);
        }
        Text(hdc, Rc(x + chk + gapW, box.top, box.right - Sx(2), box.bottom), lab, f,
             on ? RGB(255, 255, 255) : col::ink, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        if (!ghost) m_clrCells.push_back({ box, s.callsign });
    }
    else
    // Flags rechts: Clearance, Line-up / Departure
    {
        int fx = r1Right - Sx(4);
        int ph = std::min(Sx(20), row1H - Sx(8));
        int py = rc.top + (row1H - ph) / 2;
        auto pill = [&](const wchar_t* t, COLORREF bg) {
            int pw = TextWidth(hdc, t, m_fLabel) + Sx(10);
            if (fx - pw < c3 + Sx(2)) return;
            RECT r = Rc(fx - pw, py, fx, py + ph);
            RoundBox(hdc, r, Sx(6), bg, bg);
            Text(hdc, r, t, m_fLabel, RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            fx -= pw + Sx(3);
        };
        // Hold Short: rote Marke, rechts, Antippen = aendern
        if (!s.holdShort.empty())
        {
            std::wstring hs = L"HS " + W(s.holdShort);
            int pw = TextWidth(hdc, hs, m_fLabel) + Sx(10);
            RECT r = Rc(std::max(fx - pw, c3 + Sx(2)), py, fx, py + ph);
            RoundBox(hdc, r, Sx(6), col::warn, col::warn);
            Text(hdc, r, hs, m_fLabel, RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            if (!ghost) m_hsCells.push_back({ Rc(r.left - Sx(4), rc.top, r1Right, yMid), s.callsign });
            fx = r.left - Sx(3);
        }
        // CPDLC: nur ein Farbpunkt (Antippen oeffnet das CPDLC-Fenster).
        // blau = angemeldet / DCL gesendet, orange blinkend = Anfrage (Logon, DCL),
        // magenta blinkend = neue Nachricht, rot = UNABLE
        if (!s.freeStrip && m_board.dlEnabled)
        {
            COLORREF dot = 0xFFFFFFFF;
            bool blink = (GetTickCount() / 500) % 2 == 0;
            if (s.dlUnread > 0 || s.dlPending)       dot = blink ? RGB(200, 40, 160) : RGB(140, 30, 110);
            else if (s.dlDcl == 1 || s.dlLogon == 1) dot = blink ? RGB(236, 120, 20) : RGB(180, 90, 10);
            else if (s.dlDcl == 4)                   dot = col::warn;
            else if (s.dlDcl == 2 || s.dlDcl == 3 || s.dlLogon == 2) dot = RGB(70, 110, 190);
            if (dot != 0xFFFFFFFF)
            {
                int d = std::max(Sx(8), ph * 6 / 10);
                int cyd = py + ph / 2;
                if (fx - d >= c3 + Sx(2))
                {
                    RECT dr = Rc(fx - d, cyd - d / 2, fx, cyd - d / 2 + d);
                    HBRUSH br = CreateSolidBrush(dot);
                    HPEN pen = CreatePen(PS_SOLID, 1, dot);
                    HGDIOBJ ob = SelectObject(hdc, br), op = SelectObject(hdc, pen);
                    Ellipse(hdc, dr.left, dr.top, dr.right, dr.bottom);
                    SelectObject(hdc, ob); SelectObject(hdc, op);
                    DeleteObject(br); DeleteObject(pen);
                    // grosszuegige Tippflaeche um den Punkt
                    if (!ghost) m_dlCells.push_back({ Rc(fx - d - Sx(8), rc.top, fx + Sx(3), yMid), s.callsign });
                    fx -= d + Sx(6);
                }
            }
        }
        // Durchstarten geht vor (wichtiger als die Zustaendigkeit)
        if (!s.freeStrip && s.arrival && s.goAround) pill(L"G/A", RGB(214, 60, 40));
        // Zustaendigkeit (synchron): ganz links in der Gruppe. Deine Stelle kraeftig, andere blass.
        // Feld oben rechts: Zustaendigkeit; antippen = uebergeben (Menue) bzw. Angebot annehmen.
        // Zustaendigkeit aus (Settings): kein Feld
        if (!s.freeStrip && m_board.opt.respOn)
        {
            COLORREF rc0 = s.resp.empty() ? RGB(150, 150, 150) : RespColor(s.resp);
            COLORREF bg = s.respMine ? rc0 : RGB((GetRValue(rc0) + 2 * 200) / 3, (GetGValue(rc0) + 2 * 200) / 3, (GetBValue(rc0) + 2 * 200) / 3);
            COLORREF fg = s.respMine ? RGB(255, 255, 255) : RGB(40, 40, 40), frame = s.respMine ? bg : rc0;
            std::wstring lab = s.resp.empty() ? std::wstring(L"\x2013") : RespLabel(s.resp);
            std::wstring shortLab = s.resp.empty() ? lab : W(s.resp.substr(0, 3)), tinyLab = s.resp.empty() ? lab : W(s.resp.substr(0, 1));
            if (s.offerForMe)
            {
                // an mich angeboten: annehmen
                bg = RGB(250, 170, 50); frame = RGB(200, 110, 20); fg = RGB(30, 20, 0);
                lab = L"ACCEPT"; shortLab = L"ACC"; tinyLab = L"\x2713";
            }
            else if (!s.offerRejectedBy.empty())
            {
                // mein Angebot wurde abgelehnt: 30 s rot
                bg = RGB(215, 50, 45); frame = RGB(150, 20, 20); fg = RGB(255, 255, 255);
                lab = L"REJECTED " + RespLabel(s.offerRejectedBy); shortLab = L"REJ"; tinyLab = L"\x2715";
            }
            else if (!s.offerTo.empty())
            {
                // ich (oder jemand) bietet an: wartet auf Annahme
                frame = RGB(230, 140, 30);
                lab = (s.resp.empty() ? std::wstring() : RespLabel(s.resp) + L" ") + L"\x2192 " + RespLabel(s.offerTo);
                shortLab = L"\x2192 " + W(s.offerTo.substr(0, 3)); tinyLab = L"\x2192";
            }
            int pw0 = TextWidth(hdc, lab, m_fLabel) + Sx(10);
            // schmale Spalte: ohne Seite ("GND" statt "GND N"), noch schmaler nur der Anfangsbuchstabe
            if (fx - pw0 < c3 + Sx(2)) { lab = shortLab; pw0 = TextWidth(hdc, lab, m_fLabel) + Sx(10); }
            if (fx - pw0 < c3 + Sx(2)) { lab = tinyLab; pw0 = TextWidth(hdc, lab, m_fLabel) + Sx(8); }
            if (fx - pw0 >= c3 + Sx(2))
            {
                RECT r = Rc(fx - pw0, py, fx, py + ph);
                RoundBox(hdc, r, Sx(6), bg, frame);
                if (!s.offerTo.empty() && !s.offerForMe)
                {
                    RECT r2 = Rc(r.left + Sx(1), r.top + Sx(1), r.right - Sx(1), r.bottom - Sx(1));
                    Frame(hdc, r2, frame, Sx(1));
                }
                Text(hdc, r, lab, m_fLabel, fg, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                if (!ghost) m_respCells.push_back({ Rc(r.left - Sx(2), rc.top, r.right + Sx(2), yMid), s.callsign });
                fx -= pw0 + Sx(3);
            }
        }
        if (s.freeStrip)                 pill(L"NOTE", RGB(120, 120, 120));
        else if (s.arrival)
        {
            if (!s.divertFrom.empty())   pill(L"DIV", RGB(190, 50, 150));   // Ausweichlandung
            if (s.standConflict)         pill(L"STAND!", col::warn);
            if (s.goAround)              {}   // schon oben
            else if (s.landingCleared)   pill(L"CTL", RGB(40, 150, 120));
            else if (s.state == GC_TXIN) pill(L"TXI", RGB(150, 96, 36));
            else                         pill(L"ARR", RGB(190, 122, 40));
        }
        else
        {
            if (s.standConflict)           pill(L"STAND!", col::warn);
            // Line-up ohne eigene Marke (steht schon in der Runway-Bay bzw. im Ground State)
            if (s.state == GC_LINEUP)      {}
            else if (s.state == GC_DEPA)   pill(L"TO", col::ok);
            else if (s.handedOff && s.resp.empty()) pill(L"TWR", RGB(54, 104, 168));
            else if (s.toGround && s.resp.empty())  pill(L"GND", RGB(84, 150, 60));
        }
    }

    // ---- Zeile 2: Felder mit kleinen Beschriftungen wie auf Papierstreifen
    const COLORREF kNoFill = 0xFFFFFFFF;
    struct Cell { const wchar_t* label; std::wstring value; int pct; COLORREF color; bool green = false; bool big = false;
                  COLORREF fill = 0xFFFFFFFF; };
    std::wstring sqk = W(s.squawkAssigned);   // nur der zugewiesene, nicht der Transponder
    // Transponder sendet einen anderen Code: bei Abfluegen erst in der Luft (am Boden ist der Code oft noch nicht gerastet)
    bool sqkWrong = !s.squawkAssigned.empty() && !s.squawkCurrent.empty() && s.squawkAssigned != s.squawkCurrent &&
                    (s.arrival || s.airborne);
    std::vector<Cell> cells;
    if (s.arrival && !s.onGround)
    {
        // Ankunft in der Luft: Piste ganz links, dann Parkposition gross
        cells = {
            { L"RWY",   Or(s.arrRwy, L""),                       10, col::ink },
            { L"STAND", Or(s.stand, L""),                        19, col::ink, false, true },
            { L"CFL",   CflText(s, m_board.transitionAltitude),  14, col::ink },
            { L"SQK",   sqk,                                     13, sqkWrong ? col::warn : col::ink },
            { L"FREQ",  Or(s.nextFreq, L"-"),                     17, col::ink },
            { L"REMARK", Or(s.scratch, L""),                      27, col::ink },
        };
    }
    else if (s.arrival)
    {
        // Ankunft am Boden: Parkposition gross vorne
        cells = {
            { L"RWY",   Or(s.arrRwy, L""),   10, col::ink },
            { L"STAND", Or(s.stand, L""),    25, col::ink, false, true },
            { L"STD",   L"",                  8, col::ink },   // Haekchen: bis zur Parkposition freigegeben
            { L"SQK",   sqk,                 13, sqkWrong ? col::warn : col::ink },
            { L"FREQ",  Or(s.nextFreq, L"-"), 17, col::ink },
            { L"REMARK", Or(s.scratch, L""),  27, col::ink },
        };
        if (s.stdClr) cells[2].fill = RGB(176, 222, 176);
    }
    else
    {
        // Abflug: Piste ganz links, daneben die Intersection (vSID)
        cells = {
            { L"RWY",  Or(s.rwy, L""),                         10, col::ink },
            { L"INT",  Or(s.intersection, L""),                 8, s.intAssigned ? col::ink : col::inkDim },
            { L"SID",  Or(s.sid, L""),                         17, col::ink },
            { L"SQK",  sqk,                                    12, sqkWrong ? col::warn : col::ink },
            { L"CFL",  CflText(s, m_board.transitionAltitude), 12, col::ink },
            { L"TSAT", Or(s.tsat, L""),                        11, col::ink, InTsatWindow(s) },
            { L"FREQ", Or(s.nextFreq, L"-"),                    15, col::ink },
            { L"REMARK", Or(s.scratch, L""),                     15, col::ink },
        };
        // Vor dem Pushback (Pending, On Freq, Startup): statt INT das Feld CLR mit dem
        // Clearance-Kaestchen (leer / ausgefuellt). Ab Pushback wieder INT.
        bool prePush = (bay == BAY_PENDING || bay == BAY_ONFREQ || bay == BAY_STARTUP) &&
                       s.state != GC_PUSH && s.state != GC_TAXI && s.state != GC_LINEUP && s.state != GC_DEPA;
        // Startup und Pushback: Push-Richtung (Nase) als eigenes Feld "PB"
        bool pbCell = bay == BAY_STARTUP || bay == BAY_PUSHBACK || s.state == GC_STUP || s.state == GC_PUSH;
        if (prePush)
        {
            cells[1].label = L"CLR";
            cells[1].value.clear();
            cells[1].fill = kNoFill;
            // vor dem Pushback auch die Parkposition (rechts angehaengt, die anderen etwas schmaler)
            cells[2].pct = 15; cells[3].pct = 10; cells[4].pct = 10; cells[6].pct = 13; cells[7].pct = 13;
            cells.push_back({ L"STAND", Or(s.stand, L""), 10, col::ink });
            if (pbCell) { cells[2].pct = 13; cells[5].pct = 10; cells[6].pct = 12; cells[7].pct = 11; }
        }
        else if (pbCell) cells[7].pct = 9;
        if (pbCell) cells.push_back({ L"PB", Or(s.pushDir, L""), 6, col::ink });
        // vSID-Pruefung: SID-Feld rot mit dem Grund (wie das SID-Item von vSID: MANUAL, EQUIP)
        if (!s.vsidErr.empty())
        {
            cells[2].fill = RGB(240, 96, 80);
            cells[2].color = RGB(255, 255, 255);
            if (s.vsidErr == "MANUAL" || s.vsidErr == "EQUIP") cells[2].value = W(s.vsidErr);
        }
        // Tower: statt TSAT die CTOT (Slot), ohne Slot die TTOT in grau
        if (m_view == VIEW_TWR)
        {
            if (!s.ctot.empty())
            {
                // Slotfenster: orange frueh, gelb ab -15, gruen -5/+10, rot verpasst
                cells[5].label = L"CTOT"; cells[5].value = W(s.ctot); cells[5].color = col::ink;
                int cst = CtotState(s);
                cells[5].fill = kCtotFill[cst > 0 ? cst : 1];   // mit Slot immer farbig hinterlegt
            }
            else                 { cells[5].label = L"TTOT"; cells[5].value = Or(s.ttot, L""); cells[5].color = col::inkDim; }
            cells[5].green = false;
        }
        // Intersection zugewiesen: hellgruen hinterlegt (vSID: gruen), able for: grau
        if (s.intAssigned && wcscmp(cells[1].label, L"INT") == 0) cells[1].fill = RGB(176, 222, 176);
        // In der Luft und noch nicht weggeschickt: FREQ orange (vSID: HOV),
        // rot, sobald der Flieger nahe an seiner CFL ist (vSID: HOV!)
        if (s.hovDue && !s.handedOver)
        {
            bool late = s.clearedAlt > 0 && s.altitude >= s.clearedAlt - 1500;
            cells[6].fill = late ? RGB(240, 96, 80) : RGB(250, 180, 70);
        }
    }
    // Getrennte Pisten: RWY-Feld in der Farbe der Piste, orange wenn die Piste im Tag zu keiner passt
    if (m_board.rwySplit)
    {
        for (Cell& c : cells)
        {
            if (wcscmp(c.label, L"RWY") != 0) continue;
            if (s.rwyGroup == RWY_A || s.rwyGroup == RWY_B) c.fill = kRwyPaper[s.rwyGroup];
            else if (s.rwyUnknown)
            {
                c.fill = kRwyUnknown;
                if (c.value.empty()) c.value = L"?";
            }
        }
    }

    // Piste fuer diese Richtung nicht aktiv: RWY rot. Stand doppelt belegt: STAND rot
    for (Cell& c : cells)
    {
        if (s.rwyInactive && wcscmp(c.label, L"RWY") == 0) { c.fill = RGB(240, 96, 80); c.color = RGB(255, 255, 255); }
        if (s.standConflict && wcscmp(c.label, L"STAND") == 0) { c.fill = RGB(240, 96, 80); c.color = RGB(255, 255, 255); }
        // Ankunft: Stand laut GRP besetzt/gesperrt (rot) oder am Boden noch ohne Stand ("?")
        if (s.arrival && wcscmp(c.label, L"STAND") == 0)
        {
            if (s.standBusy) { c.fill = RGB(240, 96, 80); c.color = RGB(255, 255, 255); }
            else if (s.standMissing) { c.fill = RGB(250, 180, 70); c.value = L"?"; }
        }
        // Auf der Piste ohne Mode C: SQK rot "STBY"
        if (s.xpdrOff && wcscmp(c.label, L"SQK") == 0) { c.fill = RGB(240, 96, 80); c.color = RGB(255, 255, 255); c.value = L"STBY"; }
        // vACDM meldet einen Fehler: TSAT-Feld orange, ohne TSAT "ERR"
        if (!m_board.vacdmError.empty() && !s.arrival && wcscmp(c.label, L"TSAT") == 0)
            { c.fill = RGB(250, 160, 60); c.green = false; if (c.value.empty()) c.value = L"ERR"; }
        // TSAT abgelaufen (+5 min, noch kein Startup): rot
        if (m_view != VIEW_TWR && wcscmp(c.label, L"TSAT") == 0 && TsatExpired(s))
            { c.fill = RGB(240, 96, 80); c.color = RGB(255, 255, 255); c.green = false; }
    }
    // Uebergeben: FREQ orange (antippen = entfernen), wie beim Wegschicken von Abfluegen
    bool handed = HandedOver(s, bay), park = CanPark(s, bay);
    if (handed)
        for (Cell& c : cells)
            if (wcscmp(c.label, L"FREQ") == 0) { c.fill = RGB(250, 180, 70); if (c.value.empty() || c.value == L"-") c.value = L"\x2713"; }
    if (s.freeStrip) cells.clear();   // Notiz-Streifen: nur der Text oben
    int cx = x0;
    int labelH = Sx(13);
    for (size_t i = 0; i < cells.size(); ++i)
    {
        int cw = (i + 1 == cells.size()) ? rc.right - cx : innerW * cells[i].pct / 100;
        if (cells[i].green) Fill(hdc, Rc(cx + 1, yMid + 1, cx + cw, rc.bottom), col::tsatPaper);
        else if (cells[i].fill != kNoFill) Fill(hdc, Rc(cx + 1, yMid + 1, cx + cw, rc.bottom), cells[i].fill);
        if (i > 0) Fill(hdc, Rc(cx, yMid, cx + 1, rc.bottom), line);
        Text(hdc, Rc(cx + Sx(3), yMid + Sx(2), cx + cw - Sx(1), yMid + Sx(2) + labelH), cells[i].label,
             m_fLabel, cells[i].green ? RGB(20, 70, 20) : col::inkDim, DT_LEFT | DT_TOP | DT_SINGLELINE);
        FitText(hdc, Rc(cx + Sx(3), yMid + labelH, cx + cw - Sx(1), rc.bottom - Sx(1)), cells[i].value,
                cells[i].big ? m_fCallsignSmall : m_fStripSmall, cells[i].big ? m_fStripSmall : m_fStripTiny,
                cells[i].color);
        if (!ghost && wcscmp(cells[i].label, L"REMARK") == 0)
            m_spCells.push_back({ Rc(cx, yMid, cx + cw, rc.bottom), s.callsign });
        if (!ghost && wcscmp(cells[i].label, L"PB") == 0)
            m_pbCells.push_back({ Rc(cx, yMid, cx + cw, rc.bottom), s.callsign });
        if (!ghost && wcscmp(cells[i].label, L"TSAT") == 0 && m_board.opt.vacdm)
            m_tsatCells.push_back({ Rc(cx, yMid, cx + cw, rc.bottom), s.callsign });
        if (!ghost && wcscmp(cells[i].label, L"RWY") == 0)
            m_rwyCells.push_back({ Rc(cx, yMid, cx + cw, rc.bottom), s.callsign });
        if (!ghost && wcscmp(cells[i].label, L"INT") == 0)
            m_intCells.push_back({ Rc(cx, yMid, cx + cw, rc.bottom), s.callsign });
        if (!ghost && wcscmp(cells[i].label, L"SQK") == 0)
            m_sqkCells.push_back({ Rc(cx, yMid, cx + cw, rc.bottom), s.callsign });
        if (wcscmp(cells[i].label, L"CLR") == 0)
        {
            // Clearance-Kaestchen wie auf dem Papierstreifen: leer = keine Freigabe, ausgefuellt =
            // freigegeben, blauer Rand = DCL per CPDLC gesendet. Antippen = umschalten (nicht beim Tower).
            int top = yMid + labelH, d = std::max(Sx(10), std::min(cw - Sx(10), (int)(rc.bottom - top) - Sx(4)));
            int sx = cx + (cw - d) / 2, sy = top + ((int)(rc.bottom - top) - d) / 2;
            RECT sq = Rc(sx, sy, sx + d, sy + d);
            bool sent = !s.clearance && s.dlDcl == 2;
            bool req = !s.clearance && s.dlDcl == 1 && m_board.dlEnabled;   // DCL-Anfrage: orange, blinkt
            bool blink = (GetTickCount() / 500) % 2 == 0;
            bool ready = req && !s.dlDclText.empty() && s.dlDclText[0] != '!';
            if (ready)
            {
                // PDC fertig: gruene Taste "PDC", blinkt - ein Tipp sendet
                RECT pb = Rc(cx + Sx(2), top + Sx(1), cx + cw - Sx(2), rc.bottom - Sx(3));
                RoundBox(hdc, pb, Sx(5), blink ? RGB(46, 150, 80) : RGB(30, 110, 60), RGB(20, 90, 45));
                Text(hdc, pb, L"PDC", m_fLabel, RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            else
            {
                Fill(hdc, sq, s.clearance ? col::ink : paper);
                Frame(hdc, sq, req ? (blink ? RGB(236, 120, 20) : RGB(180, 90, 10)) : sent ? RGB(70, 110, 190) : col::ink,
                      std::max(2, Sx(req ? 3 : 2)));
            }
            if (!ghost && m_view != VIEW_TWR) m_depClrCells.push_back({ Rc(cx, yMid, cx + cw, rc.bottom), s.callsign });
        }
        if (wcscmp(cells[i].label, L"STD") == 0)
        {
            // Taxi In: Kaestchen leer = noch nicht bis zum Stand freigegeben, mit Haken = freigegeben
            int top = yMid + labelH, d = std::max(Sx(10), std::min(cw - Sx(10), (int)(rc.bottom - top) - Sx(4)));
            int sx = cx + (cw - d) / 2, sy = top + ((int)(rc.bottom - top) - d) / 2;
            RECT sq = Rc(sx, sy, sx + d, sy + d);
            Fill(hdc, sq, s.stdClr ? RGB(46, 150, 80) : paper);
            Frame(hdc, sq, s.stdClr ? RGB(30, 110, 60) : col::ink, std::max(2, Sx(2)));
            if (s.stdClr) Text(hdc, sq, L"\x2713", m_fStripSmall, RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            if (!ghost) m_stdClrCells.push_back({ Rc(cx, yMid, cx + cw, rc.bottom), s.callsign });
        }
        if (!ghost && !s.arrival && s.airborne && wcscmp(cells[i].label, L"FREQ") == 0)
            m_hovCells.push_back({ Rc(cx, yMid, cx + cw, rc.bottom), s.callsign });
        if (!ghost && handed && wcscmp(cells[i].label, L"FREQ") == 0)
            m_goneCells.push_back({ Rc(cx, yMid, cx + cw, rc.bottom), s.callsign });
        if (park && wcscmp(cells[i].label, L"STAND") == 0)
        {
            // kleine Marke "P": antippen = geparkt
            int pw = Sx(22), ph = Sx(18);
            RECT pr = Rc(cx + cw - pw - Sx(3), yMid + Sx(3), cx + cw - Sx(3), yMid + Sx(3) + ph);
            RoundBox(hdc, pr, Sx(5), RGB(46, 150, 80), RGB(46, 150, 80));
            Text(hdc, pr, L"P", m_fLabel, RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            // nur die Marke parkt (am Gate, < 3 kt); der Rest des Feldes ist die Standauswahl
            if (!ghost) m_parkCells.push_back({ Rc(pr.left - Sx(4), yMid, cx + cw, rc.bottom), s.callsign });
        }
        // STAND antippen = Standauswahl (mit Auto), nie geparkt
        if (!ghost && wcscmp(cells[i].label, L"STAND") == 0)
            m_standCells.push_back({ Rc(cx, yMid, cx + cw, rc.bottom), s.callsign });
        cx += cw;
    }

    // Hold Short: zusaetzlich rote Linie oben ueber den ganzen Streifen
    if (!s.holdShort.empty()) Fill(hdc, Rc(rc.left, rc.top, rc.right, rc.top + Sx(3)), col::warn);

    // Rahmen
    // Vorankuendigung: noch bei der vorherigen Stelle -> grau, nach der Uebergabe in Farbe
    if (!ghost && Preview(s, bay)) WashRect(hdc, rcIn, RGB(200, 200, 200), 150);
    // Startup-Vorschlag: TSAT-Fenster erreicht, noch in Pending / On Freq, meins (oder niemandes) -> blinkt gruen
    bool suStart = m_board.opt.tsatSort && !s.arrival && !s.freeStrip && (bay == BAY_PENDING || bay == BAY_ONFREQ) &&
                   (s.respMine || s.resp.empty()) && InTsatWindow(s) && !ghost;
    if (s.offerBlink || suStart || (s.dlDcl == 1 && m_board.dlEnabled) || s.dlUnread > 0) m_anyBlink = true;
    // Notfall-Squawk (Transponder oder zugewiesen): roter Rand
    auto emer = [](const std::string& q) { return q == "7500" || q == "7600" || q == "7601" || q == "7700"; };
    if (!s.freeStrip && (emer(s.squawkCurrent) || emer(s.squawkAssigned)))
    {
        Frame(hdc, rcIn, RGB(220, 30, 30), Sx(4));
        if (s.selected) Frame(hdc, Inset(rcIn, Sx(4), Sx(4)), RGB(25, 25, 25), Sx(2));
    }
    else if (suStart && (GetTickCount() / 500) % 2 == 0) Frame(hdc, rcIn, RGB(46, 160, 80), Sx(4));       // Startup moeglich: blinkt
    else if (s.selected) Frame(hdc, rcIn, RGB(25, 25, 25), Sx(2));   // aktueller Streifen: leichter schwarzer Rahmen
    else            Frame(hdc, rcIn, col::stripFrame, 1);

    // Nur lokal verschoben (EuroScope hat den Status nicht angenommen): orange Ecke
    if (s.localOnly)
    {
        int t = Sx(14);
        POINT tri[3] = { { rc.right - t, rc.top }, { rc.right, rc.top }, { rc.right, rc.top + t } };
        HGDIOBJ ob = SelectObject(hdc, GetStockObject(DC_BRUSH));
        HGDIOBJ op = SelectObject(hdc, GetStockObject(DC_PEN));
        SetDCBrushColor(hdc, col::local);
        SetDCPenColor(hdc, col::local);
        Polygon(hdc, tri, 3);
        SelectObject(hdc, ob);
        SelectObject(hdc, op);
    }
    (void)bay;
    (void)w;
}

void StripWindow::DrawDetail(HDC hdc, int width, int height)
{
    int bay = -1;
    const StripData* sp = FindStrip(m_detailCs, &bay);
    if (!sp) return;
    const StripData s = *sp;   // Kopie, falls waehrend des Zeichnens ein neues Board kommt
    const int ta = m_board.transitionAltitude;

    // Aenderungsmenue rechts angedockt (wie ein Eigenschaftsfeld), die Bays bleiben sichtbar
    int cardW = std::max(std::min(width - Sx(16), Sx(460)), std::min(width * 45 / 100, Sx(760)));
    int cols = cardW >= Sx(660) ? 3 : 2;

    // Felder
    struct Field { const wchar_t* label; std::wstring value; bool warn; bool green; };
    std::vector<Field> f;
    std::wstring rules = L"-";
    if (s.rules == "I") rules = L"IFR";
    else if (s.rules == "V") rules = L"VFR";
    else if (s.rules == "Y") rules = L"Y (IFR \x2192 VFR)";
    else if (s.rules == "Z") rules = L"Z (VFR \x2192 IFR)";
    else if (!s.rules.empty()) rules = W(s.rules);
    std::wstring comm = L"-";
    if (s.comm == "V") comm = L"Voice";
    else if (s.comm == "R") comm = L"Receive only";
    else if (s.comm == "T") comm = L"Text only";
    std::wstring gstate = s.groundStateRaw.empty() ? L"NSTS" : W(s.groundStateRaw);
    if (s.localOnly) gstate += L" (local only)";
    if (s.handedOff) gstate += L" (handed to tower)";
    wchar_t gsalt[64];
    swprintf(gsalt, 64, L"%d kt  /  %d ft", s.groundSpeed, s.altitude);

    std::wstring hsText = s.holdShort.empty() ? std::wstring(L"-") : W(s.holdShort);

    // Naechste Frequenz mit Station: "119.625  (EDDB_N_APP)"
    std::wstring freqText = s.nextFreq.empty() ? std::wstring(L"-") : W(s.nextFreq) + L"  (" + W(s.nextCtrl) + L")";
    std::wstring cflText = CflText(s, ta);
    if (cflText.empty()) cflText = L"-";

    // Piste aus dem Tag, bei getrennten Pisten mit Gruppe: "24R  (NORD)"
    std::wstring rwyText = Or(s.arrival ? s.arrRwy : s.rwy);
    bool rwyWarn = s.rwyInactive;
    if (s.rwyInactive) rwyText += L"  (not active!)";
    if (m_board.rwySplit)
    {
        if (s.rwyGroup == RWY_A || s.rwyGroup == RWY_B) rwyText += L"  (" + GroupEn(s.rwyGroup) + L")";
        else if (s.rwyUnknown)
        {
            rwyText += L"  (no runway group)";
            rwyWarn = true;
        }
    }

    if (s.freeStrip)
    {
        // Notiz-Streifen: nur Hold Short
        f.push_back({ L"HOLD SHORT", hsText, !s.holdShort.empty() });
    }
    else if (s.arrival && !s.onGround)
    {
        // Ankunft in der Luft: Parkposition zuerst
        f.push_back({ L"STAND", StandDetail(s), s.standConflict || s.standBusy || s.standMissing });
        f.push_back({ L"HOLD SHORT", hsText, !s.holdShort.empty() });
        f.push_back({ L"RUNWAY", rwyText, rwyWarn });
        f.push_back({ L"SQUAWK", Or(s.squawkAssigned), false });
        f.push_back({ L"ORIGIN", Or(s.adep) + (s.divertFrom.empty() ? std::wstring() : L"  (diversion, planned " + W(s.divertFrom) + L")"),
                      !s.divertFrom.empty() });
        f.push_back({ L"TYPE / WAKE", Or(s.acType) + L" / " + Or(s.wtc), false });
        f.push_back({ L"STAR", Or(s.star), false });
        f.push_back({ L"LANDING CLEARANCE (CTL)", s.landingCleared ? L"given" : L"no", false, s.landingCleared });
        f.push_back({ L"CFL", cflText, false });
        f.push_back({ L"NEXT FREQUENCY", freqText, false });
        f.push_back({ L"RADIO", comm, s.comm == "T" || s.comm == "R" });
        f.push_back({ L"GS / ALT", gsalt, false });
        f.push_back({ L"SCRATCHPAD", Or(s.scratch), false });
        f.push_back({ L"FLIGHT RULES", rules, false });
    }
    else if (s.arrival)
    {
        // Ankunft am Boden: Parkposition zuerst
        f.push_back({ L"STAND", StandDetail(s), s.standConflict || s.standBusy || s.standMissing });
        f.push_back({ L"HOLD SHORT", hsText, !s.holdShort.empty() });
        f.push_back({ L"RUNWAY", rwyText, rwyWarn });
        f.push_back({ L"STAR", Or(s.star), false });
        f.push_back({ L"SQUAWK", Or(s.squawkAssigned), false });
        f.push_back({ L"ORIGIN", Or(s.adep), false });
        f.push_back({ L"TYPE / WAKE", Or(s.acType) + L" / " + Or(s.wtc), false });
        f.push_back({ L"GROUND STATE", gstate, s.localOnly });
        if (m_board.opt.respOn) f.push_back({ L"RESPONSIBLE", s.resp.empty() ? std::wstring(L"-") : RespLabel(s.resp), s.respMine });
        f.push_back({ L"NEXT FREQUENCY", freqText, false });
        f.push_back({ L"SCRATCHPAD", Or(s.scratch), false });
        f.push_back({ L"RADIO", comm, s.comm == "T" || s.comm == "R" });
        f.push_back({ L"FLIGHT RULES", rules, false });
        f.push_back({ L"GS / ALT", gsalt, false });
    }
    else
    {
        // Reihenfolge wie beim Erteilen der Clearance: SID, Initial Altitude, Squawk
        f.push_back({ L"SID", Or(s.sid), !s.vsidErr.empty() });
        if (!s.vsidErr.empty()) f.push_back({ L"CHECK (vSID)", W(s.vsidErr) + L": " + W(s.vsidErrText), true });
        f.push_back({ L"HOLD SHORT", hsText, !s.holdShort.empty() });
        f.push_back({ L"INTERSECTION", s.intersection.empty() ? std::wstring(L"-")
                                       : W(s.intersection) + (s.intAssigned ? L"  (assigned)" : L"  (able for)"),
                      false, s.intAssigned });
        f.push_back({ L"INITIAL ALTITUDE", cflText, false });
        f.push_back({ L"SQUAWK", Or(s.squawkAssigned), false });   // nur der zugewiesene aus dem Plan
        f.push_back({ L"FINAL ALTITUDE", FmtAlt(s.finalAlt, ta), false });
        f.push_back({ L"RUNWAY", rwyText, rwyWarn });
        {
            std::wstring t = s.tsat.empty() ? L"-" : W(s.tsat) + L"Z";
            if (!s.tsatSource.empty()) t += L"  (" + W(s.tsatSource) + L")";
            f.push_back({ L"TSAT", t, false, InTsatWindow(s) });
            std::wstring c = s.ctot.empty() ? L"-" : W(s.ctot) + L"Z";
            if (!s.ctotSource.empty()) c += L"  (" + W(s.ctotSource) + L")";
            int cst = CtotState(s);
            if (cst == 4) c += L"  missed";
            f.push_back({ L"CTOT", c, cst == 4, cst == 3 });
            f.push_back({ L"TTOT", s.ttot.empty() ? std::wstring(L"-") : W(s.ttot) + L"Z", false });
        }
        f.push_back({ L"FLIGHT RULES", rules, false });
        f.push_back({ L"TYPE / WAKE", Or(s.acType) + L" / " + Or(s.wtc), false });
        f.push_back({ L"DESTINATION", Or(s.ades), false });
        f.push_back({ L"ALTERNATE", Or(s.altn), false });
        f.push_back({ L"GROUND STATE", gstate, s.localOnly });
        if (m_board.opt.respOn) f.push_back({ L"RESPONSIBLE", s.resp.empty() ? std::wstring(L"-") : RespLabel(s.resp), s.respMine });
        f.push_back({ L"CLEARANCE", ClearanceText(s), s.clearance });
        f.push_back({ L"NEXT FREQUENCY", freqText, false });
        f.push_back({ L"SCRATCHPAD", Or(s.scratch), false });
        f.push_back({ L"RADIO", comm, s.comm == "T" || s.comm == "R" });
        {
            wchar_t buf[32];
            swprintf(buf, 32, L"%03d", s.assignedHeading);
            f.push_back({ L"HEADING", s.assignedHeading > 0 ? std::wstring(buf) : std::wstring(L"-"), false });
            swprintf(buf, 32, L"%d kt", s.assignedSpeed);
            f.push_back({ L"SPEED", s.assignedSpeed > 0 ? std::wstring(buf) : std::wstring(L"-"), false });
        }
        f.push_back({ L"GS / ALT", gsalt, false });
        f.push_back({ L"EOBT", s.eobt.empty() ? L"-" : W(s.eobt) + L"Z", false });
    }

    int pad = Sx(20);
    int titleH = Sx(98);   // Callsign und darunter Rufzeichen und Ziel ausgeschrieben
    int fieldH = Sx(54);
    int fieldRows = ((int)f.size() + cols - 1) / cols;
    int routeH = Sx(104);   // Route mehrzeilig (bis 4 Zeilen)
    int remarksH = Sx(64);
    int btnH = Sx(54);
    int cardH = pad + titleH + fieldRows * fieldH + Sx(8) + routeH + remarksH + Sx(10) + btnH + Sx(10) + btnH + pad;
    if (!s.freeStrip) cardH += btnH + Sx(10);   // Reihe Zustaendigkeit / Richtung / Diversion

    // Zu wenig Platz: Route und Bemerkungen kuerzer
    int maxH = height - m_topBar.bottom;
    if (cardH > maxH)
    {
        int over = cardH - maxH;
        int cut = std::min(over, routeH + remarksH - Sx(70));
        remarksH -= cut / 2;
        routeH -= cut - cut / 2;
        cardH -= cut;
    }

    int cx = width - cardW;
    int cy = m_topBar.bottom;
    if (cardH < height - cy) cardH = height - cy;   // bis ganz unten
    m_detailCard = Rc(cx, cy, cx + cardW, cy + cardH);
    Fill(hdc, Rc(cx - Sx(4), cy, cx, cy + cardH), col::shadow);
    RoundBox(hdc, m_detailCard, 0, col::card, col::cardBorder);
    Fill(hdc, Rc(cx + 1, cy + 1, cx + Sx(6), cy + titleH + pad - Sx(4)), kBayAccent[bay < 0 ? 0 : bay]);

    int saved = SaveDC(hdc);
    IntersectClipRect(hdc, m_detailCard.left, m_detailCard.top, m_detailCard.right, m_detailCard.bottom);

    // Titel
    int x = cx + pad, y = cy + pad;
    std::wstring cs = W(s.callsign);
    int line1 = Sx(54);
    Text(hdc, Rc(x, y, x + Sx(300), y + line1), cs, m_fDetailTitle, col::text);
    int tx = x + TextWidth(hdc, cs, m_fDetailTitle) + Sx(18);
    std::wstring sub = s.freeStrip ? L"Note strip   " + std::wstring(kBayTitle[bay < 0 ? 0 : bay])
                                   : Or(s.acType) + L"   " + Or(s.adep) + L" \x2192 " + Or(s.ades) + L"   " +
                                         std::wstring(kBayTitle[bay < 0 ? 0 : bay]);
    Text(hdc, Rc(tx, y, cx + cardW - Sx(80), y + line1), sub, m_fDetailValue, col::textDim);

    // Zweite Zeile: Rufzeichen und Ziel (bei Ankuenften Herkunft) ausgeschrieben
    {
        RECT l2 = Rc(x, y + line1, cx + cardW - pad, y + titleH - Sx(10));
        std::wstring spoken = W(s.spoken);
        std::wstring place = s.arrival ? W(s.adepName) : W(s.adesName);
        if (s.freeStrip) {}
        else if (spoken.empty() && place.empty())
        {
            Text(hdc, l2, m_board.icaoLoaded ? std::wstring(L"Callsign and airport not in the ICAO data")
                                             : std::wstring(L"ICAO data not found (.strips icao)"),
                 m_fDetailLabel, col::textDim);
        }
        else
        {
            int lx = l2.left;
            if (!spoken.empty())
            {
                Text(hdc, Rc(lx, l2.top, l2.right, l2.bottom), spoken, m_fDetailValue, kBayAccent[BAY_TAXI]);
                lx += TextWidth(hdc, spoken, m_fDetailValue) + Sx(20);
            }
            if (!place.empty())
                Text(hdc, Rc(lx, l2.top, l2.right, l2.bottom), (s.arrival ? L"from " : L"to ") + place,
                     m_fDetailValue, col::text);
        }
    }
    DrawButton(hdc, Rc(cx + cardW - pad - Sx(48), y + Sx(4), cx + cardW - pad, y + Sx(52)), L"\x00D7",
               col::button, col::text, A_CLOSE_DETAIL, false);
    y += titleH;
    Fill(hdc, Rc(x, y - Sx(6), cx + cardW - pad, y - Sx(5)), col::cardBorder);

    // Feldraster
    int colW = (cardW - 2 * pad) / cols;
    for (size_t i = 0; i < f.size(); ++i)
    {
        int c = (int)i % cols, r = (int)i / cols;
        int fx = x + c * colW, fy = y + r * fieldH;
        Text(hdc, Rc(fx, fy + Sx(4), fx + colW - Sx(10), fy + Sx(20)), f[i].label, m_fDetailLabel, col::textDim,
             DT_LEFT | DT_TOP | DT_SINGLELINE);
        RECT vr = Rc(fx, fy + Sx(20), fx + colW - Sx(10), fy + fieldH - Sx(4));
        if (f[i].green)
        {
            // gruen hinterlegt, solange die Zeit im TSAT-Fenster liegt
            int tw = std::min((int)(vr.right - vr.left), TextWidth(hdc, f[i].value, m_fDetailValue) + Sx(16));
            RoundBox(hdc, Rc(vr.left - Sx(6), vr.top, vr.left - Sx(6) + tw, vr.bottom), Sx(8), col::tsatCard, col::tsatCard);
        }
        Text(hdc, vr, f[i].value, m_fDetailValue, f[i].warn ? col::warnText : col::text);
        // Jedes bearbeitbare Feld antippen: Liste oder Tastatur
        int act = A_NONE;
        static const struct { const wchar_t* label; int action; } kFieldActions[] = {
            { L"SID", A_FIELD_BASE + F_SID },           { L"RUNWAY", A_FIELD_BASE + F_RWY },
            { L"SQUAWK", A_FIELD_BASE + F_SQUAWK },     { L"INITIAL ALTITUDE", A_FIELD_BASE + F_CFL },
            { L"CFL", A_FIELD_BASE + F_CFL },           { L"FINAL ALTITUDE", A_FIELD_BASE + F_RFL },
            { L"FLIGHT RULES", A_FIELD_BASE + F_RULES },  { L"TYPE / WAKE", A_FIELD_BASE + F_TYPE },
            { L"ALTERNATE", A_FIELD_BASE + F_ALTN }, { L"DESTINATION", A_FIELD_BASE + F_ADES },
            { L"ORIGIN", A_FIELD_BASE + F_ADEP },     { L"STAND", A_FIELD_BASE + F_STAND },
            { L"STAR", A_FIELD_BASE + F_STAR },         { L"RADIO", A_FIELD_BASE + F_COMM },
            { L"HEADING", A_FIELD_BASE + F_HDG },       { L"SPEED", A_FIELD_BASE + F_SPD },
            { L"EOBT", A_FIELD_BASE + F_EOBT },         { L"NEXT FREQUENCY", A_FIELD_BASE + F_FREQ },
            { L"GROUND STATE", A_FIELD_BASE + F_STATE }, { L"HOLD SHORT", A_HS_OPEN },
            { L"INTERSECTION", A_INT_OPEN },            { L"SCRATCHPAD", A_EDIT_OPEN },
            { L"CLEARANCE", A_CLEARANCE },              { L"LANDING CLEARANCE (CTL)", A_RWY_CLEAR },
            { L"TAKEOFF CLEARANCE", A_RWY_CLEAR },     { L"RESPONSIBLE", A_FIELD_BASE + F_RESP },
        };
        for (const auto& fa : kFieldActions)
            if (wcscmp(f[i].label, fa.label) == 0) act = fa.action;
        if (act != A_NONE && (!s.freeStrip || act == A_HS_OPEN))
        {
            Button b;
            b.rc = Rc(fx - Sx(4), fy, fx + colW - Sx(6), fy + fieldH);
            b.action = act;
            m_buttons.push_back(b);
            // feine Linie unter dem Wert: bearbeitbar
            Fill(hdc, Rc(fx, fy + fieldH - Sx(5), fx + std::min(colW - Sx(16), Sx(120)), fy + fieldH - Sx(4)),
                 col::cardBorder);
        }
    }
    y += fieldRows * fieldH + Sx(8);

    // Route und Bemerkungen (mehrzeilig)
    Text(hdc, Rc(x, y, cx + cardW - pad, y + Sx(16)), L"ROUTE", m_fDetailLabel, col::textDim,
         DT_LEFT | DT_TOP | DT_SINGLELINE);
    Text(hdc, Rc(x, y + Sx(18), cx + cardW - pad, y + routeH - Sx(4)), Or(s.route), m_fStripSmall, col::text,
         DT_LEFT | DT_TOP | DT_WORDBREAK | DT_EDITCONTROL);
    if (!s.freeStrip) m_buttons.push_back({ Rc(x - Sx(4), y, cx + cardW - pad, y + routeH), A_FIELD_BASE + F_ROUTE });
    y += routeH;
    Text(hdc, Rc(x, y, cx + cardW - pad, y + Sx(16)), L"REMARKS", m_fDetailLabel, col::textDim,
         DT_LEFT | DT_TOP | DT_SINGLELINE);
    Text(hdc, Rc(x, y + Sx(18), cx + cardW - pad, y + remarksH - Sx(4)), Or(s.remarks), m_fStripSmall, col::text,
         DT_LEFT | DT_TOP | DT_WORDBREAK | DT_EDITCONTROL);
    if (!s.freeStrip) m_buttons.push_back({ Rc(x - Sx(4), y, cx + cardW - pad, y + remarksH), A_FIELD_BASE + F_REMARKS });
    y += remarksH + Sx(10);

    // Ground-State-Buttons
    // nur die States, deren Bay in der aktuellen Ansicht zu sehen ist
    // Buttons fuer alles, was in dieser Ansicht zu sehen ist
    struct Btn { const wchar_t* label; int action; bool active; COLORREF accent; };
    std::vector<Btn> btns;
    if (s.freeStrip) {}   // Notiz-Streifen: keine Status-Buttons, nur verschieben
    else if (s.arrival)
    {
        bool rwy = ShowsArrivalsIn(BAY_RUNWAY);
        if (BayInView(BAY_FINAL) && !s.onGround)
            btns.push_back({ L"Final", A_MOVE_BASE + BAY_FINAL, bay == BAY_FINAL, kBayAccent[BAY_FINAL] });
        if (rwy && (!s.onGround || bay == BAY_RUNWAY))
            btns.push_back({ L"Runway", A_MOVE_BASE + BAY_RUNWAY, bay == BAY_RUNWAY, kBayAccent[BAY_RUNWAY] });
        if (rwy)
            btns.push_back({ L"Landing clearance", A_RWY_CLEAR, s.landingCleared, col::ok });
        if (rwy && !s.onGround)
            btns.push_back({ L"Go-around (G/A)", A_GOAROUND, s.goAround, RGB(214, 60, 40) });
        if (m_board.dlEnabled)
            btns.push_back({ L"CPDLC", A_DL_OPEN, s.dlUnread > 0 || s.dlLogon == 1, RGB(70, 110, 190) });
        // Ground States (Taxi In, Parked) stecken hinter einem Button
        if (BayInView(BAY_TAXIIN) || BayInView(BAY_GNDIN))
        {
            m_stateBtnLabel = L"Ground state: " + std::wstring(s.state >= 0 && s.state < GC_COUNT ? kStateButton[s.state] : L"-") + L" \x25BE";
            btns.push_back({ m_stateBtnLabel.c_str(), A_FIELD_BASE + F_STATE, false, kBayAccent[BAY_TAXIIN] });
        }
    }
    else
    {
        // Ground States stecken hinter einem Button (Liste wie bei den anderen Feldern)
        bool gnd = (m_view == VIEW_GND);
        m_stateBtnLabel = L"Ground state: " + std::wstring(s.state >= 0 && s.state < GC_COUNT ? kStateButton[s.state] : L"-") + L" \x25BE";
        btns.push_back({ m_stateBtnLabel.c_str(), A_FIELD_BASE + F_STATE, false, kBayAccent[BAY_TAXI] });
        if (gnd) btns.push_back({ L"To Tower", A_MOVE_BASE + BAY_HANDOFF, bay == BAY_HANDOFF, kBayAccent[BAY_HANDOFF] });
        else if (m_view == VIEW_APN && !m_board.gndOnline)
            btns.push_back({ L"To Tower", A_MOVE_BASE + BAY_HANDOFF, bay == BAY_HANDOFF, kBayAccent[BAY_HANDOFF] });
        else if (m_view == VIEW_APN)
            btns.push_back({ L"To Ground", A_MOVE_BASE + BAY_TOGND, bay == BAY_TOGND, kBayAccent[BAY_TOGND] });
        else if (BayInView(BAY_RUNWAY))
            btns.push_back({ L"Handed over", A_HOV, s.handedOver, RGB(250, 180, 70) });
    }
    // Hervorheben: kleine Farbknoepfe (keine, gelb, gruen, blau, rot, lila)
    if (!s.freeStrip)
    {
        static const wchar_t* kHlLab[6] = { L"\x25CB", L"Y", L"G", L"B", L"R", L"P" };
        int hw = Sx(34), hx = cx + cardW - pad - Sx(60) - 6 * (hw + Sx(4));   // links vom Schliessen-Knopf
        for (int k = 0; k < 6; ++k)
        {
            RECT hr = Rc(hx + k * (hw + Sx(4)), cy + pad + Sx(2), hx + k * (hw + Sx(4)) + hw, cy + pad + Sx(32));
            DrawButton(hdc, hr, kHlLab[k], k ? kHlStrong[k] : col::button, k ? RGB(255, 255, 255) : col::text, A_HL_BASE + k, s.hl == k);
        }
    }
    int gap = Sx(6);
    int ns = (int)btns.size();
    int bw = (cardW - 2 * pad - (ns - 1) * gap) / std::max(1, ns);
    for (int k = 0; k < ns; ++k)
    {
        RECT r = Rc(x + k * (bw + gap), y, x + k * (bw + gap) + bw, y + btnH);
        DrawButton(hdc, r, btns[k].label, btns[k].active ? btns[k].accent : col::button,
                   btns[k].active ? RGB(12, 14, 18) : col::text, btns[k].action, btns[k].active);
    }
    y += btnH + Sx(10);
    btns.clear();
    // naechste Stelle als zustaendig (ein Tipp)
    if (!s.freeStrip && s.offerForMe)
    {
        btns.push_back({ L"Accept handover", A_NEXTRESP, true, RGB(250, 170, 50) });
        btns.push_back({ L"Reject", A_REJECT, false, RGB(215, 50, 45) });
    }
    else if (!s.freeStrip && !s.offerTo.empty())
    {
        m_nextRespLabel = L"Cancel \x2192 " + RespLabel(s.offerTo);
        btns.push_back({ m_nextRespLabel.c_str(), A_NEXTRESP, true, RGB(230, 140, 30) });
    }
    else if (!s.freeStrip && !s.nextResp.empty())
    {
        m_nextRespLabel = L"Hand over \x2192 " + RespLabel(s.nextResp);
        btns.push_back({ m_nextRespLabel.c_str(), A_NEXTRESP, false, RGB(70, 110, 190) });
    }
    // Richtung und Diversion von Hand
    if (!s.freeStrip)
    {
        btns.push_back({ s.arrival ? L"Make outbound" : L"Make inbound", A_FLIPDIR, false, col::button });
        btns.push_back({ s.diverted ? L"Cancel diversion" : L"Diversion", A_DIVERT, s.diverted, col::button });
    }
    // Streifen einer anderen Stelle: uebernehmen
    if (!s.freeStrip && m_board.opt.respOn && !s.respMine && !s.offerForMe && !s.resp.empty())
        btns.push_back({ L"Take over", A_REQ, true, RGB(250, 180, 70) });
    ns = (int)btns.size();
    bw = (cardW - 2 * pad - (std::max(ns, 4) - 1) * gap) / std::max(ns, 4);
    for (int k = 0; k < ns; ++k)
    {
        RECT r = Rc(x + k * (bw + gap), y, x + k * (bw + gap) + bw, y + btnH);
        DrawButton(hdc, r, btns[k].label, btns[k].active ? btns[k].accent : col::button,
                   btns[k].active ? RGB(12, 14, 18) : col::text, btns[k].action, btns[k].active);
    }
    if (!s.freeStrip) y += btnH + Sx(10);

    // Aktionen (Clearance nur bei Abfluegen)
    const COLORREF white = RGB(255, 255, 255);
    int na = s.freeStrip ? 4 : s.arrival ? 5 : 6;
    int aw = (cardW - 2 * pad - (na - 1) * gap) / na;
    int ax = x;
    if (!s.arrival && !s.freeStrip)
    {
        DrawButton(hdc, Rc(ax, y, ax + aw, y + btnH), s.clearance ? L"Revoke CLR" : L"Clearance",
                   s.clearance ? col::button : col::ok, s.clearance ? col::text : white, A_CLEARANCE, false);
        ax += aw + gap;
    }
    DrawButton(hdc, Rc(ax, y, ax + aw, y + btnH), L"Hold Short", col::warn, white, A_HS_OPEN, false);
    ax += aw + gap;
    DrawButton(hdc, Rc(ax, y, ax + aw, y + btnH), L"Scratchpad", RGB(54, 104, 168), white, A_EDIT_OPEN, false);
    ax += aw + gap;
    if (!s.freeStrip)
    {
        // Verlauf: wer hat wann was gesetzt
        DrawButton(hdc, Rc(ax, y, ax + aw, y + btnH), L"History", col::button, col::text, A_FIELD_BASE + F_HISTORY, false);
        ax += aw + gap;
    }
    DrawButton(hdc, Rc(ax, y, ax + aw, y + btnH), L"Remove", col::danger, white, A_REMOVE, false);
    ax += aw + gap;
    DrawButton(hdc, Rc(ax, y, ax + aw, y + btnH), L"Close", col::button, col::text, A_CLOSE_DETAIL,
               false);

    RestoreDC(hdc, saved);
}

// Aktueller Wert eines Feldes (Vorbelegung fuer die Tastatur)
static std::string FieldValue(const StripData& s, int f, int ta)
{
    char buf[32];
    switch (f)
    {
    case F_SID:     return s.sid;
    case F_RWY:     return s.arrival ? s.arrRwy : s.rwy;
    case F_SQUAWK:  return s.squawkAssigned;
    case F_CFL:
        if (s.clearedApproach == 1) return "ILS";
        if (s.clearedApproach == 2) return "VIS";
        if (s.clearedAlt <= 0) return "";
        if (s.clearedAlt > ta) { snprintf(buf, sizeof(buf), "FL%03d", (s.clearedAlt + 50) / 100); return buf; }
        return std::to_string(s.clearedAlt);
    case F_RFL:     if (s.finalAlt <= 0) return ""; snprintf(buf, sizeof(buf), "FL%03d", (s.finalAlt + 50) / 100); return buf;
    case F_RULES:   return s.rules;
    case F_TYPE:    return s.acType;
    case F_ALTN:    return s.altn;
    case F_ADES:    return s.ades;
    case F_ADEP:    return s.adep;
    case F_STAND:   return s.stand;
    case F_STAR:    return s.star;
    case F_COMM:    return s.comm;
    case F_HDG:     return s.assignedHeading > 0 ? std::to_string(s.assignedHeading) : "";
    case F_SPD:     return s.assignedSpeed > 0 ? std::to_string(s.assignedSpeed) : "";
    case F_EOBT:    return s.eobt;
    case F_ROUTE:   return s.route;
    case F_REMARKS: return s.remarks;
    case F_FREQ:    return s.nextFreq;
    case F_STATE:   return s.groundStateRaw;
    case F_RESP:    return s.resp.empty() ? std::string("NONE") : s.resp;
    case F_HS:      return s.holdShort;
    default:        return "";
    }
}

static const wchar_t* FieldTitle(int f)
{
    static const wchar_t* t[F_COUNT] = { L"SID", L"RUNWAY", L"SQUAWK", L"CFL / INITIAL ALTITUDE", L"FINAL ALTITUDE",
                                         L"FLIGHT RULES", L"TYPE", L"ALTERNATE", L"DESTINATION", L"ORIGIN", L"STAND",
                                         L"STAR", L"RADIO", L"HEADING", L"SPEED", L"EOBT", L"ROUTE", L"REMARKS",
                                         L"NEXT FREQUENCY", L"GROUND STATE", L"HOLD SHORT", L"RESPONSIBLE", L"HISTORY" };
    return (f >= 0 && f < F_COUNT) ? t[f] : L"";
}

// Feld antippen: gibt es eine Liste, die Auswahl zeigen, sonst die Tastatur
void StripWindow::OpenField(int field)
{
    if (!m_detailCs.empty()) OpenFieldFor(m_detailCs, field);
}

void StripWindow::OpenFieldFor(const std::string& cs, int field)
{
    if (cs.empty() || !m_host) return;
    // Squawk und Hold Short: Tastenfeld mit nur den moeglichen Tasten
    if (field == F_SQUAWK) { OpenKeypad(KP_SQUAWK, cs, m_downPt); return; }
    if (field == F_HDG) { OpenKeypad(KP_HDG, cs, m_downPt); return; }
    if (field == F_HS) { OpenKeypad(KP_HS, cs, m_downPt); return; }
    std::vector<std::string> opts = m_host->FieldOptions(cs, field);
    m_pickCs = cs;
    if (opts.empty())
    {
        OpenFieldEditor(field);
        return;
    }
    m_pickOpen = true;
    m_pickField = field;
    m_pickOpts = opts;
    m_pickScroll = -1;
    m_standMode = (field == F_STAND);
    m_pickAnchor = m_downPt;
    m_pickPage = 0;
    m_pickTab = -1;
    m_pickTabs.clear();
    if (opts.size() > 40)
    {
        // lange Listen (Stands): Reiter nach Anfangsbuchstaben
        std::set<std::string> t;
        for (const std::string& o : opts)
            if (!o.empty()) t.insert(o.substr(0, 1));
        if (t.size() > 1 && t.size() <= 30) m_pickTabs.assign(t.begin(), t.end());
    }
    if (m_standMode)
    {
        m_pickTabs.clear();
        m_pickStatus = m_host->StandStatus(cs);
        BuildStandGroups();
    }
    Invalidate();
}

// Stands gruppieren: "Apron 1" und "GAT" als Direkt-Buttons, "Apron 2" (nur Zahl),
// A, B, C, D, E (Buchstabe + Nummer, gezeigt wird die Nummer), W und Rest ("Other")
void StripWindow::BuildStandGroups()
{
    m_standGroups.clear();
    StandGroup ap1, gat, ap2, w, other;
    ap1.label = L"Apron 1"; ap1.button = true;
    gat.label = L"GAT"; gat.button = true;
    ap2.label = L"Apron 2"; w.label = L"W"; other.label = L"Other";
    StandGroup letters[5];
    for (int k = 0; k < 5; ++k) letters[k].label = std::wstring(1, (wchar_t)(L'A' + k));
    auto digitsFrom = [](const std::string& n, size_t i) {
        size_t j = i;
        while (j < n.size() && isdigit((unsigned char)n[j])) ++j;
        return j > i && (j == n.size() || (j + 1 == n.size() && isalpha((unsigned char)n[j])));
    };
    for (int i = 0; i < (int)m_pickOpts.size(); ++i)
    {
        std::string n = m_pickOpts[i], c;
        for (char ch : n) if (ch != ' ' && ch != '_' && ch != '-') c += (char)toupper((unsigned char)ch);
        if (c.compare(0, 6, "APRON1") == 0 || c.compare(0, 4, "APN1") == 0 || c == "AP1")
            { ap1.idx.push_back(i); ap1.shown.push_back(W(n)); }
        else if (c.compare(0, 3, "GAT") == 0 || (c.size() > 1 && c[0] == 'G' && digitsFrom(c, 1)))
            { gat.idx.push_back(i); gat.shown.push_back(W(n)); }
        else if (digitsFrom(c, 0))
            { ap2.idx.push_back(i); ap2.shown.push_back(W(n)); }
        else if (c.size() > 1 && c[0] >= 'A' && c[0] <= 'E' && digitsFrom(c, 1))
            { letters[c[0] - 'A'].idx.push_back(i); letters[c[0] - 'A'].shown.push_back(W(n.substr(1))); }
        else if (c.size() > 1 && c[0] == 'W' && digitsFrom(c, 1))
            { w.idx.push_back(i); w.shown.push_back(W(n.substr(1))); }
        else
            { other.idx.push_back(i); other.shown.push_back(W(n)); }
    }
    // "Apron 1" und "GAT" sind immer Direkt-Buttons (naechster freier Stand der Gruppe bzw. die
    // Flaeche selbst). Mehrere Stands dahinter: zusaetzlich eine Kategorie fuer die Einzelwahl.
    ap1.literal = "APRON1";
    gat.literal = "GAT";
    bool berlin = m_board.profile.empty() || m_board.profile == "EDDB";
    if (berlin || !ap1.idx.empty()) m_standGroups.push_back(ap1);
    if (berlin || !gat.idx.empty()) m_standGroups.push_back(gat);
    StandGroup ap1Cat = ap1, gatCat = gat;
    ap1Cat.button = gatCat.button = false;
    if (ap1Cat.idx.size() > 1) m_standGroups.push_back(ap1Cat);
    // ausserhalb Berlins kein "Apron 2"-Ordner: Zahl-Stands und Rest zusammen als "Stands"
    if (!berlin)
    {
        ap2.label = L"Stands";
        for (size_t k = 0; k < other.idx.size(); ++k) { ap2.idx.push_back(other.idx[k]); ap2.shown.push_back(other.shown[k]); }
        other.idx.clear(); other.shown.clear();
    }
    if (!ap2.idx.empty()) m_standGroups.push_back(ap2);
    for (StandGroup& g : letters) if (!g.idx.empty()) m_standGroups.push_back(g);
    for (StandGroup* g : { &w, &gatCat, &other })
        if (!g->idx.empty() && (g != &gatCat || g->idx.size() > 1)) m_standGroups.push_back(*g);
    // Kategorie des aktuellen Stands, sonst die erste
    const StripData* sd = FindStrip(m_pickCs);
    std::string cur = sd ? sd->stand : std::string();
    m_pickTab = -1;
    for (int g = 0; g < (int)m_standGroups.size(); ++g)
    {
        if (m_standGroups[g].button) continue;
        if (m_pickTab < 0) m_pickTab = g;
        for (int i : m_standGroups[g].idx) if (!cur.empty() && m_pickOpts[i] == cur) m_pickTab = g;
    }
}

// Direkt-Button: gibt es einen Stand mit genau dem Namen (Flaeche "GAT"), diesen; sonst den ersten
// freien Stand der Gruppe (laut GRP-Lage), sonst den ersten; ohne Stands den Namen selbst
std::string StripWindow::StandButtonValue(const StandGroup& g) const
{
    auto norm = [](const std::string& n) {
        std::string c;
        for (char ch : n) if (ch != ' ' && ch != '_' && ch != '-') c += (char)toupper((unsigned char)ch);
        return c;
    };
    for (int i : g.idx) if (norm(m_pickOpts[i]) == g.literal) return m_pickOpts[i];
    for (int i : g.idx)
    {
        auto st = m_pickStatus.find(m_pickOpts[i]);
        if (st == m_pickStatus.end() || st->second == 0) return m_pickOpts[i];
    }
    if (!g.idx.empty()) return m_pickOpts[g.idx[0]];
    return g.literal;
}

void StripWindow::OpenFieldEditor(int field)
{
    std::string cs = m_pickCs.empty() ? m_detailCs : m_pickCs;
    const StripData* s = FindStrip(cs);
    if (!s) return;
    std::string cur = FieldValue(*s, field, m_board.transitionAltitude);
    OpenEditor(cs);
    if (!m_editing) return;
    m_editMode = 4;
    m_editField = field;
    m_editText = cur;
    if (field == F_ROUTE)
    {
        // Route als einzelne Punkte; bearbeitet wird der markierte, neu angehaengt wird am Ende
        m_rtOrig = cur;
        m_rtTok.clear();
        std::string t;
        for (char c : cur + " ")
        {
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { if (!t.empty()) m_rtTok.push_back(t); t.clear(); }
            else t += (char)toupper((unsigned char)c);
        }
        m_rtTok.push_back(std::string());   // leerer Punkt am Ende: Tippen haengt an
        m_rtSel = (int)m_rtTok.size() - 1;
        m_editText.clear();
    }
    Invalidate();
}

bool StripWindow::RouteEditing() const
{
    return m_editing && m_editMode == 4 && m_editField == F_ROUTE;
}

std::string StripWindow::RouteJoined() const
{
    std::string r;
    for (const std::string& t : m_rtTok) if (!t.empty()) r += (r.empty() ? "" : " ") + t;
    return r;
}

// Anderen Punkt markieren: leere Punkte (ausser dem markierten am Ende) fallen weg
void StripWindow::RouteSelect(int i)
{
    if (m_rtTok.empty()) m_rtTok.push_back(std::string());
    i = std::max(0, std::min(i, (int)m_rtTok.size() - 1));
    int kept = -1;
    std::vector<std::string> v;
    for (int k = 0; k < (int)m_rtTok.size(); ++k)
    {
        if (k == i) { kept = (int)v.size(); v.push_back(m_rtTok[k]); }
        else if (!m_rtTok[k].empty()) v.push_back(m_rtTok[k]);
    }
    m_rtTok.swap(v);
    m_rtSel = kept < 0 ? (int)m_rtTok.size() - 1 : kept;
    m_editText = m_rtTok[m_rtSel];
}

void StripWindow::RouteAction(int action)
{
    if (!RouteEditing()) return;
    if (m_rtTok.empty()) { m_rtTok.push_back(std::string()); m_rtSel = 0; }
    if (action >= A_RT_TOK_BASE) RouteSelect(action - A_RT_TOK_BASE);
    else switch (action)
    {
    case A_RT_LEFT:  RouteSelect(m_rtSel - 1); break;
    case A_RT_RIGHT:
        // hinter dem letzten Punkt: neuer leerer Punkt zum Anhaengen
        if (m_rtSel == (int)m_rtTok.size() - 1 && !m_rtTok.back().empty())
        {
            m_rtTok.push_back(std::string());
            m_rtSel = (int)m_rtTok.size() - 1;
            m_editText.clear();
        }
        else RouteSelect(m_rtSel + 1);
        break;
    case A_RT_INS:
        if (!m_rtTok[m_rtSel].empty())
        {
            m_rtTok.insert(m_rtTok.begin() + m_rtSel, std::string());
            m_editText.clear();
        }
        break;
    case A_RT_DEL:
        m_rtTok.erase(m_rtTok.begin() + m_rtSel);
        if (m_rtTok.empty()) m_rtTok.push_back(std::string());
        m_rtSel = std::min(m_rtSel, (int)m_rtTok.size() - 1);
        m_editText = m_rtTok[m_rtSel];
        break;
    case A_RT_DCT:
        // DCT nach dem markierten Punkt (bzw. in den leeren), danach weiter mit einem neuen Punkt
        if (m_rtTok[m_rtSel].empty() && m_rtSel > 0 && m_rtTok[m_rtSel - 1] == "DCT") break;   // steht schon da
        if (m_rtTok[m_rtSel].empty()) m_rtTok[m_rtSel] = "DCT";
        else m_rtTok.insert(m_rtTok.begin() + (++m_rtSel), "DCT");
        m_rtTok.insert(m_rtTok.begin() + (++m_rtSel), std::string());
        m_editText.clear();
        break;
    case A_RT_CUT:
        // ab dem markierten Punkt alles weg (z. B. vor einer neuen Fortsetzung)
        m_rtTok.erase(m_rtTok.begin() + m_rtSel, m_rtTok.end());
        m_rtTok.push_back(std::string());
        m_rtSel = (int)m_rtTok.size() - 1;
        m_editText.clear();
        break;
    case A_RT_RESET:
    {
        m_rtTok.clear();
        std::string t;
        for (char c : m_rtOrig + " ")
        {
            if (c == ' ') { if (!t.empty()) m_rtTok.push_back(t); t.clear(); }
            else t += (char)toupper((unsigned char)c);
        }
        m_rtTok.push_back(std::string());
        m_rtSel = (int)m_rtTok.size() - 1;
        m_editText.clear();
        break;
    }
    default: break;
    }
    Invalidate();
}

std::vector<int> StripWindow::PickVisible() const
{
    std::vector<int> v;
    if (m_standMode)
    {
        if (m_pickTab >= 0 && m_pickTab < (int)m_standGroups.size()) v = m_standGroups[m_pickTab].idx;
        return v;
    }
    for (int i = 0; i < (int)m_pickOpts.size(); ++i)
    {
        if (m_pickTab >= 0 && m_pickTab < (int)m_pickTabs.size() &&
            m_pickOpts[i].compare(0, m_pickTabs[m_pickTab].size(), m_pickTabs[m_pickTab]) != 0)
            continue;
        v.push_back(i);
    }
    return v;
}

void StripWindow::DrawPicker(HDC hdc, int width, int height)
{
    const StripData* s = FindStrip(m_pickCs);
    std::string cur = s ? FieldValue(*s, m_pickField, m_board.transitionAltitude) : std::string();

    if (m_standMode) { DrawStandPicker(hdc, width, height, cur); return; }
    // Kurze Listen (Hoehen, Pisten, SIDs ...): kleine Felder uebereinander, direkt neben dem Finger.
    // Lange Listen (Stands) mit Reitern: grosses Fenster.
    if (PickScrollList())
    {
        // Eine Spalte: Werte untereinander, wischen / Mausrad / Pfeile zum Blaettern
        int n = (int)m_pickOpts.size();
        int rowH = Sx(38), gap = Sx(2), pad = Sx(8), titleH = Sx(32), arrowH = Sx(30), btnH = Sx(44);
        int cardW = Sx(230);
        int maxRows = std::max(3, (height - Sx(30) - titleH - 2 * arrowH - btnH - 4 * pad) / (rowH + gap));
        int rows = std::max(1, std::min({ n, 9, maxRows }));
        m_pickRowsVis = rows;
        m_pickRowPitch = rowH + gap;
        if (m_pickScroll < 0)
        {
            // aktueller Wert in die Mitte, sonst 5000 ft bzw. FL100 als Startpunkt
            int at = -1;
            for (int i = 0; i < n; ++i) if (!cur.empty() && Split1(m_pickOpts[i]) == cur) at = i;
            if (at < 0)
                for (int i = 0; i < n; ++i) if (m_pickOpts[i] == "5000" || m_pickOpts[i] == "FL100") { at = i; break; }
            m_pickScroll = std::max(0, at - rows / 2);
        }
        ClampPickScroll();
        int cardH = titleH + pad + arrowH + gap + rows * (rowH + gap) + arrowH + pad + btnH + pad;
        int x = std::max(Sx(6), std::min((int)m_pickAnchor.x - cardW / 2, width - cardW - Sx(6)));
        int y = m_pickAnchor.y + Sx(24);
        if (y + cardH > height - Sx(6)) y = m_pickAnchor.y - Sx(24) - cardH;
        y = std::max(Sx(6), std::min(y, height - cardH - Sx(6)));
        m_pickCard = Rc(x, y, x + cardW, y + cardH);
        Fill(hdc, Rc(x + Sx(3), y + Sx(3), x + cardW + Sx(3), y + cardH + Sx(3)), col::shadow);
        RoundBox(hdc, m_pickCard, 0, col::card, col::cardBorder);
        Fill(hdc, Rc(x + 1, y + 1, x + cardW - 1, y + titleH), col::bayHeader);
        Text(hdc, Rc(x + pad, y, x + cardW - pad, y + titleH), std::wstring(FieldTitle(m_pickField)) + L"  " + W(m_pickCs),
             m_fHeader, col::text);
        m_pickPage = 0;
        m_pickPerPage = n;
        int ly = y + titleH + pad;
        bool canUp = m_pickScroll > 0, canDown = m_pickScroll + rows < n;
        DrawButton(hdc, Rc(x + pad, ly, x + cardW - pad, ly + arrowH), L"\x25B2", canUp ? col::buttonAlt : col::bay,
                   canUp ? col::text : col::textDim, canUp ? A_PICK_UP : A_NONE, false);
        ly += arrowH + gap;
        m_pickListRc = Rc(x + pad, ly, x + cardW - pad, ly + rows * (rowH + gap));
        for (int k = 0; k < rows && m_pickScroll + k < n; ++k)
        {
            int i = m_pickScroll + k;
            const std::string& o = m_pickOpts[i];
            bool isCur = !cur.empty() && Split1(o) == cur;
            int cy2 = ly + k * (rowH + gap);
            DrawButton(hdc, Rc(x + pad, cy2, x + cardW - pad, cy2 + rowH), W(o), isCur ? col::ok : col::input,
                       isCur ? RGB(255, 255, 255) : col::text, A_PICK_BASE + i, false);
        }
        // Lage in der Liste (schmaler Balken rechts)
        if (n > rows)
        {
            int th = m_pickListRc.bottom - m_pickListRc.top;
            int bh = std::max(Sx(12), th * rows / n);
            int by0 = m_pickListRc.top + (th - bh) * m_pickScroll / std::max(1, n - rows);
            Fill(hdc, Rc(x + cardW - Sx(6), by0, x + cardW - Sx(3), by0 + bh), col::textDim);
        }
        ly = m_pickListRc.bottom;
        DrawButton(hdc, Rc(x + pad, ly, x + cardW - pad, ly + arrowH), L"\x25BC", canDown ? col::buttonAlt : col::bay,
                   canDown ? col::text : col::textDim, canDown ? A_PICK_DOWN : A_NONE, false);
        int by = y + cardH - pad - btnH, bw = (cardW - 2 * pad - gap) / 2;
        DrawButton(hdc, Rc(x + pad, by, x + pad + bw, by + btnH), L"Clear", col::buttonAlt, col::text, A_PICK_CLEAR, false);
        DrawButton(hdc, Rc(x + pad + bw + gap, by, x + cardW - pad, by + btnH), L"C", col::danger, RGB(255, 255, 255),
                   A_PICK_CLOSE, false);
        return;
    }
    if (m_pickTabs.empty() && m_pickOpts.size() <= 80)
    {
        int rowH = Sx(34), gap = Sx(2), pad = Sx(8), titleH = Sx(32), cellW = Sx(104), btnH = Sx(44);
        int n = (int)m_pickOpts.size();
        int maxRows = std::max(4, (height - Sx(30) - titleH - btnH - 2 * pad) / (rowH + gap));
        int rows = std::max(1, std::min(n, maxRows));
        int cols = std::max(1, (n + rows - 1) / rows);
        rows = (n + cols - 1) / cols;
        int cardW = std::max(Sx(330), 2 * pad + cols * cellW + (cols - 1) * gap);
        cellW = (cardW - 2 * pad - (cols - 1) * gap) / cols;
        int cardH = titleH + pad + rows * (rowH + gap) + pad + btnH + pad;
        int x = std::max(Sx(6), std::min((int)m_pickAnchor.x - cardW / 2, width - cardW - Sx(6)));
        int y = m_pickAnchor.y + Sx(24);
        if (y + cardH > height - Sx(6)) y = m_pickAnchor.y - Sx(24) - cardH;
        y = std::max(Sx(6), std::min(y, height - cardH - Sx(6)));
        m_pickCard = Rc(x, y, x + cardW, y + cardH);
        Fill(hdc, Rc(x + Sx(3), y + Sx(3), x + cardW + Sx(3), y + cardH + Sx(3)), col::shadow);
        RoundBox(hdc, m_pickCard, 0, col::card, col::cardBorder);
        Fill(hdc, Rc(x + 1, y + 1, x + cardW - 1, y + titleH), col::bayHeader);
        Text(hdc, Rc(x + pad, y, x + cardW - pad, y + titleH), std::wstring(FieldTitle(m_pickField)) + L"  " + W(m_pickCs),
             m_fHeader, col::text);
        m_pickPage = 0;
        m_pickPerPage = n;
        std::vector<int> vis = PickVisible();
        for (int k = 0; k < (int)vis.size(); ++k)
        {
            int c = k / rows, r = k % rows;   // spaltenweise: Werte laufen von oben nach unten
            int cx = x + pad + c * (cellW + gap), cy = y + titleH + pad + r * (rowH + gap);
            const std::string& o = m_pickOpts[vis[k]];
            bool isCur = !cur.empty() && Split1(o) == cur;
            DrawButton(hdc, Rc(cx, cy, cx + cellW, cy + rowH), W(o), isCur ? col::ok : col::input,
                       isCur ? RGB(255, 255, 255) : col::text, A_PICK_BASE + k, false);
        }
        // Pisten, SIDs, STARs: nur aus der festen Liste (keine freie Eingabe), Piste nicht leeren
        const bool hist = m_pickField == F_HISTORY;   // Verlauf: nur lesen
        bool fixed = (m_pickField == F_RWY || m_pickField == F_SID || m_pickField == F_STAR || hist);
        bool canClear = (m_pickField != F_RWY && !hist);
        std::vector<std::pair<const wchar_t*, int>> bb;
        if (!fixed) bb.push_back({ L"Type", A_PICK_FREE });
        if (canClear) bb.push_back({ m_pickField == F_SID || m_pickField == F_STAR ? L"Auto" : L"Clear", A_PICK_CLEAR });
        bb.push_back({ hist ? L"Close" : L"C", A_PICK_CLOSE });
        int by = y + cardH - pad - btnH, nb = (int)bb.size();
        int bw = (cardW - 2 * pad - (nb - 1) * gap) / nb;
        for (int i = 0; i < nb; ++i)
        {
            int bx = x + pad + i * (bw + gap);
            int br = (i == nb - 1) ? x + cardW - pad : bx + bw;
            bool close = bb[i].second == A_PICK_CLOSE;
            DrawButton(hdc, Rc(bx, by, br, by + btnH), bb[i].first, close ? col::danger : col::buttonAlt,
                       close ? RGB(255, 255, 255) : col::text, bb[i].second, false);
        }
        return;
    }
    Dim(hdc, width, height, (BYTE)(col::dimAlpha * 4 / 5));

    int pad = Sx(18), gap = Sx(6), btnH = Sx(52);
    int cardW = std::min(width - Sx(24), Sx(980));
    int cardH = std::min(height - Sx(24), Sx(720));
    int cx = (width - cardW) / 2, cy = std::max(Sx(12), (height - cardH) / 2);
    m_pickCard = Rc(cx, cy, cx + cardW, cy + cardH);
    RoundBox(hdc, m_pickCard, Sx(14), col::card, col::cardBorder);

    int x = cx + pad, y = cy + pad, right = cx + cardW - pad;
    Text(hdc, Rc(x, y, right, y + Sx(36)), std::wstring(FieldTitle(m_pickField)) + L"   " + W(m_pickCs), m_fTitle, col::text);
    Text(hdc, Rc(x, y, right, y + Sx(36)), L"current: " + (cur.empty() ? std::wstring(L"-") : W(cur)), m_fHeader,
         col::textDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    y += Sx(44);

    // Reiter (Anfangsbuchstaben), nochmal antippen zeigt wieder alle
    if (!m_pickTabs.empty())
    {
        int n = (int)m_pickTabs.size();
        int tw = std::max(Sx(36), (right - x - (n - 1) * Sx(4)) / n);
        int tx = x;
        for (int i = 0; i < n; ++i)
        {
            RECT tr = Rc(tx, y, std::min(tx + tw, right), y + Sx(40));
            bool on = (i == m_pickTab);
            DrawButton(hdc, tr, W(m_pickTabs[i]), on ? kBayAccent[BAY_TAXI] : col::button, on ? col::onAccent : col::text,
                       A_PICK_TAB_BASE + i, on);
            tx += tw + Sx(4);
        }
        y += Sx(48);
    }

    // Raster der Werte
    int bottom = cy + cardH - pad - btnH - Sx(10);
    int cols = std::max(2, (right - x + gap) / (Sx(130) + gap));
    int cellW = (right - x - (cols - 1) * gap) / cols;
    int rows = std::max(1, (bottom - y + gap) / (btnH + gap));
    m_pickPerPage = cols * rows;
    std::vector<int> vis = PickVisible();
    int pages = std::max(1, ((int)vis.size() + m_pickPerPage - 1) / m_pickPerPage);
    if (m_pickPage >= pages) m_pickPage = pages - 1;
    for (int k = 0; k < m_pickPerPage; ++k)
    {
        int i = m_pickPage * m_pickPerPage + k;
        if (i >= (int)vis.size()) break;
        const std::string& o = m_pickOpts[vis[i]];
        int c = k % cols, r = k / cols;
        RECT br = Rc(x + c * (cellW + gap), y + r * (btnH + gap), x + c * (cellW + gap) + cellW, y + r * (btnH + gap) + btnH);
        bool isCur = !cur.empty() && Split1(o) == cur;
        DrawButton(hdc, br, W(o), isCur ? col::ok : col::button, isCur ? RGB(255, 255, 255) : col::text, A_PICK_BASE + k,
                   isCur);
    }

    // unten: Seiten, freie Eingabe, leeren, abbrechen
    int by = cy + cardH - pad - btnH;
    int bw = (right - x - 4 * gap) / 5;
    int bx = x;
    DrawButton(hdc, Rc(bx, by, bx + bw, by + btnH), L"Back", m_pickPage > 0 ? col::button : col::bay,
               m_pickPage > 0 ? col::text : col::textDim, A_PICK_PREV, false);
    bx += bw + gap;
    wchar_t pg[32];
    swprintf(pg, 32, L"Next  (%d/%d)", m_pickPage + 1, pages);
    DrawButton(hdc, Rc(bx, by, bx + bw, by + btnH), pg, m_pickPage + 1 < pages ? col::button : col::bay,
               m_pickPage + 1 < pages ? col::text : col::textDim, m_pickPage + 1 < pages ? A_PICK_NEXT : A_NONE, false);
    bx += bw + gap;
    DrawButton(hdc, Rc(bx, by, bx + bw, by + btnH), L"Type", RGB(54, 104, 168), RGB(255, 255, 255), A_PICK_FREE, false);
    bx += bw + gap;
    DrawButton(hdc, Rc(bx, by, bx + bw, by + btnH), L"Clear", col::buttonAlt, col::text, A_PICK_CLEAR, false);
    bx += bw + gap;
    DrawButton(hdc, Rc(bx, by, right, by + btnH), L"Cancel", col::danger, RGB(255, 255, 255), A_PICK_CLOSE, false);
}

// Standauswahl: oben Auto und die Direkt-Buttons, darunter die Kategorien, dann die Stands.
// Stands, die im GRP besetzt, vergeben oder gesperrt sind, erscheinen blass.
void StripWindow::DrawStandPicker(HDC hdc, int width, int height, const std::string& cur)
{
    Dim(hdc, width, height, (BYTE)(col::dimAlpha * 4 / 5));
    int pad = Sx(18), gap = Sx(6), btnH = Sx(52), rowH = Sx(46);
    int cardW = std::min(width - Sx(24), Sx(980));
    int cardH = std::min(height - Sx(24), Sx(720));
    int cx = (width - cardW) / 2, cy = std::max(Sx(12), (height - cardH) / 2);
    m_pickCard = Rc(cx, cy, cx + cardW, cy + cardH);
    RoundBox(hdc, m_pickCard, Sx(4), col::card, col::cardBorder);
    int x = cx + pad, y = cy + pad, right = cx + cardW - pad;
    Text(hdc, Rc(x, y, right, y + Sx(36)), std::wstring(L"STAND   ") + W(m_pickCs), m_fTitle, col::text);
    Text(hdc, Rc(x, y, right, y + Sx(36)), L"current: " + (cur.empty() ? std::wstring(L"-") : W(cur)), m_fHeader,
         col::textDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    y += Sx(44);

    // Reihe 1: Auto (GRP) und Direkt-Buttons
    {
        std::vector<std::pair<std::wstring, int>> top;
        top.push_back({ L"Auto (GRP)", A_PICK_AUTO });
        int k = 0;
        for (const StandGroup& g : m_standGroups)
            if (g.button && k < 2) { top.push_back({ g.label, A_PICK_BTN0 + k }); ++k; }
        int n = (int)top.size();
        int bw = std::min(Sx(220), (right - x - (n - 1) * gap) / n);
        for (int i = 0; i < n; ++i)
        {
            RECT r = Rc(x + i * (bw + gap), y, x + i * (bw + gap) + bw, y + rowH);
            bool isAuto = top[i].second == A_PICK_AUTO;
            bool isCur = false;
            if (!isAuto)
                for (const StandGroup& g : m_standGroups)
                    if (g.button && g.label == top[i].first && !cur.empty())
                    {
                        if (cur == g.literal) isCur = true;
                        for (int gi : g.idx) if (m_pickOpts[gi] == cur) isCur = true;
                    }
            DrawButton(hdc, r, top[i].first, isAuto ? RGB(70, 110, 190) : isCur ? col::ok : col::buttonAlt,
                       (isAuto || isCur) ? RGB(255, 255, 255) : col::text, top[i].second, false);
        }
        y += rowH + Sx(10);
    }
    // Reihe 2: Kategorien
    {
        std::vector<int> cats;
        for (int g = 0; g < (int)m_standGroups.size(); ++g) if (!m_standGroups[g].button) cats.push_back(g);
        if (cats.size() <= 1 && !(m_board.profile.empty() || m_board.profile == "EDDB")) cats.clear();   // nur ein Ordner: keine Reiter
        int n = std::max(1, (int)cats.size());
        int tw = std::min(Sx(140), (right - x - (n - 1) * Sx(4)) / n);
        for (int k = 0; k < (int)cats.size(); ++k)
        {
            int g = cats[k];
            RECT tr = Rc(x + k * (tw + Sx(4)), y, x + k * (tw + Sx(4)) + tw, y + Sx(42));
            bool on = (g == m_pickTab);
            DrawButton(hdc, tr, m_standGroups[g].label, on ? kBayAccent[BAY_TAXI] : col::button, on ? col::onAccent : col::text,
                       A_PICK_TAB_BASE + g, on);
        }
        if (!cats.empty()) y += Sx(50);
    }
    // Stands der Kategorie
    int bottom = cy + cardH - pad - btnH - Sx(10);
    int cols = std::max(4, (right - x + gap) / (Sx(96) + gap));
    int cellW = (right - x - (cols - 1) * gap) / cols;
    int rows = std::max(1, (bottom - y + gap) / (rowH + gap));
    m_pickPerPage = cols * rows;
    std::vector<int> vis = PickVisible();
    int pages = std::max(1, ((int)vis.size() + m_pickPerPage - 1) / m_pickPerPage);
    if (m_pickPage >= pages) m_pickPage = pages - 1;
    const StandGroup* grp = (m_pickTab >= 0 && m_pickTab < (int)m_standGroups.size()) ? &m_standGroups[m_pickTab] : nullptr;
    for (int k = 0; k < m_pickPerPage; ++k)
    {
        int i = m_pickPage * m_pickPerPage + k;
        if (i >= (int)vis.size()) break;
        const std::string& o = m_pickOpts[vis[i]];
        std::wstring lab = grp && i < (int)grp->shown.size() ? grp->shown[i] : W(o);
        int c = k % cols, r = k / cols;
        RECT br = Rc(x + c * (cellW + gap), y + r * (rowH + gap), x + c * (cellW + gap) + cellW, y + r * (rowH + gap) + rowH);
        bool isCur = !cur.empty() && o == cur;
        auto st = m_pickStatus.find(o);
        bool pale = st != m_pickStatus.end() && st->second > 0;
        COLORREF bg = isCur ? col::ok : pale ? col::bay : col::button;
        COLORREF fg = isCur ? RGB(255, 255, 255) : pale ? col::textDim : col::text;
        DrawButton(hdc, br, lab, bg, fg, A_PICK_BASE + k, false);
        // kleine Kennung rechts oben: vergeben / besetzt / gesperrt
        if (pale && !isCur)
        {
            const wchar_t* tag = st->second == 1 ? L"ASG" : st->second == 2 ? L"OCC" : L"BLK";
            Text(hdc, Rc(br.left + Sx(3), br.top + Sx(1), br.right - Sx(4), br.top + Sx(14)), tag, m_fLabel, col::textDim,
                 DT_RIGHT | DT_TOP | DT_SINGLELINE);
        }
    }
    // unten wie bei den anderen Listen
    int by = cy + cardH - pad - btnH;
    int bw = (right - x - 4 * gap) / 5;
    int bx = x;
    DrawButton(hdc, Rc(bx, by, bx + bw, by + btnH), L"Back", m_pickPage > 0 ? col::button : col::bay,
               m_pickPage > 0 ? col::text : col::textDim, A_PICK_PREV, false);
    bx += bw + gap;
    wchar_t pg[32];
    swprintf(pg, 32, L"Next  (%d/%d)", m_pickPage + 1, pages);
    DrawButton(hdc, Rc(bx, by, bx + bw, by + btnH), pg, m_pickPage + 1 < pages ? col::button : col::bay,
               m_pickPage + 1 < pages ? col::text : col::textDim, m_pickPage + 1 < pages ? A_PICK_NEXT : A_NONE, false);
    bx += bw + gap;
    DrawButton(hdc, Rc(bx, by, bx + bw, by + btnH), L"Type", RGB(54, 104, 168), RGB(255, 255, 255), A_PICK_FREE, false);
    bx += bw + gap;
    DrawButton(hdc, Rc(bx, by, bx + bw, by + btnH), L"Clear", col::buttonAlt, col::text, A_PICK_CLEAR, false);
    bx += bw + gap;
    DrawButton(hdc, Rc(bx, by, right, by + btnH), L"Cancel", col::danger, RGB(255, 255, 255), A_PICK_CLOSE, false);
}

// Papierkorb: entfernte und weggeschickte Streifen, Antippen holt zurueck
void StripWindow::DrawTrash(HDC hdc, int width, int height)
{
    Dim(hdc, width, height, col::dimAlpha);
    int pad = Sx(18), rowH = Sx(52), gap = Sx(6), btnH = Sx(50);
    int n = (int)m_board.trash.size();
    int cardW = std::min(width - Sx(32), Sx(640));
    int maxRows = std::max(1, (height - Sx(220)) / (rowH + gap));
    int rows = std::min(n, maxRows);
    int cardH = pad + Sx(44) + (n ? rows * (rowH + gap) : Sx(50)) + Sx(8) + btnH + pad;
    int cx = (width - cardW) / 2, cy = std::max(Sx(12), (height - cardH) / 2);
    m_trashCard = Rc(cx, cy, cx + cardW, cy + cardH);
    RoundBox(hdc, m_trashCard, Sx(14), col::card, col::cardBorder);

    int x = cx + pad, y = cy + pad;
    Text(hdc, Rc(x, y, cx + cardW - pad, y + Sx(36)), L"RECOVER", m_fTitle, col::text);
    Text(hdc, Rc(x, y, cx + cardW - pad, y + Sx(36)), L"Tap a strip to bring it back  \x2013  aircraft beyond 20 NM are gone", m_fDetailLabel,
         col::textDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    y += Sx(44);
    if (n == 0)
    {
        Text(hdc, Rc(x, y, cx + cardW - pad, y + Sx(50)), L"No removed strips", m_fHeader, col::textDim,
             DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        y += Sx(50);
    }
    for (int i = 0; i < rows; ++i)
    {
        const TrashItem& t = m_board.trash[i];
        RECT rr = Rc(x, y, cx + cardW - pad, y + rowH);
        COLORREF bg = t.kind == "handed over" ? RGB(250, 180, 70) : t.kind == "note" ? RGB(232, 232, 228)
                      : (t.kind == "parked" || t.arrival) ? col::paperArr : col::paper;
        DrawButton(hdc, rr, L"", bg, col::ink, A_RESTORE_BASE + i, false);
        Text(hdc, Rc(rr.left + Sx(14), rr.top, rr.right - Sx(160), rr.bottom), W(t.label), m_fCallsignSmall, col::ink);
        Text(hdc, Rc(rr.left, rr.top, rr.right - Sx(14), rr.bottom), W(t.kind) + L"   recover",
             m_fHeader, RGB(60, 60, 60), DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        y += rowH + gap;
    }
    if (n > rows)
    {
        wchar_t more[48];
        swprintf(more, 48, L"and %d more", n - rows);
        Text(hdc, Rc(x, y - gap, cx + cardW - pad, y + Sx(8)), more, m_fDetailLabel, col::textDim, DT_LEFT | DT_TOP | DT_SINGLELINE);
    }
    y += Sx(8);
    int bw = (cardW - 2 * pad - gap) / 2;
    DrawButton(hdc, Rc(x, y, x + bw, y + btnH), L"Recover all", n ? col::ok : col::button,
               n ? RGB(255, 255, 255) : col::textDim, A_TRASH_ALL, false);
    DrawButton(hdc, Rc(x + bw + gap, y, x + 2 * bw + gap, y + btnH), L"Close", col::button, col::text,
               A_TRASH_CLOSE, false);
}

// ---------------------------------------------------------------------------
// CPDLC / DCL (Hoppie): Verlauf und Aktionen fuer einen Flieger
// ---------------------------------------------------------------------------
void StripWindow::OpenDatalink(const std::string& cs)
{
    if (!FindStrip(cs)) return;
    m_dlCs = cs;
    m_dlOpen = true;
    m_trashOpen = false;
    if (m_host) m_host->Datalink(cs, DL_READ, std::string());
    Invalidate();
}

void StripWindow::OpenDlTextEditor(const std::string& cs)
{
    if (!FindStrip(cs)) return;
    OpenEditor(cs);
    if (!m_editing) return;
    m_editMode = 5;
    m_editText.clear();
    Invalidate();
}

void StripWindow::DrawDatalink(HDC hdc, int width, int height)
{
    const StripData* sp = FindStrip(m_dlCs);
    if (!sp) { m_dlOpen = false; return; }
    const StripData& s = *sp;
    Dim(hdc, width, height, col::dimAlpha);

    // Buttons je nach Lage
    struct B { std::wstring label; int action; COLORREF bg; COLORREF fg; };
    std::vector<B> bs;
    const COLORREF white = RGB(255, 255, 255), blue = RGB(70, 110, 190);
    // nur PDC: senden bzw. erneut senden, bei einer Anfrage "auf Sprechfunk"
    bool dclOk = !s.arrival && !s.dlDclText.empty() && s.dlDclText[0] != '!';
    if (!s.arrival)
        bs.push_back({ s.dlDcl >= 2 ? L"Resend PDC" : L"Send PDC", A_DL_BASE + DL_SEND_DCL,
                       dclOk ? col::ok : col::button, dclOk ? white : col::textDim });
    if (s.dlDcl == 1)
        bs.push_back({ L"PDC: call on voice", A_DL_BASE + DL_DCL_VOICE, col::button, col::text });
    (void)blue;
    bs.push_back({ L"Close", A_DL_CLOSE, col::button, col::text });

    int pad = Sx(18), gap = Sx(6), btnH = Sx(50), rowH = Sx(30), boxH = Sx(58);
    int cardW = std::min(width - Sx(32), Sx(860));
    int perRow = cardW >= Sx(600) ? 3 : 2;
    int btnRows = ((int)bs.size() + perRow - 1) / perRow;
    int boxes = (!s.dlRequest.empty() ? 1 : 0) + (!s.arrival ? 1 : 0) + (s.dlPending ? 1 : 0);
    int fixedH = pad + Sx(44) + Sx(30) + boxes * (boxH + gap) + Sx(8) + btnRows * (btnH + gap) + pad;
    int maxLog = std::max(1, (height - Sx(24) - fixedH) / rowH);
    int logRows = std::min((int)std::max<size_t>(1, s.dlLog.size()), std::min(maxLog, 10));
    int cardH = fixedH + logRows * rowH;
    int cx = (width - cardW) / 2, cy = std::max(Sx(12), (height - cardH) / 2);
    m_dlCard = Rc(cx, cy, cx + cardW, cy + cardH);
    RoundBox(hdc, m_dlCard, Sx(14), col::card, col::cardBorder);

    int x = cx + pad, y = cy + pad, right = cx + cardW - pad;
    Text(hdc, Rc(x, y, right, y + Sx(36)), L"PDC   " + W(s.callsign), m_fTitle, col::text);
    std::wstring st = L"Station " + W(m_board.dlStation) + (m_board.dlConnected ? L"  \x00B7  connected" : L"  \x00B7  not connected");
    Text(hdc, Rc(x, y, right, y + Sx(36)), st, m_fDetailLabel, m_board.dlConnected ? col::textDim : col::warnText,
         DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    y += Sx(44);
    static const wchar_t* logonTxt[] = { L"not logged on", L"logon requested", L"logged on" };
    static const wchar_t* dclTxt[] = { L"-", L"requested", L"sent, awaiting WILCO", L"confirmed (WILCO)", L"rejected (UNABLE)" };
    Text(hdc, Rc(x, y, right, y + Sx(26)),
         std::wstring(L"Logon: ") + logonTxt[std::max(0, std::min(2, s.dlLogon))] + L"      DCL: " +
             dclTxt[std::max(0, std::min(4, s.dlDcl))],
         m_fHeader, col::text);
    y += Sx(30);

    auto box = [&](const std::wstring& label, const std::wstring& body, COLORREF fg) {
        RECT r = Rc(x, y, right, y + boxH);
        RoundBox(hdc, r, Sx(8), col::input, col::cardBorder);
        Text(hdc, Rc(r.left + Sx(10), r.top + Sx(4), r.right - Sx(10), r.top + Sx(20)), label, m_fDetailLabel, col::textDim,
             DT_LEFT | DT_TOP | DT_SINGLELINE);
        Text(hdc, Rc(r.left + Sx(10), r.top + Sx(20), r.right - Sx(10), r.bottom - Sx(2)), body, m_fDetailLabel, fg,
             DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);
        y += boxH + gap;
    };
    if (!s.dlRequest.empty()) box(L"PILOT REQUEST", W(s.dlRequest), col::text);
    if (s.dlPending) box(L"AWAITING REPLY", W(s.dlPendingText), col::text);
    if (!s.arrival)
    {
        if (dclOk) box(L"PDC AS IT WILL BE SENT", W(s.dlDclText), col::text);
        else box(L"PDC NOT POSSIBLE", W(s.dlDclText.empty() ? std::string() : s.dlDclText.substr(1)), col::warnText);
    }

    // Verlauf: die letzten Nachrichten, neueste unten
    if (s.dlLog.empty())
    {
        Text(hdc, Rc(x, y, right, y + rowH), L"No messages yet", m_fDetailLabel, col::textDim);
        y += rowH;
    }
    else
    {
        size_t from = s.dlLog.size() > (size_t)logRows ? s.dlLog.size() - logRows : 0;
        for (size_t i = from; i < s.dlLog.size(); ++i)
        {
            const DlLine& l = s.dlLog[i];
            COLORREF c = l.up ? RGB(70, 110, 190) : col::text;
            Text(hdc, Rc(x, y, x + Sx(70), y + rowH), W(l.time), m_fDetailLabel, col::textDim);
            Text(hdc, Rc(x + Sx(70), y, x + Sx(110), y + rowH), l.up ? L"\x2191" : L"\x2193", m_fHeader, c);
            int stW = l.status.empty() ? 0 : Sx(110);
            Text(hdc, Rc(x + Sx(110), y, right - stW, y + rowH), W(l.text), m_fDetailLabel, c,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            if (!l.status.empty())
            {
                bool bad = l.status == "UNABLE" || l.status == "NEGATIVE" || l.status == "REJECT";
                Text(hdc, Rc(right - stW, y, right, y + rowH), W(l.status), m_fHeader, bad ? col::warnText : col::ok,
                     DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            }
            y += rowH;
        }
    }
    y += Sx(8);
    int bw = (right - x - (perRow - 1) * gap) / perRow;
    for (size_t i = 0; i < bs.size(); ++i)
    {
        int c = (int)i % perRow, r = (int)i / perRow;
        int bx = x + c * (bw + gap), by = y + r * (btnH + gap);
        DrawButton(hdc, Rc(bx, by, bx + bw, by + btnH), bs[i].label, bs[i].bg, bs[i].fg, bs[i].action, false);
    }
}

// ---------------------------------------------------------------------------
// Scratchpad-Editor mit Bildschirmtastatur. Waehrend er offen ist, darf das
// Fenster den Fokus haben, damit auch die echte Tastatur funktioniert.
// ---------------------------------------------------------------------------
static bool IsTransientScratchW(const std::string& sp)
{
    std::string u;
    for (char c : sp) u += (char)toupper((unsigned char)c);
    return u.compare(0, 6, "GRP/S/") == 0 || u.find(".VSID_") != std::string::npos || u.find(".SB_") != std::string::npos;
}

static const size_t kScratchMax = 40;

void StripWindow::OpenEditor(const std::string& cs)
{
    const StripData* s = FindStrip(cs);
    if (!s) return;
    m_editCs = cs;
    m_editMode = 0;
    m_editText = IsTransientScratchW(s->scratch) ? std::string() : s->scratch;
    m_editing = true;
    if (m_hwnd)
    {
        DpiScope dpi;
        m_prevForeground = GetForegroundWindow();
        LONG_PTR ex = GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE);
        SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE, ex & ~(LONG_PTR)WS_EX_NOACTIVATE);
        SetForegroundWindow(m_hwnd);
        SetFocus(m_hwnd);
    }
    Invalidate();
}

// Intersection-Editor: dieselbe Tastatur, hoechstens 3 Zeichen. OK = zugewiesen,
// "Able for" = nur moeglich, leer + OK = keine Intersection.
void StripWindow::OpenIntEditor(const std::string& cs)
{
    const StripData* s = FindStrip(cs);
    if (!s || s->arrival) return;
    OpenEditor(cs);
    if (!m_editing) return;
    m_editMode = 1;
    m_editAssign = true;
    m_editText = s->intersection;
    Invalidate();
}

// Hold-Short-Editor: hoechstens 6 Zeichen (z. B. "V1", "24R"), OK = setzen,
// leer + OK = loeschen. "Piste" traegt die Piste des Fliegers ein.
void StripWindow::OpenHsEditor(const std::string& cs)
{
    const StripData* s = FindStrip(cs);
    if (!s) return;
    std::string cur = s->holdShort;
    OpenEditor(cs);
    if (!m_editing) return;
    m_editMode = 2;
    m_editText = cur;
    Invalidate();
}

// Neuer Streifen: Callsign (mit Flugplan -> echter Streifen) oder Text (Notiz)
void StripWindow::OpenNewEditor()
{
    m_detailCs.clear();
    m_trashOpen = false;
    m_editCs.clear();
    m_editText.clear();
    m_editMode = 3;
    m_editNote = false;
    m_editing = true;
    if (m_hwnd)
    {
        DpiScope dpi;
        m_prevForeground = GetForegroundWindow();
        LONG_PTR ex = GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE);
        SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE, ex & ~(LONG_PTR)WS_EX_NOACTIVATE);
        SetForegroundWindow(m_hwnd);
        SetFocus(m_hwnd);
    }
    Invalidate();
}

void StripWindow::CloseEditor(bool commit)
{
    if (!m_editing) return;
    // OK mit noch nicht erkannter Handschrift: erst erkennen, dann uebernehmen
    if (commit && !m_inkStrokes.empty()) InkRecognize();
    if (RouteEditing()) m_editText = RouteJoined();
    std::string cs = m_editCs, text = m_editText;
    int mode = m_editMode;
    std::string prefix = m_editPrefix;
    m_editPrefix.clear();
    bool assign = m_editAssign;
    bool note = m_editNote, desig = m_editDesig;
    m_editNote = false;
    m_editDesig = false;
    m_editing = false;
    m_editMode = 0;
    m_inkStrokes.clear();
    m_inkMsg.clear();
    if (m_hwnd) KillTimer(m_hwnd, kInkTimer);
    m_editAssign = true;
    m_editCs.clear();
    m_editText.clear();
    if (m_hwnd)
    {
        DpiScope dpi;
        LONG_PTR ex = GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE);
        SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE, ex | WS_EX_NOACTIVATE);
        // Fokus zurueck an EuroScope
        if (m_prevForeground && m_prevForeground != m_hwnd && IsWindow(m_prevForeground))
            SetForegroundWindow(m_prevForeground);
        m_prevForeground = nullptr;
    }
    Invalidate();
    if (commit && m_host && mode == 6)
    {
        m_host->Command(prefix + text);   // Einstellung aus dem Einstellungsfenster
        return;
    }
    if (commit && m_host && mode == 3)
    {
        // neuer Streifen in die erste Spalte der Ansicht (Notiz) bzw. nach Status (Flugplan)
        int bay = ViewOf(m_view, m_board).cols[0].dropBay;
        if (desig)
        {
            // Designator-Strip: Trennstreifen; ohne Text "RUNWAY OBSTRUCTED"
            m_host->Command(".strips designator " + std::to_string(bay) + " " + (text.empty() ? std::string("RUNWAY OBSTRUCTED") : text));
            return;
        }
        m_host->CreateStrip(text, note, bay);
        return;
    }
    if (commit && m_host && !cs.empty())
    {
        if (mode == 4) m_host->SetField(cs, m_editField, text);
        else if (mode == 1) m_host->SetIntersection(cs, text, assign);
        else if (mode == 5) m_host->Datalink(cs, DL_FREE, text);
        else if (mode == 2) m_host->SetHoldShort(cs, text);
        else m_host->SetScratchpad(cs, text);
    }
}

void StripWindow::EditKey(wchar_t c)
{
    if (!m_editing) return;
    if (c == L'\r' || c == L'\n') { CloseEditor(true); return; }
    if (c == 27) { CloseEditor(false); return; }
    if (c == 0x16) { PasteClipboard(); return; }   // Strg+V
    if (RouteEditing())
    {
        if (m_rtTok.empty()) { m_rtTok.push_back(std::string()); m_rtSel = 0; }
        std::string& tok = m_rtTok[m_rtSel];
        if (c == L'\b')
        {
            if (!tok.empty()) tok.pop_back();
            else if (m_rtTok.size() > 1)
            {
                // leerer Punkt: weg, weiter am vorherigen
                m_rtTok.erase(m_rtTok.begin() + m_rtSel);
                m_rtSel = std::max(0, m_rtSel - 1);
            }
        }
        else if (c == L' ')
        {
            // Leerzeichen: naechster Punkt (neu dahinter)
            if (!tok.empty() && m_rtTok.size() < 200)
            {
                m_rtTok.insert(m_rtTok.begin() + m_rtSel + 1, std::string());
                ++m_rtSel;
            }
        }
        else if (c > 32 && c < 127 && tok.size() < 40 && RouteJoined().size() < 400)
            tok += (char)towupper(c);
        m_editText = m_rtTok[m_rtSel];
        Invalidate();
        return;
    }
    if (c == L'\b')
    {
        if (!m_editText.empty()) m_editText.pop_back();
    }
    else if (m_editMode == 4)
    {
        // Feld aus dem Detailmenue: Zahlenfelder nur Ziffern, Route und Remarks frei
        int f = m_editField;
        bool digits = (f == F_SQUAWK || f == F_HDG || f == F_SPD || f == F_EOBT);
        bool freeText = (f == F_ROUTE || f == F_REMARKS);
        size_t maxLen = freeText ? 400 : (f == F_SQUAWK || f == F_EOBT) ? 4 : 12;
        bool okChar = digits ? (c >= L'0' && c <= L'9')
                             : freeText ? (c >= 32 && c < 127)
                                        : (iswalnum(c) || c == L'.' || c == L'-' || c == L'/') && c < 127;
        if (okChar && m_editText.size() < maxLen) m_editText += (char)(f == F_REMARKS ? c : towupper(c));
    }
    else if (m_editMode == 6)
    {
        // Einstellung: Text wie eingegeben (z. B. Logon-Code mit Gross- und Kleinbuchstaben)
        if (c >= 32 && c < 127 && m_editText.size() < 80) m_editText += (char)c;
    }
    else if (m_editMode == 5)
    {
        // CPDLC-Freitext: Grossbuchstaben, Ziffern und wenige Zeichen
        if ((iswalnum(c) || c == L' ' || c == L'-' || c == L'/' || c == L'.' || c == L',') && c < 127 && m_editText.size() < 160)
            m_editText += (char)towupper(c);
    }
    else if (m_editMode == 3)
    {
        if ((iswalnum(c) || c == L' ' || c == L'-' || c == L'/' || c == L'.') && c < 127 && m_editText.size() < 16)
            m_editText += (char)towupper(c);
    }
    else if (m_editMode == 1 || m_editMode == 2)
    {
        size_t maxLen = m_editMode == 1 ? 3 : 6;
        if (iswalnum(c) && c < 127 && m_editText.size() < maxLen) m_editText += (char)towupper(c);
    }
    else if (c >= 32 && c < 127 && m_editText.size() < kScratchMax)
    {
        m_editText += (char)c;
    }
    Invalidate();
}

// Zwischenablage einfuegen, als waere sie getippt (es gelten die Regeln des Editors);
// Zeilenumbrueche werden zu Leerzeichen, Leerraum am Rand faellt weg
void StripWindow::PasteClipboard()
{
    if (!m_editing) return;
    std::string text;
    if (OpenClipboard(m_hwnd))
    {
        HANDLE h = GetClipboardData(CF_UNICODETEXT);
        if (h)
        {
            const wchar_t* w = (const wchar_t*)GlobalLock(h);
            if (w)
            {
                for (const wchar_t* q = w; *q && text.size() < 400; ++q)
                {
                    wchar_t c = *q;
                    if (c == L'\r' || c == L'\n' || c == L'\t') c = L' ';
                    if (c >= 32 && c < 127) text += (char)c;
                }
                GlobalUnlock(h);
            }
        }
        CloseClipboard();
    }
    size_t a = text.find_first_not_of(' '), b = text.find_last_not_of(' ');
    if (a == std::string::npos) return;
    // Route: an einen fertigen Punkt nicht anhaengen, sondern als neue Punkte dahinter
    if (RouteEditing() && m_rtSel >= 0 && m_rtSel < (int)m_rtTok.size() && !m_rtTok[m_rtSel].empty()) EditKey(L' ');
    for (char c : text.substr(a, b - a + 1)) EditKey((wchar_t)c);
    Invalidate();
}

// Handschrift erkennen und wie getippt einfuegen (es gelten die Regeln des Editors)
void StripWindow::InkRecognize()
{
    if (m_hwnd) KillTimer(m_hwnd, kInkTimer);
    if (!m_editing || m_inkStrokes.empty()) return;
    std::string text, err;
    if (RecognizeInk(m_inkStrokes, m_dpi, text, err))
    {
        // Handschrift in Grossbuchstaben (wie auf dem Papierstreifen), nur Remarks bleiben wie erkannt
        bool keepCase = (m_editMode == 4 && m_editField == F_REMARKS) || m_editMode == 6;
        for (char c : text)
            if (c >= 32 && c < 127) EditKey((wchar_t)(keepCase ? c : toupper((unsigned char)c)));
        m_inkMsg = text.empty() ? std::string("nothing recognized") : std::string();
    }
    else m_inkMsg = err;
    m_inkStrokes.clear();
    Invalidate();
}

// Route mehrzeilig als Punkte (antippen = bearbeiten); der markierte Punkt blau mit Cursor
void StripWindow::DrawRouteBox(HDC hdc, const RECT& box)
{
    RoundBox(hdc, box, Sx(8), col::input, kBayAccent[BAY_TAXI]);
    int px = Sx(10), py = Sx(6), lineH = Sx(46), chipH = Sx(38), cg = Sx(6);
    int lines = std::max(1, (int)((box.bottom - box.top - 2 * py) / lineH));
    HFONT f = m_fDetailValue;
    // Umbruch berechnen
    struct Chip { int row, x, w; };
    std::vector<Chip> chips;
    int row = 0, cxp = (int)box.left + px, maxX = (int)box.right - px;
    for (size_t i = 0; i < m_rtTok.size(); ++i)
    {
        std::wstring t = W(m_rtTok[i]) + ((int)i == m_rtSel ? L"|" : L"");
        int w = std::max(Sx(30), TextWidth(hdc, t, f) + Sx(18));
        w = std::min(w, (int)(maxX - (box.left + px)));
        if (cxp + w > maxX && cxp > box.left + px) { ++row; cxp = box.left + px; }
        chips.push_back({ row, cxp, w });
        cxp += w + cg;
    }
    int selRow = (m_rtSel >= 0 && m_rtSel < (int)chips.size()) ? chips[m_rtSel].row : 0;
    int first = std::max(0, selRow - (lines - 1));
    int lastRow = chips.empty() ? 0 : chips.back().row;
    for (size_t i = 0; i < chips.size(); ++i)
    {
        const Chip& c = chips[i];
        if (c.row < first || c.row >= first + lines) continue;
        int top = box.top + py + (c.row - first) * lineH + (lineH - chipH) / 2;
        RECT r = Rc(c.x, top, c.x + c.w, top + chipH);
        bool sel = (int)i == m_rtSel;
        const std::string& tk = m_rtTok[i];
        bool dct = tk == "DCT";
        // Airway (Buchstabe(n) + Ziffern, z. B. T204, UL602): grau hinterlegt, Punkte weiss
        bool awy = tk.size() >= 2 && isalpha((unsigned char)tk[0]) && isdigit((unsigned char)tk.back()) &&
                   tk.find('/') == std::string::npos && tk.size() <= 6 &&
                   (isdigit((unsigned char)tk[1]) || (tk.size() > 2 && isdigit((unsigned char)tk[2])));
        COLORREF bg = sel ? RGB(70, 110, 190) : dct ? RGB(222, 222, 216) : awy ? RGB(232, 236, 244) : RGB(255, 255, 255);
        RoundBox(hdc, r, Sx(6), bg, sel ? RGB(40, 70, 140) : col::cardBorder);
        Text(hdc, r, W(tk) + (sel ? L"|" : L""), f, sel ? RGB(255, 255, 255) : RGB(30, 30, 30),
             DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (i < 400) m_buttons.push_back({ r, A_RT_TOK_BASE + (int)i });
    }
    // mehr Zeilen als sichtbar: Hinweis
    if (first > 0 || lastRow >= first + lines)
    {
        wchar_t more[48];
        swprintf(more, 48, L"%ls%ls", first > 0 ? L"\x25B2 " : L"", lastRow >= first + lines ? L"\x25BC" : L"");
        Text(hdc, Rc(box.right - Sx(40), box.top, box.right - Sx(6), box.top + Sx(18)), more, m_fDetailLabel, col::textDim,
             DT_RIGHT | DT_TOP | DT_SINGLELINE);
    }
}

void StripWindow::DrawEditor(HDC hdc, int width, int height)
{
    Dim(hdc, width, height, (BYTE)(col::dimAlpha * 4 / 5));

    int pad = Sx(18), gap = Sx(6);
    bool route = RouteEditing();
    int cardW = std::min(width - Sx(24), route ? Sx(980) : Sx(860));
    int keyW = (cardW - 2 * pad - 9 * gap) / 10;
    int keyH = std::max(Sx(48), std::min(Sx(62), (height - Sx(240)) / 5));
    int titleH = Sx(46), inputH = Sx(64);
    int rtLines = 4, rtLineH = Sx(46);
    if (route)
    {
        // Route mehrzeilig (Punkte), darunter die Werkzeuge, dann die Tastatur
        int fixed = Sx(20) + 2 * pad + titleH + Sx(10) + Sx(14) + 5 * gap;
        int boxH = rtLines * rtLineH + Sx(12);
        keyH = std::max(Sx(34), std::min(Sx(58), (height - fixed - boxH) / 6));
        if ((height - fixed - boxH) / 6 < Sx(34)) { rtLines = 3; boxH = rtLines * rtLineH + Sx(12); }
        inputH = boxH + Sx(10) + keyH;
    }
    int cardH = pad + titleH + inputH + Sx(14) + 5 * keyH + 4 * gap + pad;
    int cx = (width - cardW) / 2;
    int cy = std::max(Sx(10), (height - cardH) / 2);
    m_editCard = Rc(cx, cy, cx + cardW, cy + cardH);
    RoundBox(hdc, m_editCard, Sx(14), col::card, col::cardBorder);

    int x = cx + pad, y = cy + pad;
    Text(hdc, Rc(x, y, cx + cardW - pad, y + titleH - Sx(8)),
         (m_editMode == 4 ? std::wstring(FieldTitle(m_editField)) + L"  " :
          m_editMode == 6 ? m_editTitle + L"  " :
          m_editMode == 5 ? std::wstring(L"CPDLC FREE TEXT  ") :
          m_editMode == 3 ? std::wstring(L"NEW STRIP   (callsign; without a flight plan it becomes a note strip)") :
          m_editMode == 1 ? std::wstring(L"INTERSECTION  ") : m_editMode == 2 ? std::wstring(L"HOLD SHORT  ")
                          : std::wstring(L"SCRATCHPAD  ")) + W(m_editCs) +
             (m_editMode == 1 ? L"   (OK = assigned, empty = none)"
                              : m_editMode == 2 ? L"   (OK = set, empty = clear)" : L""),
         m_fHeader, col::textDim);
    // Umschalter Stift / Tastatur (rechts in der Titelzeile)
    {
        int bw = Sx(150);
        DrawButton(hdc, Rc(cx + cardW - pad - bw, y - Sx(4), cx + cardW - pad, y + titleH - Sx(10)),
                   m_inkMode ? L"Keyboard" : L"Pen (handwriting)", m_inkMode ? col::button : RGB(70, 110, 190),
                   m_inkMode ? col::text : RGB(255, 255, 255), A_EDIT_INK, false);
    }
    y += titleH;

    if (route)
    {
        int boxH = rtLines * rtLineH + Sx(12);
        DrawRouteBox(hdc, Rc(x, y, cx + cardW - pad, y + boxH));
        y += boxH + Sx(10);
        struct T { const wchar_t* lab; int act; };
        static const T tools[] = { { L"\x25C0", A_RT_LEFT }, { L"\x25B6", A_RT_RIGHT }, { L"Insert", A_RT_INS },
                                   { L"Delete", A_RT_DEL }, { L"DCT", A_RT_DCT }, { L"Cut to end", A_RT_CUT },
                                   { L"Original", A_RT_RESET }, { L"Paste", A_EDIT_PASTE } };
        int n = 8, tw = (cardW - 2 * pad - (n - 1) * gap) / n;
        for (int i = 0; i < n; ++i)
        {
            bool blue = tools[i].act == A_EDIT_PASTE || tools[i].act == A_RT_DCT;
            DrawButton(hdc, Rc(x + i * (tw + gap), y, i == n - 1 ? cx + cardW - pad : x + i * (tw + gap) + tw, y + keyH),
                       tools[i].lab, tools[i].act == A_RT_DEL || tools[i].act == A_RT_CUT ? col::buttonAlt
                                     : blue ? RGB(70, 110, 190) : col::button,
                       blue ? RGB(255, 255, 255) : col::text, tools[i].act, false);
        }
        y += keyH + Sx(14);
    }
    // Eingabefeld mit Cursor
    RECT in = Rc(x, y, cx + cardW - pad, y + inputH);
    if (!route) {
    RoundBox(hdc, in, Sx(8), col::input, kBayAccent[BAY_TAXI]);
    // lange Texte (Route): das Ende zeigen, dort wird getippt
    std::wstring t = W(m_editText) + L"|";
    HFONT inFont = (m_editMode == 4 || m_editMode == 5) && m_editText.size() > 20 ? m_fDetailValue : m_fDetailTitle;
    int avail = (in.right - in.left) - Sx(90);
    while (t.size() > 2 && TextWidth(hdc, t, inFont) > avail) t.erase(0, 1);
    if (t.size() < m_editText.size() + 1) t = L"\x2026" + t;
    Text(hdc, Inset(in, Sx(14), 0), t, inFont, col::text);
    wchar_t cnt[16];
    swprintf(cnt, 16, L"%d/%d", (int)m_editText.size(),
             m_editMode == 1 ? 3 : m_editMode == 2 ? 6 : m_editMode == 3 ? 16 : m_editMode == 5 ? 160 : m_editMode == 6 ? 80
             : m_editMode == 4 ? ((m_editField == F_ROUTE || m_editField == F_REMARKS) ? 400 : 12) : (int)kScratchMax);
    Text(hdc, Inset(in, Sx(12), 0), cnt, m_fDetailLabel, col::textDim, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    y += inputH + Sx(14);
    }

    if (m_inkMode)
    {
        // Schreibfeld statt der ersten drei Tastenreihen
        int padH = 3 * keyH + 2 * gap;
        m_inkPad = Rc(x, y, cx + cardW - pad, y + padH);
        RoundBox(hdc, m_inkPad, Sx(10), col::input, RGB(70, 110, 190));
        // Schreiblinie
        HPEN guide = CreatePen(PS_DOT, 1, col::textDim);
        HGDIOBJ og = SelectObject(hdc, guide);
        POINT gl[2] = { { m_inkPad.left + Sx(20), m_inkPad.top + padH * 3 / 4 }, { m_inkPad.right - Sx(20), m_inkPad.top + padH * 3 / 4 } };
        Polyline(hdc, gl, 2);
        SelectObject(hdc, og);
        DeleteObject(guide);
        if (m_inkStrokes.empty())
            Text(hdc, Rc(m_inkPad.left, m_inkPad.top + Sx(8), m_inkPad.right, m_inkPad.top + Sx(40)),
                 m_inkMsg.empty() ? std::wstring(L"Write here with pen or finger \x2013 recognized after a short pause")
                                  : L"Handwriting: " + W(m_inkMsg),
                 m_fDetailLabel, m_inkMsg.empty() ? col::textDim : col::warnText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        HPEN pen = CreatePen(PS_SOLID, std::max(2, Sx(3)), col::text);
        HGDIOBJ op = SelectObject(hdc, pen);
        for (const std::vector<POINT>& st : m_inkStrokes)
            if (st.size() >= 2) Polyline(hdc, st.data(), (int)st.size());
        SelectObject(hdc, op);
        DeleteObject(pen);
        y += padH + gap;
        // Reihe: Zeichen zurueck | Leerzeichen | Tinte weg | Erkennen
        int w2 = 2 * keyW + gap, w4 = 4 * keyW + 3 * gap;
        int bx = x;
        DrawButton(hdc, Rc(bx, y, bx + w2, y + keyH), L"\x2190", col::buttonAlt, col::text, A_EDIT_BACK, false);
        bx += w2 + gap;
        DrawButton(hdc, Rc(bx, y, bx + w4, y + keyH), L"Space", col::button, col::text, A_KEY_BASE + ' ', false);
        bx += w4 + gap;
        DrawButton(hdc, Rc(bx, y, bx + w2, y + keyH), L"Clear ink", col::button, col::text, A_INK_CLEAR, false);
        bx += w2 + gap;
        DrawButton(hdc, Rc(bx, y, x + cardW - 2 * pad, y + keyH), L"Recognize", RGB(70, 110, 190), RGB(255, 255, 255),
                   A_INK_RECO, false);
        y += keyH + gap;
    }
    // Tastatur (QWERTZ)
    static const char* rows[4] = { "1234567890", "QWERTZUIOP", "ASDFGHJKL/", "YXCVBNM.-\b" };
    for (int r = 0; r < (m_inkMode ? 0 : 4); ++r)
    {
        for (int k = 0; k < 10; ++k)
        {
            char ch = rows[r][k];
            RECT kr = Rc(x + k * (keyW + gap), y, x + k * (keyW + gap) + keyW, y + keyH);
            if (ch == '\b') DrawButton(hdc, kr, L"\x2190", col::buttonAlt, col::text, A_EDIT_BACK, false);
            else
            {
                // Einstellungen (Logon-Code): Kleinbuchstaben umschaltbar
                if (m_editMode == 6 && m_editLower && ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
                wchar_t lab[2] = { (wchar_t)ch, 0 };
                DrawButton(hdc, kr, lab, col::button, col::text, A_KEY_BASE + (unsigned char)ch, false);
            }
        }
        y += keyH + gap;
    }
    // letzte Reihe: Leeren | Leerzeichen | Abbrechen | OK
    int w2 = 2 * keyW + gap, w4 = 4 * keyW + 3 * gap;
    int bx = x;
    DrawButton(hdc, Rc(bx, y, bx + w2, y + keyH), L"Clear", col::buttonAlt, col::text, A_EDIT_CLEAR, false);
    bx += w2 + gap;
    if (m_editMode == 1)
        DrawButton(hdc, Rc(bx, y, bx + w4, y + keyH), L"Able for", col::button, col::text, A_EDIT_ABLE, false);
    else if (m_editMode == 3)
    {
        // Uebernehmen gibt es nur mit Zustaendigkeit
        const bool req = m_board.opt.respOn;
        int tw = req ? (w4 - 2 * gap) / 3 : (w4 - gap) / 2;
        DrawButton(hdc, Rc(bx, y, bx + tw, y + keyH), L"As note strip", col::button, col::text, A_EDIT_NOTE, false);
        DrawButton(hdc, Rc(bx + tw + gap, y, req ? bx + 2 * tw + gap : bx + w4, y + keyH), L"Designator", RGB(60, 70, 95),
                   RGB(255, 255, 255), A_EDIT_DESIG, false);
        if (req)
            DrawButton(hdc, Rc(bx + 2 * (tw + gap), y, bx + w4, y + keyH), L"Take over", RGB(250, 180, 70), RGB(20, 20, 20), A_EDIT_REQ, false);
    }
    else if (m_editMode == 2)
    {
        const StripData* sd = FindStrip(m_editCs);
        std::string rw = sd ? (sd->arrival ? sd->arrRwy : sd->rwy) : std::string();
        DrawButton(hdc, Rc(bx, y, bx + w4, y + keyH), rw.empty() ? std::wstring(L"Runway") : L"Runway " + W(rw), col::button,
                   col::text, A_EDIT_RWY, false);
    }
    else if (m_editMode == 6)
    {
        // Logon-Code & Co.: Gross/klein umschalten und Einfuegen aus der Zwischenablage
        int hw = (w4 - gap) / 2;
        DrawButton(hdc, Rc(bx, y, bx + hw, y + keyH), m_editLower ? L"ABC" : L"abc", col::button, col::text, A_EDIT_CASE,
                   m_editLower);
        DrawButton(hdc, Rc(bx + hw + gap, y, bx + w4, y + keyH), L"Paste", RGB(70, 110, 190), RGB(255, 255, 255),
                   A_EDIT_PASTE, false);
    }
    else
        DrawButton(hdc, Rc(bx, y, bx + w4, y + keyH), route ? L"Space  (next point)" : L"Space", col::button, col::text,
                   A_KEY_BASE + ' ', false);
    bx += w4 + gap;
    DrawButton(hdc, Rc(bx, y, bx + w2, y + keyH), L"Cancel", col::danger, col::text, A_EDIT_CANCEL, false);
    bx += w2 + gap;
    DrawButton(hdc, Rc(bx, y, x + cardW - 2 * pad, y + keyH),
               m_editMode == 1 ? L"Assigned" : m_editMode == 3 ? L"Create" : m_editMode == 5 ? L"Send" : L"OK", col::ok,
               RGB(255, 255, 255),
               A_EDIT_OK, false);
}
