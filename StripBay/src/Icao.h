// StripBay - ICAO-Daten aus dem Sektorpaket
//
// EuroScope zeigt Airline-Rufzeichen und Flughafennamen aus zwei Textdateien,
// die in jedem Sektorpaket liegen:
//   ICAO_Airlines.txt   ICAO <Tab> Name <Tab> Rufzeichen <Tab> Land
//   ICAO_Airports.txt   ICAO <Tab> Name <Tab> Land
// Die Plugin-Schnittstelle gibt diese Daten nicht heraus. StripBay sucht die
// Dateien deshalb selbst (im Hintergrund) und liest sie ein.
#pragma once

#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Eine Parkposition aus GRpluginStands.txt (Ground Radar Plugin)
struct StandInfo
{
    double lat = 0, lon = 0, radius = 0;              // Kreis (Meter) fuer "besetzt"
    bool   hasPos = false;
    std::vector<std::pair<double, double>> poly;      // alternativ COORD-Polygon
    std::vector<std::string> blocks;                  // BLOCKS: wird gesperrt, wenn dieser Stand belegt/vergeben ist
    std::string wtc, use;                             // WTC:LM, USE:AC (leer = alle)
    std::vector<std::string> airlines;                // CALLSIGN:DLH,EWG (leer = alle)
    int    priority = 0;                              // PRIORITY:-3..+3
    bool   manual = false, area = false;              // MANUAL (nicht automatisch), AREA (mehrere Flieger)
};

struct IcaoTables
{
    std::map<std::string, std::string> airlines;   // DLH -> LUFTHANSA
    std::map<std::string, std::string> airports;   // EDDF -> FRANKFURT/MAIN
    std::map<std::string, std::vector<std::string>> stands;   // EDDB -> A01, A02 ... (Ground Radar Plugin)
    std::map<std::string, std::map<std::string, StandInfo>> standInfo;   // EDDB -> A01 -> Lage, BLOCKS ...
    std::wstring airlinesPath, airportsPath, standsPath;
};

class IcaoLoader
{
public:
    ~IcaoLoader();

    // Sucht in den angegebenen Ordnern (rekursiv, begrenzt) und laedt die Dateien
    void Start(const std::vector<std::wstring>& roots);
    void Stop();

    // true genau einmal, wenn das Laden fertig ist
    bool TryTake(IcaoTables& out);

private:
    void Run(std::vector<std::wstring> roots);

    std::thread       m_thread;
    std::atomic<bool> m_stop{ false };
    std::mutex        m_mx;
    bool              m_ready = false;
    IcaoTables        m_result;
};

// Gesprochenes Rufzeichen: "DLH4AB" -> "LUFTHANSA 4AB",
// Kennzeichen ohne Airline: "DIABC" -> "DELTA INDIA ALFA BRAVO CHARLIE"
std::string SpokenCallsign(const std::string& callsign, const std::map<std::string, std::string>& airlines);

// "FRANKFURT/MAIN" -> "Frankfurt/Main"
std::string NiceName(const std::string& upper);

// Dateien einlesen (fuer Tests auch direkt nutzbar)
bool LoadAirlines(const std::wstring& path, std::map<std::string, std::string>& out);
// ganze Textdatei lesen / schreiben (UTF-8 bzw. wie sie ist)
bool ReadTextFileW(const std::wstring& path, std::string& text);
bool WriteTextFileW(const std::wstring& path, const std::string& text);
bool LoadAirports(const std::wstring& path, std::map<std::string, std::string>& out);
// GRpluginStands.txt: Zeilen "STAND:EDDB:A01" (danach Koordinaten), natuerlich sortiert
bool LoadStands(const std::wstring& path, std::map<std::string, std::vector<std::string>>& out,
                std::map<std::string, std::map<std::string, StandInfo>>* info = nullptr);
// Inhalt der Datei auswerten (auch fuer Tests)
bool ParseStandsText(const std::string& text, std::map<std::string, std::vector<std::string>>& out,
                     std::map<std::string, std::map<std::string, StandInfo>>* info = nullptr);
// "N052.21.38.123" bzw. "52.3606" -> Grad (Sueden/Westen negativ); false wenn unlesbar
bool ParseGrpCoord(const std::string& s, double& deg);
