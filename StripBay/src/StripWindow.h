// StripBay - eigenes Touch-Fenster mit den Streifen-Bays.
// Reines Win32/GDI, keine Abhaengigkeit von EuroScope.
#pragma once

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>
#include <vector>

#include "Model.h"

struct WindowSettings
{
    RECT normalRect = { 100, 100, 1500, 800 };  // Bildschirmkoordinaten (physische Pixel)
    bool fullscreen = false;
    bool topmost = false;
    bool visible = true;
    double zoom = 1.0;
    int layout = 0;   // 0 = automatisch, 1 = alle Bays nebeneinander, 2 = Raster mit zwei Reihen
    int view = 0;     // 0 = alle Bays, 1 = Delivery, 2 = Ground, 3 = Tower
    int rwyFilter = 0;// getrennte Pisten: 0 = beide, RWY_A, RWY_B
    bool light = true;  // helles Farbschema (DFS-Stil), ab 2.0.21 Standard
    // ab 4.2.0: Groesse, Anordnung und Farbschema je Station merken ("zoom,layout,light" je Ansicht, mit "/" getrennt;
    // nicht gesetzte Werte -1) - beim Wechsel der Station stellt sich das Fenster darauf ein
    bool perView = true;
    std::string presets;
};

// Farben des Zeitstrahls: DFS-Schema (Tag / Nacht, dunkelgrauer bzw. fast schwarzer Grund) oder wie das Fenster
struct TlPal
{
    COLORREF bg, side, past, axisBg, grid, sep, conn, text, textDim, frame, hover, nowC, plan;
    COLORREF arrBody, arrInk, arrDim, arrEdge, arrField, arrFieldEdge;
    COLORREF depBody, depInk, depDim, depEdge, depField, depFieldEdge;
    int statusPct;   // Statusfarben in Prozent (Nacht gedimmt)
};

class StripWindow
{
public:
    explicit StripWindow(IStripHost* host);
    ~StripWindow();

    bool Create(const WindowSettings& settings);
    void Destroy();

    void SetBoard(const Board& board);
    const Board& GetBoard() const { return m_board; }

    void Show(bool show);
    bool IsVisible() const;
    void ToggleFullscreen();
    void SetTopmost(bool on);
    void SetZoom(double zoom);
    double GetZoom() const { return m_zoom; }
    void SetLayout(int layout);
    void SetView(int view);
    int  GetView() const { return m_view; }
    bool ShowsBay(int bay) const { return BayInView(bay); }
    bool ShowsArrivalsIn(int bay) const;   // zeigt die Ansicht Ankuenfte in dieser Bay?
    void SetRwyFilter(int filter);
    void SetLight(bool light);
    bool IsLight() const { return m_light; }
    void SetPerView(bool on) { m_perView = on; Invalidate(); }
    bool PerView() const { return m_perView; }
    void SetFilter(const std::string& text);            // Suche / Filter (leer = alles zeigen)
    const std::string& Filter() const { return m_filter; }
    int  GetRwyFilter() const { return m_rwyFilter; }
    WindowSettings GetSettings() const;

    // Zeichnet die komplette Oberflaeche in einen beliebigen DC.
    // Wird vom Fenster benutzt, laesst sich aber auch ohne Fenster testen.
    void RenderTo(HDC hdc, int width, int height);

    // Fuer Tests: Detailansicht fuer ein Callsign oeffnen
    void OpenDetail(const std::string& callsign) { m_detailCs = callsign; Invalidate(); }

    // StripBay Remote: dieses Fenster laeuft auf einem anderen Geraet (info = "Connected to ...", leer = im Plugin)
    void SetRemoteClient(const std::wstring& info) { m_remoteClient = !info.empty(); m_remoteInfo = info; Invalidate(); }
    bool IsRemoteClient() const { return m_remoteClient; }
    // Remote ohne Verbindung: Grund auf der Karte "NOT CONNECTED" (leer = Standardtext)
    void SetOfflineDetail(const std::wstring& t) { if (t != m_offlineDetail) { m_offlineDetail = t; Invalidate(); } }
    void ShowNote(const std::wstring& text, int ms = 6000);   // kurze Meldung unten im Fenster
    void SetTitle(const std::wstring& title);

private:
    // ----- Layout -----
    struct BayRect
    {
        RECT frame{}, header{}, body{}, track{};
        int contentHeight = 0;
        bool overflow = false;
    };
    struct Button
    {
        RECT rc{};
        int action = 0;
    };
    // Eine Spalte der aktuellen Ansicht. Sie kann mehrere Bays zusammen zeigen.
    struct StripRef { int bay; int idx; };
    struct Column
    {
        int def = -1;                    // Index in der Spaltentabelle der Ansicht
        int group = RWY_NONE;            // getrennte Pisten: nur Streifen dieser Piste (RWY_NONE = alle)
        int sub = 0;                     // 0 = volle Hoehe, 1 = obere, 2 = untere Haelfte (beide Pisten)
        std::vector<StripRef> strips;    // in Anzeigereihenfolge
        BayRect rect;
    };
    static const int kMaxCols = 32;
    enum Mode { M_NONE, M_PRESS_STRIP, M_DRAG_STRIP, M_SCROLL, M_PRESS_BUTTON, M_PRESS_OVERLAY_BG, M_INK, M_PICK_DRAG, M_RESIZE };

