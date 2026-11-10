// StripBay - CPDLC und DCL ueber das Hoppie-ACARS-Netz
//
// Schnittstelle: https://www.hoppie.nl/acars/system/connect.html
//   ?logon=<Code>&from=<Station>&to=<Ziel>&type=<Typ>&packet=<Inhalt>
// Antworten: "ok", "ok {CS typ {inhalt}} {...}" (poll) oder "error {Text}".
// CPDLC-Inhalt: "/data2/<MIN>/<MRN>/<RA>/<Text>", RA = WU, AN, R, NE, Y, N.
//
// Der HoppieClient laeuft in einem eigenen Thread: Pollen alle 45-75 s
// (nach dem Senden eine Weile alle 20 s), Senden mit mindestens 2 s Abstand.
#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct HoppieMsg
{
    std::string from;     // Rufzeichen des Absenders
    std::string type;     // "cpdlc", "telex", ...
    std::string packet;   // Inhalt
};

// Ergebnis einer gesendeten Nachricht (fuer Fehlermeldungen im Chat)
struct HoppieSent
{
    std::string to, type, packet;
    bool ok = false;
    std::string error;
};

// "ok {DLH1 cpdlc {/data2/5//Y/REQUEST LOGON}} {...}" zerlegen. false bei "error ..." (error = Text)
bool ParseHoppieReply(const std::string& body, std::vector<HoppieMsg>& out, std::string& error);

// "/data2/12/5/WU/CLIMB TO @FL100@" -> min 12, mrn 5, ra "WU", text "CLIMB TO @FL100@" (mrn -1 = leer)
bool ParseCpdlc(const std::string& packet, int& min, int& mrn, std::string& ra, std::string& text);

// CPDLC-Text fuer die Anzeige: "@" weg, Leerzeichen zusammenfassen
std::string CpdlcDisplay(const std::string& text);

// Prozent-Kodierung fuer URL-Parameter
std::string UrlEncode(const std::string& s);

class HoppieClient
{
public:
    ~HoppieClient();

    void Start();
    void Stop();
    bool IsRunning() const { return m_thread.joinable(); }

    // Logon-Code (geheim) und eigene Station, z. B. "EDDB"
    void Configure(const std::string& logon, const std::string& station);
    std::string Station();

    void Send(const std::string& to, const std::string& type, const std::string& packet);
    // eine Weile schneller pollen (Antwort erwartet)
    void PollSoon();

    std::vector<HoppieMsg> TakeIncoming();
    std::vector<HoppieSent> TakeSent();

    bool Connected();
    std::string Status();

private:
    void Run();
    bool Request(const std::string& to, const std::string& type, const std::string& packet,
                 std::string& body, std::string& error);

    std::thread             m_thread;
    std::atomic<bool>       m_stop{ false };
    std::mutex              m_mx;
    std::condition_variable m_cv;

    // durch m_mx geschuetzt
    std::string             m_logon, m_station;
    std::deque<HoppieMsg>   m_out;        // zu senden (from = Ziel)
    std::vector<HoppieMsg>  m_in;
    std::vector<HoppieSent> m_sent;
    long long               m_fastUntil = 0;
    long long               m_lastOk = 0;
    std::string             m_lastError;
    bool                    m_connected = false;
    bool                    m_pollNow = false;
};
