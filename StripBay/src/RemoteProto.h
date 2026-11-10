// StripBay Remote - Protokoll zwischen Plugin (Server in EuroScope) und StripBay Remote (weiteres Windows-Geraet)
//
// Das Geraet ist nur Anzeige und Eingabe: Es bekommt denselben Zustand (Board) wie das Fenster in EuroScope
// und schickt jede Aktion ueber dieselbe Schnittstelle (IStripHost) zurueck. Alles laeuft damit durch den Code
// des Plugins - auch der Abgleich mit den anderen Lotsen. Hier gibt es keine Sockets: Hub und Link bekommen
// fertige Nachrichten und geben fertige Rahmen ab, das Netz (RemoteNet / RemoteApp) transportiert sie nur.
#pragma once

#include "Model.h"
#include "Wire.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace remote
{
    const uint32_t kMagic = 0x31524253;    // "SBR1"
    const int kProtocol = 2;               // bei Aenderung am Protokoll oder an Wire.h erhoehen
    const int kDefaultPort = 47810;        // TCP
    const int kDiscoveryPort = 47811;      // UDP: "STRIPBAY?" -> "STRIPBAY1 <port> <name>"
    const uint32_t kMaxFrame = 16u << 20;  // 16 MB

    enum Msg : uint8_t
    {
        M_HELLO = 1,   // Geraet -> Plugin: Magic, Protokoll, Code, Name, Version
        M_WELCOME,     // Plugin -> Geraet: Name, Version
        M_REFUSED,     // Plugin -> Geraet: Grund ("code", "version")
        M_BOARD,       // Plugin -> Geraet: kompletter Zustand
        M_CALL,        // Geraet -> Plugin: Aktion (IStripHost)
        M_REQ,         // Geraet -> Plugin: Anfrage mit Antwort (Auswahlliste, Stands)
        M_REPLY,       // Plugin -> Geraet: Antwort
        M_SOUND,       // Plugin -> Geraet: Ton abspielen
        M_NOTE,        // Plugin -> Geraet: Meldung (wie im EuroScope-Chat)
        M_PING,
        M_PONG,
    };

    enum Call : uint8_t
    {
        C_STRIP_MOVED = 1, C_STRIP_TAPPED, C_SET_GROUND_STATE, C_TOGGLE_CLEARANCE, C_TOGGLE_RWY_CLEARANCE, C_GO_AROUND,
        C_COMMAND, C_SET_STRIP_GROUP, C_DATALINK, C_SET_INTERSECTION, C_TOGGLE_HANDOVER, C_SET_HOLD_SHORT, C_CREATE_STRIP,
        C_RESTORE_STRIP, C_SET_FIELD, C_SET_SCRATCHPAD, C_REMOVE_STRIP,
        C_VIEW_FILTER,   // Ansicht / Bereich gewechselt: gilt fuer die ganze Sitzung (wer du bist)
    };

    enum Req : uint8_t { R_FIELD_OPTIONS = 1, R_STAND_STATUS };

    // Rahmen: 4 Byte Laenge (little endian), dann die Nachricht (erstes Byte = Art)
    std::string Frame(const std::string& payload);

    // Stueckweise ankommende Bytes zu ganzen Nachrichten zusammensetzen
    class FrameReader
    {
    public:
        void Feed(const char* data, size_t n);
        bool Next(std::string& payload);    // true = eine ganze Nachricht
        bool Bad() const { return m_bad; }  // Unsinn empfangen: Verbindung trennen
        void Clear() { m_buf.clear(); m_bad = false; }
    private:
        std::string m_buf;
        bool m_bad = false;
    };

    std::string Hello(const std::string& code, const std::string& name, const std::string& version);
    // Antwort auf HELLO lesen: true = angenommen (name/version vom Plugin), sonst reason
    bool ParseWelcome(const std::string& payload, std::string& name, std::string& version, std::string& reason);

    // Plugin-Seite: angemeldete Geraete, Aktionen an das Plugin weiterreichen
    class Hub
    {
    public:
        IStripHost* host = nullptr;
        std::function<void(int client, const std::string& frame)> send;
        std::function<void(int client)> drop;
        std::function<void(int view, int filter)> setViewFilter;
        std::function<void(const std::string& text)> log;
        std::function<void()> changed;           // Geraet an- oder abgemeldet
        std::string code;                        // Kopplungscode
        std::string name;                        // z. B. "PC-NAME - EDDB_TWR"
        std::string version;                     // Plugin-Version

        void Connected(int client, const std::string& addr, long long nowMs);
        void Received(int client, const std::string& payload, long long nowMs);
        void Closed(int client);
        void Board(const ::Board& b);            // an alle angemeldeten, nur wenn sich etwas geaendert hat
        void Sound(const std::string& tone);
        void Note(const std::string& text);
        void Tick(long long nowMs);              // stille Geraete trennen
        void DropAll();
        int  Count() const;                      // angemeldete Geraete
        std::vector<std::string> Names() const;

    private:
        struct Client { std::string addr, name; bool authed = false; long long lastRx = 0; };
        std::map<int, Client> m_c;
        std::string m_board;                     // zuletzt gesendet (kodiert)
        void SendTo(int client, const std::string& payload);
        void Broadcast(const std::string& payload);
        void Dispatch(int client, wire::Reader& r);
        void Request(int client, wire::Reader& r);
    };

    // Geraete-Seite: IStripHost, der alles an das Plugin schickt
    class Link : public IStripHost
    {
    public:
        std::function<void(const std::string& frame)> send;
        // auf die Antwort zu Anfrage id warten (der Transport liest weiter und legt andere Nachrichten ab)
        std::function<bool(uint32_t id, std::string& reply)> wait;
        // Befehle, die nur dieses Geraet betreffen (Zoom, Hell/Dunkel, Vollbild ...): true = hier erledigt
        std::function<bool(const std::string& line)> localCommand;
        std::function<void()> windowState;       // Fenster-Einstellungen geaendert (speichern, Ansicht melden)
        bool online = false;

        void SendViewFilter(int view, int filter);
        void Ping();

        void StripMoved(const std::string& callsign, int bay, int index) override;
        void StripTapped(const std::string& callsign) override;
        void SetGroundState(const std::string& callsign, int groundCmd) override;
        void ToggleClearance(const std::string& callsign) override;
        void ToggleRunwayClearance(const std::string& callsign) override;
        void GoAround(const std::string& callsign) override;
        void Command(const std::string& line) override;
        void SetStripGroup(const std::string& callsign, int group) override;
        void Datalink(const std::string& callsign, int action, const std::string& text) override;
        void SetIntersection(const std::string& callsign, const std::string& text, bool assigned) override;
        void ToggleHandover(const std::string& callsign) override;
        void SetHoldShort(const std::string& callsign, const std::string& text) override;
        void CreateStrip(const std::string& text, bool asNote, int bay) override;
        void RestoreStrip(const std::string& id) override;
        std::vector<std::string> FieldOptions(const std::string& callsign, int field) override;
        std::map<std::string, int> StandStatus(const std::string& callsign) override;
        void SetField(const std::string& callsign, int field, const std::string& value) override;
        void SetScratchpad(const std::string& callsign, const std::string& text) override;
        void RemoveStrip(const std::string& callsign) override;
        void WindowStateChanged() override;

    private:
        uint32_t m_nextId = 1;
        void Out(const std::string& payload);
    };
}