    enum Action
    {
        A_NONE = 0,
        A_ZOOM_OUT,
        A_ZOOM_IN,
        A_FULLSCREEN,
        A_HIDE,
        A_VIEW,
        A_RWY_FILTER,        // getrennte Pisten: beide -> erste -> zweite (oben, wie A_VIEW)
        A_THEME,             // hell / dunkel
        A_NEW_STRIP,         // Streifen erstellen (Editor)
        A_TRASH_OPEN,        // Papierkorb zeigen
        A_AREA_A,            // getrennte Pisten: nur erste Piste (North)
        A_AREA_BOTH,         // ... beide (Combined)
        A_AREA_B,            // ... nur zweite Piste (South)
        A_AREA_MENU,         // Bereichsauswahl (Dropdown) auf/zu
        A_SETTINGS,          // Einstellungsfenster auf/zu
        A_SET_CLOSE,         // Einstellungsfenster schliessen
        A_CLOSE_DETAIL,      // ab hier: Buttons in Details/Editor
        A_CLEARANCE,
        A_REMOVE,
        A_RWY_CLEAR,         // Start- bzw. Landefreigabe umschalten (Details)
        A_GOAROUND,          // Durchstarten (Details)
        A_TRASH_CLOSE,       // Papierkorb schliessen
        A_TRASH_ALL,         // alles zurueckholen
        A_PICK_PREV,         // Auswahlliste: Seite zurueck
        A_PICK_NEXT,         // ... Seite vor
        A_PICK_FREE,         // ... freie Eingabe (Tastatur)
        A_PICK_CLEAR,        // ... Wert leeren
        A_PICK_CLOSE,        // ... schliessen
        A_STATE_BASE = 100,  // + GroundCmd: Ground State setzen
        A_MOVE_BASE = 200,   // + Bay: Streifen in diese Bay legen (z. B. An Tower, Landefreigabe)
        A_EDIT_OPEN = 300,   // Scratchpad-Editor oeffnen
        A_EDIT_OK,
        A_EDIT_CANCEL,
        A_EDIT_CLEAR,
        A_EDIT_BACK,
        A_EDIT_ABLE,         // Intersection-Editor: "able for" statt zugewiesen
        A_EDIT_RWY,          // Hold-Short-Editor: Piste des Fliegers eintragen
        A_EDIT_NOTE,         // Neuer-Streifen-Editor: als Notiz-Streifen anlegen
        A_EDIT_DESIG,        // Neuer-Streifen-Editor: als Designator-Strip (Trennstreifen) anlegen
        A_EDIT_INK,          // Editor: Stift (Handschrift) / Tastatur umschalten
        A_INK_CLEAR,         // Handschrift: Tinte loeschen
        A_INK_RECO,          // Handschrift: jetzt erkennen
        A_EDIT_CASE,         // Einstellungs-Editor: Klein-/Grossbuchstaben umschalten
        A_EDIT_PASTE,        // Editor: Zwischenablage einfuegen
        A_PICK_UP,           // Hoehenliste: nach oben blaettern
        A_PICK_AUTO,         // Standauswahl: Auto (Ground Radar Plugin)
        A_RWY_SWITCH,        // Betriebsrichtungswechsel: Abfluege umstellen
        A_PICK_BTN0,         // Standauswahl: Direkt-Button (Apron 1)
        A_PICK_BTN1,         // Standauswahl: Direkt-Button (GAT)
        A_PICK_DOWN,         // Hoehenliste: nach unten blaettern
        A_INT_OPEN,          // Intersection-Editor oeffnen (Details)
        A_HOV,               // weggeschickt umschalten (Details)
        A_HS_OPEN,           // Hold-Short-Editor oeffnen (Details)
        A_RT_LEFT,           // Routen-Editor: vorheriger Punkt
        A_RT_RIGHT,          // ... naechster Punkt
        A_RT_INS,            // ... neuen Punkt davor einfuegen
        A_RT_DEL,            // ... Punkt loeschen
        A_RT_DCT,            // ... DCT einfuegen
        A_RT_CUT,            // ... ab hier bis zum Ende loeschen
        A_RT_RESET,          // ... urspruengliche Route
        A_REQ,               // Streifen einer anderen Stelle uebernehmen (Details, Zeitstrahl-Menue)
        A_EDIT_REQ,          // Neuer-Streifen-Editor: Callsign uebernehmen
        A_HL_BASE,           // + 0..5: Streifen farblich hervorheben (Details)
        A_HL_END = A_HL_BASE + 5,
        A_NEXTRESP,
        A_FLIPDIR,           // Inbound <-> Outbound (Details)
        A_REJECT,            // Uebergabe ablehnen (Details)
        A_UNDO,              // letzte Aktion rueckgaengig (obere Leiste)
        A_DIVERT,            // Diversion ein/aus (Details)          // naechste Stelle als zustaendig (Details)
        A_TIMELINE,          // Tower: Zeitstrahl / Spalten umschalten (obere Leiste)
        A_TL_DETAILS,        // Zeitstrahl-Menue: ganzes Detailfenster
        A_TL_OTHERSIDE,      // Zeitstrahl-Menue: auf die andere Piste (Nord <-> Sued)
        A_TL_COLOUR,         // Zeitstrahl-Menue: Farbe (Hervorhebung)
        A_TL_DAYNIGHT,       // DFS-Schema: Tag / Nacht
        A_TL_PLANRESET,      // Zeitstrahl-Menue: geplante Reihenfolge zuruecknehmen
        A_TL_SEQ,            // Zeitstrahl-Menue: Zeit fuer die Reihenfolge (Tastenfeld)
        A_RWYST_BASE,        // + Spalte: Piste gelb / rot / frei schalten (Kopf der Runway-Bay)
        A_RWYST_END = A_RWYST_BASE + 31,
        A_SEARCH,            // Suche / Filter (obere Leiste): Editor oeffnen bzw. Filter loeschen
        A_DEICE,             // Enteisung weiterschalten (Details)
        A_EDIT_FDEICE,       // Such-Editor: nur Flieger, die auf die Enteisung warten
        A_RESTORE_BASE = 500,// + Index im Papierkorb: zurueckholen
        A_FIELD_BASE = 600,  // + Field: Feld im Detailmenue bearbeiten
        A_PICK_BASE = 700,   // + Index auf der Seite: Wert aus der Liste
        A_PICK_TAB_BASE = 800, // + Reiter (Anfangsbuchstabe)
        A_DL_BASE = 900,     // + DlAction: CPDLC-Fenster
        A_DL_OPEN = 950,     // CPDLC-Fenster oeffnen (Details)
        A_DL_CLOSE,
        A_DL_TEXT,           // Freitext eingeben
        A_VIEW_BASE = 960,   // + Eintrag der Stationsauswahl (Dropdown)
        A_KEY_BASE = 1000,   // + Zeichen: Taste der Bildschirmtastatur (bis 1255)
        A_SET_BASE = 1300,   // + Eintrag im Einstellungsfenster
        A_KP_OK = 1490,      // Tastenfeld (Squawk, Hold Short, Intersection)
        A_KP_CANCEL,
        A_KP_BACK,
        A_KP_FREE,           // freie Eingabe
        A_KP_NONE,           // loeschen
        A_KP_AUTO,           // Squawk automatisch (TopSky)
        A_KP_ABLE,           // Intersection: "able for" statt zugewiesen (umschalten)
        A_KP_BASE = 1500,    // + Taste
        A_RT_TOK_BASE = 2000 // + Index: Punkt der Route antippen (bis 2399)
    };

