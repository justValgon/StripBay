# StripBay

Touch-Flugstreifen für **EuroScope**: ein eigenes Fenster mit elektronischen Streifen für Delivery, Apron, Ground und Tower. Fertige Profile für Berlin **EDDB** (Nord/Süd getrennt), Leipzig **EDDP** und Hannover **EDDV**.

Autoren: Luis Korb, Sören Pflug · aktuelle Version: **4.1.0**

![Details mit Verlauf](docs/img/vorschau-details-verlauf.png)

## Was es kann

- **Bays und Drag and Drop:** Jede Verschiebung setzt den Ground State in EuroScope, den auch TopSky, GRP und andere Lotsen sehen. Ändert jemand den Status woanders, wandert der Streifen mit.
- **Einheitliche Übergabe:** Wer einen Streifen bearbeitet, ist zuständig. Die Übergabe wird der nächsten Stelle angeboten und von ihr angenommen. Ablehnen schickt den Streifen zurück.
- **Gemischter Betrieb:** Nutzt eine besetzte Stelle StripBay nicht, bleibt die Kette nicht hängen. Zum Beispiel Delivery und Tower mit StripBay, Ground ohne.
- **Zeitstrahl nach DFS-Vorbild** für Tower: ganze Labels als Taste, Menü per Langdruck, Tag- und Nachtfarben.
- **Verlauf je Streifen:** Zuständigkeit, Freigaben, Ground State und Übergabe mit Uhrzeit.
- **Runway-Bay**, Hold Short, Start- und Landefreigabe, nächste Frequenz, METAR und ATIS.
- **Synchron zwischen Lotsen** über das Scratchpad, ohne Server.
- **StripBay Remote:** der Streifen-Bildschirm auf einem zweiten Windows-Gerät, z. B. ein Touchscreen.

![Zeitstrahl](docs/img/vorschau-timeline.png)

## Installation

1. Unter [Releases](../../releases) die neueste `StripBay-X.Y.Z.zip` laden und entpacken.
2. In EuroScope: **OTHER SET → Plug-ins… → Load** und `StripBay.dll` wählen.
3. Das Fenster auf den Touchscreen ziehen und **Vollbild** tippen.

EuroScope ist 32 Bit, die DLL ist deshalb x86.

**Alle Lotsen und das Remote-Gerät brauchen dieselbe Hauptversion** (die erste Zahl). Unterschiede bei der zweiten oder dritten Zahl sind unkritisch.

## Selbst bauen

- **GitHub Actions:** Jeder Push auf `main` baut die DLL (Actions → neuester Lauf → Artifacts). Ein Tag `vX.Y.Z` legt zusätzlich ein Release an.
- **Auf dem eigenen PC:** `INSTALLIEREN.bat` doppelklicken. Es sucht das SDK, baut Release/x86 und installiert nach `%APPDATA%\EuroScope\Plugins\StripBay`.
- **Von Hand:** `StripBay.sln` in Visual Studio 2022 öffnen, **Release / x86** wählen.

`StripBay/sdk/` enthält `EuroScopePlugIn.h` und `EuroScopePlugInDll.lib` aus dem EuroScope-SDK.

## Dokumentation

- [Handbuch](docs/HANDBUCH.md): alle Funktionen, Bedienung, Befehle
- [Änderungsverlauf](docs/CHANGELOG.md)
- [StripBay Remote](Remote/LIESMICH.txt)

## Fehler melden

Bitte als [Issue](../../issues/new/choose) mit einer passenden Kategorie. Nützlich sind: Version, Profil (EDDB/EDDP/EDDV), Rolle (DEL/GND/TWR/APN), was du getan hast und was passiert ist, ein Screenshot.

## Lizenz

[CC0 1.0](LICENSE)