    static LRESULT CALLBACK WndProcStatic(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    void ComputeLayout(int width, int height);
    void RebuildColumns();
    bool BayInView(int bay) const;
    int  ColumnOf(const std::string& cs) const;
    bool ColumnHas(int col, const std::string& cs) const;
    bool SplitColumns() const;
    bool AcceptsDrop(int col, const StripData& s) const;
    std::wstring GroupLabel(int group) const;
    const StripData& StripAt(int col, int i) const;
    void UpdateScale();
    void CreateFonts();
    void DeleteFonts();
    int  Sx(double v) const;
    void Invalidate();

    // Eingabe (Maus und Touch landen hier)
    void PointerDown(POINT p, bool touch);
    void PointerMove(POINT p);
    void PointerUp(POINT p);
    void PointerCancel();
    void Wheel(POINT pScreen, int delta);
    void ExecuteAction(int action);
    void ExecuteActionInner(int action);

    bool HitStrip(POINT p, int& col, int& index) const;
    int  HitCol(POINT p) const;
    int  HitButton(POINT p) const;
    int  DropIndex(int col, POINT p) const;
    int  StripPitch() const;
    RECT SlotRect(int col, int slot) const;
    void ClampScroll(int col);
    const StripData* FindStrip(const std::string& cs, int* bayOut = nullptr) const;

    // Zeichnen
    void DrawTopBar(HDC hdc);
    void DrawColumn(HDC hdc, int col);
    void DrawStrip(HDC hdc, const RECT& rc, const StripData& s, int bay, bool ghost);
    void DrawDetail(HDC hdc, int width, int height);
    void DrawEditor(HDC hdc, int width, int height);
    void OpenEditor(const std::string& callsign);
    void OpenIntEditor(const std::string& callsign);
    void OpenHsEditor(const std::string& callsign);
    void OpenDlTextEditor(const std::string& callsign);
    void OpenNewEditor();
    void DrawTrash(HDC hdc, int width, int height);
    void DrawDatalink(HDC hdc, int width, int height);
    void OpenDatalink(const std::string& cs);
    void OpenField(int field);
    void OpenFieldFor(const std::string& callsign, int field);
    void OpenFieldEditor(int field);
    void DrawPicker(HDC hdc, int width, int height);
    void DrawStandPicker(HDC hdc, int width, int height, const std::string& cur);
    void BuildStandGroups();
    std::vector<int> PickVisible() const;   // Indizes der Optionen im aktuellen Reiter
    void CloseEditor(bool commit);
    void EditKey(wchar_t c);
    static bool IsEditAction(int action);
    void FitText(HDC hdc, const RECT& rc, const std::wstring& text, HFONT normal, HFONT narrow, COLORREF color);
    void DrawButton(HDC hdc, const RECT& rc, const std::wstring& text, COLORREF bg, COLORREF fg, int action, bool active);

    IStripHost* m_host = nullptr;
    HWND        m_hwnd = nullptr;
    Board       m_board;

    // Skalierung
    UINT   m_dpi = 96;
    double m_zoom = 1.0;
    int    m_layout = 0;
    int    m_view = 0;
    int    m_rwyFilter = 0;
    bool   m_light = false;
    bool        m_wxRow = false;     // schmales Fenster: ATIS und METAR in einer zweiten Zeile oben
    std::string m_lastAtis;          // fuer die Hervorhebung, wenn der ATIS-Buchstabe wechselt
    long long   m_atisChanged = 0;   // Zeitpunkt des letzten Wechsels (Sekunden)
    double m_scale = 1.0;

    // Schriften
    HFONT m_fTitle = nullptr, m_fHeader = nullptr, m_fCallsign = nullptr, m_fCallsignSmall = nullptr,
          m_fStrip = nullptr, m_fStripSmall = nullptr, m_fStripTiny = nullptr, m_fLabel = nullptr, m_fDetailTitle = nullptr,
          m_fDetailLabel = nullptr, m_fDetailValue = nullptr, m_fButton = nullptr;

    // Layout-Cache (wird bei jedem Zeichnen und bei jeder Eingabe neu berechnet)
    int     m_width = 0, m_height = 0;
    RECT    m_topBar{};
    std::vector<Column> m_cols;
    int     m_scroll[kMaxCols] = {};
    int     m_stripH = 62, m_gap = 5, m_pad = 6;
    std::vector<Button> m_buttons;
    RECT    m_detailCard{};

    // Pointer-Zustand
    Mode        m_mode = M_NONE;
    bool        m_touch = false;
    POINT       m_downPt{};
    POINT       m_curPt{};
    POINT       m_grabOffset{};
    std::string m_dragCs;
    int         m_dragFromCol = -1;
    int         m_dragBay = -1;      // Bay des gezogenen Streifens
    int         m_dropCol = -1;
    int         m_dropIndex = -1;
    int         m_dragW = 0;
    int         m_scrollCol = -1;
    int         m_scrollStart = 0;
    bool        m_scrollByTrack = false;
    int         m_pressedAction = 0;

    // Detailansicht
    std::string m_detailCs;

    // Scratchpad-Editor
    bool        m_editing = false;
    std::string m_editCs;
    std::string m_editText;
    RECT        m_editCard{};
    HWND        m_prevForeground = nullptr;
    std::vector<std::pair<RECT, std::string>> m_typeCells; // Flugzeugtyp (Antippen = Streifen einfaerben)
    std::vector<std::pair<RECT, std::string>> m_pbCells;   // Push-Richtung (Antippen = Auswahl N/E/S/W)
    std::vector<std::pair<RECT, std::string>> m_tsatCells; // TSAT (Antippen = TOBT an vACDM)
    // Spaltenbreiten: Raender zwischen zwei Plaetzen (Ziehen aendert die Breite, gespeichert je Ansicht)
    struct SlotEdge { RECT hit; int left, right; };
    std::vector<SlotEdge> m_slotEdges;
    std::vector<int> m_slotWNow;       // aktuelle Breiten der Plaetze (Pixel)
    int  m_rsLeft = -1, m_rsRight = -1, m_rsW0 = 0, m_rsW1 = 0, m_rsF0 = 100, m_rsF1 = 100;
    std::map<std::string, int> m_colWLive;   // waehrend des Ziehens
    std::string SlotKey(int slot) const;
    int  SlotFactor(int slot) const;
    std::vector<std::pair<RECT, std::string>> m_spCells;   // SP-Felder der Streifen (Antippen = Editor)
    std::vector<std::pair<RECT, std::string>> m_clrCells;  // Freigabefelder T/O bzw. LDG (Antippen = umschalten)
    std::vector<std::pair<RECT, std::string>> m_respCells;    // Zustaendigkeits-Marken (Antippen = Liste)
    std::vector<std::pair<RECT, std::string>> m_depClrCells;  // Clearance-Kaestchen (Antippen = Freigabe)
    std::vector<std::pair<RECT, std::string>> m_nextCells;
    bool m_anyBlink = false;
    std::wstring m_nextRespLabel;                                  // etwas blinkt: alle 500 ms ganz neu zeichnen     // Anforderungs-Marke (geben / zuruecknehmen)    // Weiter-Taste (naechste Bay)
    int NextStep(const StripData& s, int bay, int& next) const;
    bool m_kpAble = false;                                   // Intersection-Tastenfeld: "able for"
    bool Outgoing(const StripData& s) const;
    bool HiddenByResp(const StripData& sd, int bay) const;
    bool Preview(const StripData& sd, int bay) const;
    bool RwyColMatch(const char* match, const StripData& sd, bool firstCol) const;
    std::vector<std::pair<RECT, std::string>> m_stdClrCells;  // Taxi In: bis zum Stand freigegeben (Haekchen)
    std::vector<std::pair<RECT, std::string>> m_intCells;  // INT-Felder (Antippen = Intersection-Editor)
    std::vector<std::pair<RECT, std::string>> m_hovCells;  // FREQ-Felder in der Luft (Antippen = weggeschickt)
    std::vector<std::pair<RECT, std::string>> m_hsCells;   // Hold-Short-Marken (Antippen = Editor)
    std::vector<std::pair<RECT, std::string>> m_taxiCells; // Rollweg-Marken (Antippen = Editor)
    std::vector<std::pair<RECT, std::string>> m_deiceCells;// Enteisungs-Marken (Antippen = weiterschalten)
    // Suche / Filter: nur Streifen, die zu allen Woertern passen (Callsign, Typ, Piste, SID, Stand, Platz, DEICE)
    std::string m_filter, m_filterPrev;
    bool FilterMatch(const StripData& s) const;
    void OpenSearch();
    bool EditNumeric() const;                               // Editor nimmt nur Ziffern: Zahlenfeld statt Tastatur
    // Groesse, Anordnung, Farbschema je Station
    struct ViewPreset { int zoomPct = -1, layout = -1, light = -1; };
    ViewPreset m_presets[VIEW_COUNT];
    bool m_perView = true;
    void StorePreset(int view);
    void ApplyPreset(int view);
    std::vector<std::pair<RECT, std::string>> m_rwyCells;  // RWY-Felder (Antippen = Hold-Short-Auswahl)
    std::vector<std::pair<RECT, std::string>> m_goneCells;  // FREQ uebergebener Streifen (Antippen = entfernen)
    std::vector<std::pair<RECT, std::string>> m_sqkCells;    // SQK-Felder (Antippen = Squawk-Tastenfeld mit Auto)
    std::vector<std::pair<RECT, std::string>> m_standCells;  // STAND fehlt/belegt (Antippen = Standauswahl)
    std::vector<std::pair<RECT, std::string>> m_parkCells;  // STAND rollender Ankuenfte (Antippen = geparkt)
    bool HandedOver(const StripData& s, int bay) const;      // an Ground/Apron uebergeben (in dieser Ansicht)
    bool CanPark(const StripData& s, int bay) const;         // rollende Ankunft: STAND antippen = geparkt
public:
    std::wstring ColTitle(int col) const;     // Titel einer Spalte der aktuellen Ansicht (mit Piste)
    std::wstring GroupEn(int group) const;    // "NORTH" / "SOUTH"
    void OpenStationMenu() { m_viewMenu = true; BuildStationItems(); }
    void OpenSettings() { m_setOpen = true; m_detailCs.clear(); m_trashOpen = false; Invalidate(); }
    int ColAccent(int col) const;
private:
    std::vector<std::pair<RECT, std::string>> m_dlCells;   // CPDLC/DCL-Marken (Antippen = CPDLC-Fenster)
    // StripBay Remote
    bool         m_remoteClient = false;
    std::wstring m_remoteInfo;
    std::wstring m_offlineDetail;
    RECT         m_offlineCard = {};
    std::wstring m_note;             // kurze Meldung (Remote: was das Plugin im EuroScope-Chat meldet)
    DWORD        m_noteUntil = 0;
    void DrawNoteAndOffline(HDC hdc, int width, int height);
    // Handschrift im Editor (Stift oder Finger)
    bool        m_inkMode = false;
    std::vector<std::vector<POINT>> m_inkStrokes;
    RECT        m_inkPad{};
    std::string m_inkMsg;            // Fehler der Erkennung (leer = keiner)
    void InkRecognize();
    void PasteClipboard();
    bool        m_editLower = false;     // Bildschirmtastatur in Kleinbuchstaben (Logon-Code)
    // Stationsauswahl (Dropdown): Ansicht und Piste in einem
    struct StationItem { int view; int filter; std::wstring label; };
    bool        m_viewMenu = false;
    bool        m_areaMenu = false;      // Bereich North / Combined / South (Dropdown)
    // Tastenfeld am Streifen bzw. Feld: nur die moeglichen Werte
    enum KpKind { KP_NONE = 0, KP_SQUAWK, KP_HS, KP_INT, KP_HDG, KP_HL, KP_PB, KP_TOBT, KP_TLSEQ };
    int         m_kpKind = KP_NONE;
    std::string m_kpCs;
    std::string m_kpText;                 // Squawk: bisher getippte Ziffern
    std::string m_kpGroup;                // Hold Short / Intersection: gewaehlter Buchstabe
    std::vector<std::string> m_kpOpts;    // alle moeglichen Werte
    std::vector<std::string> m_kpKeys;    // aktuelle Tasten (Wert bzw. Gruppe)
    RECT        m_kpAnchor{}, m_kpCard{};
    void OpenKeypad(int kind, const std::string& cs, POINT at);
    void KeypadKey(int action);
    void DrawKeypad(HDC hdc, int width, int height);
    // Einstellungsfenster: jede Schaltflaeche fuehrt einen ".strips ..."-Befehl aus
    bool        m_setOpen = false;
    RECT        m_setCard{};
    std::vector<std::string> m_setCmds;
    std::string m_editPrefix;            // Editor-Modus 6: Befehl, an den der Text angehaengt wird
    std::wstring m_editTitle;
    std::wstring m_stateBtnLabel;   // "Ground state: ..." im Detailfenster
    void DrawSettings(HDC hdc, int width, int height);
    RECT        m_areaBtn{};
    void DrawAreaMenu(HDC hdc, int width, int height);
    RECT        m_viewMenuCard{};
    RECT        m_viewBtn{};
    std::vector<StationItem> m_stationItems;
    void BuildStationItems();
    void DrawViewMenu(HDC hdc, int width, int height);
    std::wstring StationLabel() const;
    std::wstring AreaLabel(int group) const;
    bool        m_dlOpen = false;    // CPDLC-Fenster offen
    std::string m_dlCs;
    RECT        m_dlCard{};
    int         m_editMode = 0;      // 0 = Scratchpad, 1 = Intersection, 2 = Hold Short, 3 = neuer Streifen, 4 = Feld, 5 = CPDLC-Freitext, 6 = Einstellung, 7 = Suche
    bool        m_editNote = false;  // neuer Streifen als Notiz
    bool        m_editDesig = false; // neuer Streifen als Designator-Strip
    int         RwyStateOf(int col) const;   // Runway-Bay: 0 frei, 1 Caution, 2 Closed (gesperrt)
    bool        m_trashOpen = false; // Papierkorb offen
    RECT        m_trashCard{};
    int         m_editField = -1;    // Editor-Modus 4: welches Feld
    // Routen-Editor (Modus 4, F_ROUTE): Route als Punkte, einer davon wird bearbeitet
    std::vector<std::string> m_rtTok;
    int         m_rtSel = 0;
    std::string m_rtOrig;
    bool        RouteEditing() const;
    void        RouteSelect(int i);
    void        RouteAction(int action);
    std::string RouteJoined() const;
    void        DrawRouteBox(HDC hdc, const RECT& box);
    // Auswahlliste im Detailmenue
    bool        m_pickOpen = false;
    std::string m_pickCs;
    int         m_pickField = -1;
    std::vector<std::string> m_pickOpts;
    std::vector<std::string> m_pickTabs;   // Anfangsbuchstaben, wenn die Liste lang ist
    // Standauswahl: Auto, Direkt-Buttons (Apron 1, GAT), Kategorien (Apron 2, A-E, W, Other)
    struct StandGroup { std::wstring label; std::vector<int> idx; std::vector<std::wstring> shown; bool button = false;
                        std::string literal; };   // Button ohne passenden Stand: dieser Text
    std::string StandButtonValue(const StandGroup& g) const;
    bool        m_standMode = false;
    std::vector<StandGroup> m_standGroups;     // Reihenfolge: erst Buttons, dann Kategorien
    std::map<std::string, int> m_pickStatus;   // Stand -> 0 frei, 1 vergeben, 2 besetzt, 3 gesperrt
    int         m_pickTab = -1;            // -1 = alle
    int         m_pickPage = 0;
    // Hoehen (CFL, Final Altitude): eine Spalte zum Scrollen (Wischen, Mausrad, Pfeile)
    bool        PickScrollList() const;
    void        ClampPickScroll();
    int         m_pickScroll = -1;       // erste sichtbare Zeile (-1 = beim Oeffnen auf den Wert setzen)
    int         m_pickRowsVis = 1;
    int         m_pickRowPitch = 1;
    RECT        m_pickListRc{};
    int         m_pickDragStart = 0;
    bool        m_pickDragMoved = false;
    int         m_pickPerPage = 1;
    RECT        m_pickCard{};
    POINT       m_pickAnchor{ 0, 0 };   // kurze Listen erscheinen hier (Fingerposition)
    bool        m_editAssign = true; // Intersection: zugewiesen (true) oder able for

    // Vollbild
    bool m_fullscreen = false;
    bool m_topmost = false;
    RECT m_normalRect = { 100, 100, 1500, 800 };

    // ----- Zeitstrahl (Tower), nach der DFS-Planungsanzeige "WFF TWR-HMI" -----
    // Holding, Runway und Final gehen in einem Feld auf: waagerechte Zeitachse (Jetzt bei 1/3),
    // erste Piste oberhalb, zweite unterhalb, je Piste eine Reihe Abfluege (aussen) und Ankuenfte
    // (an der Achse). Was spaeter kommt oder keine Zeit hat, steht rechts als einzeilige Liste,
    // was laengst weg ist (gestartet / gelandet und noch nicht abgegeben) links (Hand Over Out).
    struct TlItem
    {
        std::string cs;
        int bay = -1, side = 0, lane = 0;   // lane 0 = Abfluege, 1 = Ankuenfte
        long long t = 0;                    // erwartete Bahnbelegung (UTC-Sekunden), 0 = keine Zeit
        int where = 0;                      // 0 Zeitstrahl, 1 spaeter / ohne Zeit (rechts), 2 Hand Over Out (links), 3 Notiz
        bool overdue = false;               // TTOT vorbei und noch nicht auf der Piste
        int order = 0;                      // Reihenfolge in der Bay (fuer Eintraege ohne Zeit)
        bool planned = false;               // Zeit per Ziehen bzw. Reihenfolge gesetzt (synchron)
        bool done = false;                  // gelandet bzw. gestartet (Zeit steht fest)
        bool est = false;                   // Abflug ohne TTOT/Slot: geschaetzte, gestaffelte Zeit
        long long earliest = 0;             // Schaetzung: fruehestens (Rollen bzw. am Rollhalt)
        std::string rwy;                    // Piste (normalisiert)
    };
    // Klickfelder im Label (DFS: Labelkopf = Status, Felder einzeln waehlbar)
    enum TlZoneKind { TLZ_BODY = 0, TLZ_STATUS, TLZ_CS, TLZ_TYPE, TLZ_TIME, TLZ_HS, TLZ_REMARK, TLZ_SID, TLZ_STAND, TLZ_INT, TLZ_RWY };
    struct TlZone { RECT rc; std::string cs; int kind; };
    TlPal TlPalette() const;
    int  TlZoneAt(POINT p, const std::string& cs) const;
    long long TlTimeAt(int x) const;
    long long TlPlanTarget() const;       // Ziehen entlang der Zeitleiter: neue Zeit (0 = keine)
    void DrawTlGhostPlan(HDC hdc, const RECT& ghost, TlItem& it);
    std::string m_tlHoverCs;              // Maus ueber einem Klickfeld (Rahmen)
    int  m_tlHoverKind = 0;
    int  m_tlPressZone = 0;
    struct TlLane { RECT rc{}; int side = 0, lane = 0; bool above = true; };
    bool TimelineActive() const;
    bool TlAbsorbed(int col) const { return col >= 0 && col < (int)m_tlAbs.size() && m_tlAbs[col] && TimelineActive(); }
    void BuildTlItems(long long now);
    void LayoutTimeline();
    void DrawTimeline(HDC hdc);
    void DrawTlLabel(HDC hdc, const RECT& body, int tipX, int apexY, int tipDir, const StripData& s, int bay,
                     const TlItem* it, bool ghost);
    void DrawTlRow(HDC hdc, const RECT& rc, const StripData& s, int bay, const TlItem& it);
    COLORREF TlStatus(const StripData& s, int bay, std::wstring& text) const;
    int  TlDropCol(POINT p) const;
    bool TlHit(POINT p, std::string& cs, RECT& rc) const;
    void TlTap(const std::string& cs, bool secondary, const RECT& anchor, int zone = 0);
    void DrawTlMenu(HDC hdc, int width, int height);
    long long TlX0() const;   // linker Rand der Zeitachse (UTC-Sekunden)
    bool TlSplitEddv() const;                       // Hannover: Zeitstrahl je Bahn (Nord / Sued), ohne geteilte Tower
    int  TlRwyGroup(const StripData& s) const;      // Seite eines Streifens (RWY_A, RWY_B, RWY_NONE)
    void TlSetGroup(const std::string& cs, int group);   // auf die andere Seite: Piste im Flugplan setzen
    int  TlRwyColFor(const StripData& s, int side) const;
    void TlEstimate();
    int  TlX(long long t) const;
    std::vector<char> m_tlAbs;            // je Spalte: geht im Zeitstrahl auf
    std::vector<int>  m_tlSideGroup;      // je Seite die Pistengruppe (RWY_A, RWY_B oder RWY_NONE)
    std::vector<int>  m_tlRwyCol;         // je Seite die Runway-Spalte (Status frei / Caution / Closed)
    std::vector<std::vector<int>> m_tlRwyCols;   // je Seite alle Runway-Spalten (Hannover: 09L/27R und 09C/27C)
    std::vector<TlItem> m_tlItems;
    std::vector<TlLane> m_tlLanes;
    RECT m_tlPanel{}, m_tlAxis{}, m_tlLeft{}, m_tlRight{};
    int  m_tlAx0 = 0, m_tlAx1 = 0;        // Zeitachse in Pixeln
    long long m_tlNow = 0;
    std::vector<std::pair<RECT, std::string>> m_tlHits;   // Labels und Listenzeilen (zuletzt gezeichnet = oben)
    std::map<std::string, long long> m_tlSeen;           // gelandet bzw. abgehoben seit (lokal)
    bool  m_tlPress = false;              // Druck begann auf einem Zeitstrahl-Label
    RECT  m_tlPressRc{};
    bool  m_secondary = false;            // Rechtsklick, Stifttaste/Radierer: Menue statt naechster Schritt
    DWORD m_downTick = 0;                 // langes Druecken (Touch) = Menue
    bool  m_tlMenu = false;               // Details als kleines Menue am Label
    RECT  m_tlMenuAnchor{};

    // Offscreen-Puffer
    HDC     m_memDC = nullptr;
    HBITMAP m_memBmp = nullptr;
    HBITMAP m_memOld = nullptr;
    int     m_memW = 0, m_memH = 0;
};
