# StripBay – Handbuch

Autoren: Luis Korb, Sören Pflug
Änderungen je Version stehen im [Änderungsverlauf](CHANGELOG.de.md). Die Kurzübersicht steht in der [README](../README.md).


EuroScope-Plugin mit eigenem Touch-Fenster für elektronische Flugstreifen, mit fertigen Profilen für Berlin (EDDB), Leipzig (EDDP) und Hannover (EDDV). Die Streifen liegen in Bays und lassen sich per Drag and Drop verschieben. Jede Verschiebung setzt den Ground State in EuroScope, den auch TopSky, das Ground Radar Plugin und andere Lotsen sehen. Ändert jemand den Status woanders (TopSky-Label, Departure List, anderer Lotse), wandert der Streifen automatisch mit.

## Funktionen

- Fünf Ansichten:
  - **Delivery:** Pending, On Frequency, Startup
  - **Apron:** Startup, Pushback, Taxi, An Ground, Taxi In
  - **Ground:** Startup, Pushback, Taxi Out, Departing Tower, Taxi In
  - **Tower:** Taxi / Holding, Runway, Final, Taxi In
  - **Alle Bays:** Pending, On Frequency, Startup, Pushback, Taxi / Holding, Runway, Final, Taxi In
- **Runway-Bay:** Line-up, Takeoff und Landing in einer Bay. Abflüge und Ankünfte auf der Piste stehen in einer gemeinsamen, frei sortierbaren Reihenfolge. **Start- und Landefreigabe** hakt man auf dem Streifen ab (Feld **T/O** bzw. **LDG**), siehe [Runway](#runway-bay-und-freigaben)
- **Nächste Frequenz** auf jedem Streifen (Feld FREQ), für Berlin nach festen Regeln, siehe [Nächste Frequenz](#nächste-frequenz)
- **Ankünfte** erscheinen im Anflug in Final (Tower) und nach der Landung in Taxi In (Ground). Ihr Streifen zeigt die **Parkposition** aus dem Ground Radar Plugin
- **Getrennte Pisten (Berlin):** Runway, Final, Holding und die Übergabe an Tower getrennt nach Nord (06L/24R) und Süd (06R/24L), nach der Piste im Tag. Beide Pisten untereinander oder nur eine, siehe [Berlin](#berlin-zwei-getrennte-pisten)
- **Hold Short** auf jedem Streifen eintragen (rote Marke "HS V1"), siehe [Hold Short](#hold-short)
- **Obere Leiste mit METAR und ATIS-Buchstabe** des Platzes, siehe [METAR und ATIS](#metar-und-atis)
- **Hell und dunkel:** helles Farbschema angelehnt an Tower-Streifensysteme (DFS-Stil), Button oben oder `.strips hell` / `.strips dunkel`
- **Streifen erstellen** (Button **+ Streifen**) für jeden Flieger mit Flugplan, ohne Flugplan als **Notiz-Streifen** (z. B. Fahrzeug, Pistenkontrolle). **Recover** holt entfernte und weggeschickte Streifen per Antippen zurück, siehe [Streifen erstellen](#streifen-erstellen-und-recover)
- Drag and Drop zwischen und innerhalb der Bays (Reihenfolge frei sortierbar)
- Antippen öffnet die Details und wählt den Flieger in EuroScope an (ASEL). **In den Details ist jeder Wert änderbar**: Feld antippen, aus einer Liste wählen (Stands, SIDs, STARs, Pisten, Höhen ...) oder eintippen, siehe [Details bearbeiten](#details-bearbeiten)
- **Scratchpad schreiben:** SP-Feld auf dem Streifen antippen (oder in den Details), Bildschirmtastatur oder echte Tastatur
- In den Details stehen Rufzeichen und Zielflughafen ausgeschrieben, z. B. "LUFTHANSA 4AB nach London/Heathrow" (bei Ankünften "von ...")
- **TSAT** aus CDM auf dem Streifen (statt EOBT), grün hinterlegt, solange die aktuelle Zeit innerhalb TSAT ±5 Minuten liegt
- Details, oben in der Reihenfolge der Clearance: SID, Initial Altitude, Squawk (der zugewiesene aus dem Plan), danach Final Altitude, Runway, TSAT, EOBT, Flugregeln, Typ/Wake, Ausweichflughafen, Ground State, Clearance, Scratchpad, Funk, zugewiesenes Heading/Speed, GS/ALT, Route, Remarks
- Buttons in den Details: **Ground state** (ein Button, öffnet die Liste der Status), Übergabe (An Tower / An Ground / Weggeschickt), Runway und Landefreigabe bei Ankünften, Clearance-Flag setzen/entfernen, Streifen entfernen
- Streifen wie auf Papier: Farbbalken nach Flugregeln (blau IFR, grün VFR, orange Y/Z), **RWY steht immer ganz links.** Abflüge: RWY, INT (Intersection), SID, SQK, CFL, TSAT, FREQ, SP, CLR-Kästchen, Badges TO / TWR (Line-up hat seit 2.0.32 keine eigene Marke mehr). Abflüge auf leicht blauem, Ankünfte auf cremefarbenem Papier (in beiden Farbschemata). Ankünfte: im Anflug mit RWY, STAND (Parkposition, groß), CFL, SQK, FREQ, SP, am Boden mit RWY, STAND, SQK, FREQ, SP (die STAR steht nur noch in den Details), Badges ARR / CTL / TXI. **CFL** zeigt immer nur die freigegebene Höhe aus dem Tag (oder ILS bzw. VIS bei Anflugfreigabe), nie die aktuelle Höhe. SQK zeigt nur den zugewiesenen Code und wird rot, wenn der Transponder etwas anderes sendet
- Für Touchscreens gebaut: das Fenster nimmt EuroScope nie den Tastaturfokus weg, Touch wird direkt verarbeitet (kein Rechtsklick durch langes Drücken), scharf auf hochauflösenden Displays
- Vollbild auf dem Monitor, auf dem das Fenster gerade liegt, Zoom, Bays nebeneinander oder in zwei Reihen

## Bauen

### Einfach: Doppelklick auf `INSTALL.bat`

Der Installer erledigt alles auf deinem Windows-PC:

1. Er holt `EuroScopePlugIn.h` und `EuroScopePlugInDll.lib`: entweder direkt neben dem Skript (einfach dort hineinlegen) oder aus deinem EuroScope-Ordner (Unterordner `PlugInEnvironment`). Findet er nichts, fragt er nach dem Ordner.
2. Es sucht den Microsoft-C++-Compiler. Fehlt er, bietet es an, die kostenlosen **Build Tools für Visual Studio 2022** mit "Desktopentwicklung mit C++" über winget zu installieren (einige GB, dauert ein paar Minuten).
3. Es baut Release/x86 (`StripBay.dll` liegt danach auch neben dem Skript).
4. Es installiert die DLL nach **`%APPDATA%\EuroScope\Plugins\StripBay\StripBay.dll`** und öffnet den Explorer mit der Datei markiert. Ist StripBay gerade in EuroScope geladen, erst EuroScope schließen.

Klappt der Build nicht, bleiben die Fehlermeldungen im Fenster stehen.

**Wo liegen die SDK-Dateien?** Meist im Programmordner von EuroScope (dort, wo `EuroScope.exe` liegt), Unterordner `PlugInEnvironment`, manchmal auch in `%APPDATA%\EuroScope\PlugIn`. Beides findet der Installer selbst. Der Installer sucht selbst in den üblichen Ordnern, fragt ein laufendes EuroScope nach seinem Pfad und durchsucht danach Programme und Benutzerordner. Den Programmordner findest du per Rechtsklick auf die EuroScope-Verknüpfung → "Dateipfad öffnen".

**SDK-Dateien fehlen:** Gibt es bei dir keinen Ordner `PlugInEnvironment`, wurde EuroScope wahrscheinlich nicht mit dem offiziellen Installer von euroscope.hu eingerichtet (z. B. nur entpackt oder kopiert). Dann EuroScope mit dem Installer neu installieren, Einstellungen und Sektorpakete in `%APPDATA%\EuroScope` bleiben dabei erhalten. Alternativ die beiden Dateien `EuroScopePlugIn.h` und `EuroScopePlugInDll.lib` von Hand nach `StripBay\sdk\` legen und den Installer erneut starten.

### Von Hand in Visual Studio

1. Visual Studio 2022 (oder neuer) mit der Arbeitslast **Desktopentwicklung mit C++**.
2. `EuroScopePlugIn.h` und `EuroScopePlugInDll.lib` nach `StripBay/sdk/` kopieren. Beide liegen im EuroScope-Ordner unter `PlugInEnvironment`.
3. `StripBay.sln` öffnen, oben **Release** und **x86** wählen, **Erstellen**.
4. Ergebnis: `bin/Release/StripBay.dll`

EuroScope ist 32 Bit, deshalb nur x86/Win32.

## In EuroScope laden

1. **OTHER SET → Plug-ins... → Load** und `StripBay.dll` auswählen (nach dem Installer: `%APPDATA%\EuroScope\Plugins\StripBay\StripBay.dll`).
2. Das StripBay-Fenster geht auf. Auf den Touchscreen ziehen und oben **Vollbild** tippen.
3. Beim Beenden von EuroScope die Plugin-Einstellungen speichern lassen, dann merkt sich StripBay Monitor, Vollbild, Zoom, Ansicht und Layout.

## Design und Bedienung (ab 2.0.21)

**Design:** flach und ruhig, angelehnt an die DFS-Streifenanzeige: blaue Bays, graue Kopfleisten, flache Tasten mit feinem Rand, keine bunten Pillen mehr. **Hell ist jetzt Standard**; der dunkle Modus hat dieselbe Ordnung (dunkelblaue Bays, graue Leisten, gedämpftes Papier). Umschalten im Einstellungsfenster oder mit `.strips theme light|dark`.

**Menüs dort, wo man tippt:**
- **Änderungsmenü** (Streifen antippen): rechts angedockt über die ganze Höhe, die Bays bleiben sichtbar.
- **Kurze Listen** (Höhen, Pisten, SIDs …) erscheinen als kleine Felder übereinander direkt neben dem Finger, Werte laufen von oben nach unten. Nur lange Listen (Stands) öffnen das große Fenster mit Reitern.
- **Squawk:** Tastenfeld nur mit **0–7**, **←**, **C** (abbrechen) und **OK** (bei 4 Ziffern).
- **Hold Short** direkt am Streifen (RWY-Feld oder rote HS-Marke antippen): Tastenfeld nur mit den möglichen Haltepunkten. Erst der Buchstabe, z. B. **V**, dann **1 · 2 · 3 · 4 · C**. Einzelne Punkte (B, C, F, G, T) und die Pisten sind sofort wählbar. Dazu **Text** (freie Eingabe), **None** (löschen), **C**.
- **Intersection** (INT-Feld antippen): nur die Intersections der Piste des Fliegers: Südbahn **M1–M9**, Nordbahn **L1–L8** und **K1–K4, K6**. **Text** öffnet die freie Eingabe (mit "Able for").
- Listen ändern: `.strips holdshort V1 V2 V3 V4 VC P1 P2 B C F G T`, `.strips intersections north|south LISTE`, `reset` für die Standardwerte (auch im Einstellungsfenster).

**Weiteres:** Die Transition Altitude ist fest 5000 ft (nicht mehr einstellbar). Das Feld TAKEOFF CLEARANCE ist aus dem Änderungsmenü entfernt (die Startfreigabe bleibt auf dem Streifen). Uhrzeiten haben immer vier Stellen (0853, nicht 853). 122.800 heißt **Advisory**.

## Einstellungsfenster (ab 2.0.20)

Button **Settings** oben (statt Hell/Dunkel) oder `.strips settings`. Darin: Farbschema, Layout, Größe, Fenster (immer im Vordergrund, Vollbild), getrennte Pisten, Taxi In nach dem Verlassen der Piste, Status beim Ziehen auf die Runway (LINEUP/DEPA), Entfernung für Ankünfte, Transition Altitude, Wetter, vACDM, CPDLC (an/aus, Logon-Code, Station, Auto-Logon, Ton), Stands ohne Apron und die Hold-Short-Liste. Texte gibst du über die Bildschirmtastatur oder den Stift ein. Jede Schaltfläche führt den passenden `.strips`-Befehl aus, gespeichert wird wie bisher mit den EuroScope-Einstellungen.

## Aufteilung der Ansichten (ab 2.0.15)

Weniger ist mehr: breit sind nur die Spalten, in denen gearbeitet wird; Übergabe-Spalten sind schmal. Bay-Namen sind durchgehend **englisch**, Pistenspalten tragen die **aktive Piste** im Namen, wie sie aus dem Runway-Dialog von EuroScope gelesen wird (z. B. **RUNWAY 25R**, **HOLDING 25L**, **DEPARTING TOWER 25R**; ist keine Piste aktiv, steht NORTH bzw. SOUTH dort). Was übergeben ist, entfernt ein **Tipp auf das orange FREQ-Feld** (zurück über Recover).

| Ansicht | Spalten |
|---|---|
| **Tower** | je Piste eine Reihe **HOLDING · RUNWAY · FINAL** (erste Piste oben, zweite unten), rechts schmal **TO GROUND** für beide Pisten |
| **Ground** (Apron online) | erste Spalte oben **TAXI OUT**, darunter klein **STARTUP** (nur Flieger von Stands ohne Apron: GAT, Apron 1, W-Stands); dann **DEPARTING TOWER** (je Piste, schmaler) **· TAXI IN · TO APRON** (schmal, TXIN) |
| **Ground + Apron** (niemand auf 129.605 / 121.855) | **STARTUP · PUSHBACK · TAXI OUT · DEPARTING TOWER · TAXI IN** (bis zum Stand) |
| **Apron** | **STARTUP · PUSHBACK · TAXI · TO GROUND** (schmal) **· TAXI IN**, jeweils ohne die Ground-Stands |
| **Apron + DEL** (kein Delivery online) | vorne zusätzlich **DELIVERY** (Pending und On Frequency) |
| **Delivery**, **All Bays** | wie bisher |

**Station und Bereich getrennt (ab 2.0.17):** Oben wählst du im **Dropdown** die Station (Delivery, Apron, Ground, Tower, All Bays). Rechts daneben, bei getrennten Pisten, ein zweites **Dropdown** für den Bereich: **Combined · North 25R · South 25L** (mit der aktiven Piste). Der Bereich bleibt beim Stationswechsel erhalten, z. B. erst "South", dann zwischen Ground und Tower wechseln. Bei Ground zeigt North/South in allen Spalten nur die Flieger dieser Piste, bei Tower nur die Reihe der Piste.

**Jeder Streifen nur einmal (ab 2.0.19):** Ein Flieger ohne erkannte Piste steht bei "Combined" nur in der oberen Pistenreihe (RWY-Feld orange mit "?"), bei North/South in der gewählten. Notiz-Streifen gehören zu der Piste, in deren Spalte du sie legst. Intern wird die Reihenfolge jede Sekunde geprüft, sodass kein Streifen doppelt erscheinen kann.

**Sprache:** Die Oberfläche und die Meldungen im EuroScope-Chat sind ab 2.0.17 durchgehend englisch. Die alten deutschen Befehle (`.strips hell`, `neu`, `notiz`, `zurück`, `piste` …) funktionieren weiter, dazu die englischen (`light`, `dark`, `new`, `note`, `restore`, `runway`, `on`, `off`, `north`, `south`, `both`).

**Stands ohne Apron** (Startup bei Ground): ab Werk `W*`. GAT und Apron 1 bitte ergänzen, z. B. `.strips gndstands W*,G*,A1*` (`*` = beliebig; `reset` = nur W). Diese Flieger erscheinen bei Ground in STARTUP und bei Apron nicht.

**Parken (ab 2.0.33):** Die grüne Marke **P** im STAND-Feld erscheint erst, wenn die Ankunft **am Gate angekommen** ist: am Boden, **langsamer als 3 kt** und – wenn der Stand in `GRpluginStands.txt` eine Lage hat – in ihrem Standkreis. Nur ein Tipp auf das **P** setzt PARK und entfernt den Streifen. Der Rest des STAND-Feldes öffnet die Standauswahl. Setzt jemand anderes PARK (anderer Lotse, GRP), bleibt der Streifen bei dir stehen, bis du selbst parkst. Aus Versehen? In Recover ("parked") zurückholen setzt wieder TXIN.

**Synchron über alle Lotsen:** Alles, was den Streifen bei anderen verschiebt, geht über EuroScope an alle mit StripBay: Ground States, Clearance-Flag, CTL/HOV/Intersection (vSID), Hold Short, Durchstarten, Stand (GRP) und jetzt auch die Übergaben **TO TOWER / TO GROUND** bei Abflügen (`.SB_XF_…`, gelten solange der Flieger rollt) und die Lage der Ankünfte **auf der Piste / an Ground abgegeben / zurück im Anflug** (`.SB_AP_…`). Seit 2.0.24 auch die **Zuständigkeit** (DEL / APN / GND / TWR mit Seite, `.SB_RS_…`). Nur lokal bleiben deine Ansicht, der Pistenfilter und weggetippte Streifen.

## Spalten und Ground States

| Spalte | Abflüge | Ankünfte |
|---|---|---|
| Pending | kein Status (NSTS) | |
| On Frequency | ONFREQ | |
| Startup | ST-UP | |
| Pushback | PUSH | |
| Taxi Out | TAXI | |
| An Tower (Ground: Departing Tower, Tower: Taxi / Holding) | an Tower übergeben, Status bleibt TAXI (nur lokal) | |
| Runway | LINEUP, mit Startfreigabe DEPA | auf der Piste, mit Landefreigabe CTL-Flag von vSID |
| Final | | im Anflug, kein Status |
| Taxi In GND | | gelandet, noch bei Ground (kein Status) |
| Taxi In Apron | | an Apron übergeben: Ground State **TXIN** |

Gesetzt wird der Status über die Scratchpad-Kommandos von EuroScope. Ein vorhandener Scratchpad-Eintrag bleibt dabei erhalten. Ankünfte gehen nur in Final, Runway, Taxi In GND und Taxi In Apron, Abflüge nie nach Final oder Taxi In.

**Ground-Ansicht:** Ziehen nach **Departing Tower** ist nur die Übergabe an den Tower, **keine Takeoff-Clearance**. Der Ground State bleibt, der Streifen bekommt das Badge **TWR**. In den Details gibt es dort statt Line-up und Startfreigabe den Button **An Tower**. Nimmt der Tower den Flieger auf die Runway, bleibt er in Departing Tower (bei Takeoff mit Badge TO). Ankünfte erscheinen dort nie.

**Tower-Ansicht:**
- **Taxi / Holding** zeigt alle rollenden Abflüge, die an dich übergebenen oben (Badge **TWR**). Die Übergabe ist nur lokal: Ist Ground ein anderer Lotse, siehst du alle Flieger mit TAXI.
- Abflüge bleiben nach dem Start stehen, bis du sie **weggeschickt** hast, siehe [Weggeschickt](#weggeschickt-abflüge-nach-dem-start).
- Ankünfte erscheinen ab 15 NM vor dem Platz in **Final** (`.strips arr` ändert die Entfernung).
- Runway und Freigaben: siehe nächster Abschnitt.
- In der **Ground-Ansicht** erscheinen gelandete Ankünfte immer direkt in Taxi In, damit Ground sie sieht, auch wenn der Tower sie noch auf der Runway hat.

**Übergabe Ground → Apron bei Ankünften (ab 2.0.7):** läuft über den Ground State **Taxi (TXIN)** und ist damit für alle Lotsen synchron.
- **Ground** hat die Spalten **TAXI IN** (seine Inbounds) und **AN APRON**. Ziehen nach An Apron setzt TXIN. Zurückziehen nach Taxi In nimmt TXIN wieder weg.
- **Apron** sieht die Inbounds noch bei Ground in **INBOUND (GND)** und die übergebenen (TXIN) in **TAXI IN**.
- In den Details heißt der Button **An Apron (TXIN)**.

## Runway-Bay und Freigaben

Line-up, Takeoff und Landing sind eine Bay: **RUNWAY**. Abflüge und Ankünfte stehen dort in einer gemeinsamen Reihenfolge, die du frei sortierst (z. B. Landung, dann Abflug, dann wieder Landung).

Rechts oben auf jedem Streifen in der Runway-Bay liegt ein **Freigabefeld**:

| Streifen | Feld | leer | abgehakt (grün mit Haken) |
|---|---|---|---|
| Abflug | **T/O** | Line-up (LINEUP) | Startfreigabe (DEPA) |
| Ankunft | **LDG** | auf der Piste, ohne Freigabe | Landefreigabe (CTL-Flag) |

- **Antippen** des Feldes schaltet um. Das ändert nur die Freigabe, der Streifen bleibt an seinem Platz, und es öffnen sich keine Details. Dieselben Buttons gibt es in den Details: **Startfreigabe** bzw. **Landefreigabe**.
- **Abflug auf die Runway ziehen** setzt LINEUP (mit `.strips takeoff DEPA` gleich die Startfreigabe). Zurück nach Taxi / Holding setzt TAXI.
- **Ankunft auf die Runway ziehen** legt sie nur auf die Piste, noch ohne Landefreigabe (nur bei dir sichtbar, EuroScope hat dafür keinen Status). Die Landefreigabe hakst du danach ab. Zurück nach Final nimmt eine Landefreigabe zurück.
- Gelandete Flieger **bleiben auf der Runway**, bis du sie als Tower herausziehst, auch beim Durchstarten. Ziehen nach **Taxi In** gibt sie an Ground ab (nur lokal, StripBay setzt dabei weder TXIN noch nimmt es CTL zurück). Setzt Ground TXIN, wandert der Streifen auch ohne dich nach Taxi In.

**Synchron mit allen:** Die Startfreigabe ist der Ground State DEPA, die Landefreigabe das CTL-Flag von vSID. Beides sehen andere Lotsen, TopSky und vSID sofort. Gibt jemand anderes die Freigabe (z. B. CTL im vSID-Tag), springt der Streifen bei dir auf die Runway und ist abgehakt.

### Durchstarten (G/A)

In den Details einer Ankunft im Anflug: **Durchstarten (G/A)**. Die Landefreigabe wird zurückgenommen (CTL-Flag), der Streifen geht zurück nach **Final** und bekommt die rote Marke **G/A**. Die nächste Frequenz ist dann Bremen Radar der Bahn (Nord 120.340, Süd 120.630). Die Marke sehen alle Lotsen mit StripBay (Scratchpad `.SB_GA_TRUE`). Sie endet mit einer neuen Landefreigabe oder der Landung, nochmal antippen nimmt sie zurück.

### Warnungen

- **Stand doppelt belegt:** Haben zwei Flieger am selben Platz denselben Stand (Ankünfte und Abflüge, die noch am Stand stehen, also bis Startup), bekommen beide die rote Marke **STAND!**, das STAND-Feld wird rot. In den Details steht, wer den Stand noch hat.
- **Piste nicht aktiv:** Ist die Piste im Tag im Runway-Dialog für diese Richtung (Abflug bzw. Ankunft) nicht aktiv, wird das RWY-Feld rot, in den Details steht "nicht aktiv!". Gilt nur für Plätze, an denen überhaupt eine Piste aktiv ist.

### Landefreigabe (CTL) und vSID

- StripBay verteilt CTL genauso wie vSID, als kurzes Scratchpad `.VSID_CTL_TRUE` bzw. `.VSID_CTL_FALSE`. Der alte Scratchpad-Inhalt wird danach wiederhergestellt.
- Setzt jemand CTL in vSID (auch du selbst im Tag), wandert der Streifen auf die Runway, abgehakt.
- Zurückgenommen wird die Freigabe nur durch Antippen von LDG, Ziehen nach Final oder wenn jemand CTL in vSID entfernt. Durchstarten und Landung ändern nichts daran. Hatte nur ein anderer die Freigabe gegeben und nimmt sie zurück, geht der Streifen zurück nach Final. Hast du ihn selbst auf die Runway gelegt, bleibt er dort.
- Ein CTL-Flag, das gesetzt wurde, bevor StripBay geladen war, kennt StripBay erst, wenn vSID es erneut verteilt.

## Details bearbeiten

In den Details (Streifen antippen) lässt sich jeder Wert ändern. Bearbeitbare Felder haben eine feine Linie unter dem Wert. **Antippen** öffnet:

- eine **Auswahlliste**, wenn es feste Werte gibt. Der aktuelle Wert ist grün. Lange Listen haben **Reiter nach Anfangsbuchstaben** (bei Stands A, B, C ...; noch einmal antippen zeigt wieder alle) und **Seiten**. Daneben gibt es **Type** (Tastatur), **Clear** und **C** (abbrechen). Bei Piste, SID und STAR gibt es keine freie Eingabe, nur die Liste.
- sonst die **Tastatur**. Zahlenfelder (Squawk, Heading, Speed, EOBT) nehmen nur Ziffern.

| Feld | Auswahl | wird gesetzt über |
|---|---|---|
| Stand | **Auto (GRP)**, Apron 1, GAT, Kategorien Apron 2 / A–E / W aus `GRpluginStands.txt` (wird automatisch gefunden); besetzte, vergebene und gesperrte Stands blass | `GRP/S/<Stand>`, wie GRP selbst, für alle sichtbar |
| SID | nur die SIDs für die Piste des Streifens (Sektordatei); **Auto** = EuroScope wählt | erster Routenpunkt `SID/RWY`, wie vSID |
| Runway | nur die festen Pisten (Berlin 06L 06R 24L 24R, aktive zuerst). Die SID bleibt, wenn es sie auch für die neue Piste gibt, sonst wählt EuroScope | erster bzw. letzter Routenpunkt |
| STAR | STARs für Platz und Landepiste | letzter Routenpunkt `STAR/RWY` |
| CFL / Initial Altitude | 1000 bis 5000 ft in 1000er-Schritten, dann FL060 …, bei Ankünften auch ILS und VIS; eine Spalte zum Scrollen | Cleared Altitude |
| Final Altitude | FL050 bis FL450 | |
| Squawk | Tastatur (nur gültige Codes 0–7) | |
| Flugregeln | I, V | Flugplan |
| Funk | V, R, T | |
| Ground State | Button **Ground state: …**; Line-up/Takeoff nur beim Tower | Ground State in EuroScope |
| Nächste Frequenz | AUTO, die Berliner Frequenzen, alle Lotsen online | nur bei dir (AUTO = wieder automatisch) |
| Typ, Ziel, Herkunft, Ausweichflughafen, EOBT, Route, Remarks | Tastatur | Flugplan |
| Heading, Speed | Tastatur | Assigned Heading/Speed |
| Scratchpad, Intersection, Hold Short | eigene Editoren wie bisher | |
| Clearance (PENDING / SENT / CLEARED), Start-/Landefreigabe | Antippen schaltet um; Delivery auch direkt im CLR-Feld des Streifens | Clearance-Flag |

Flugplanänderungen (Route, SID, Piste, Typ, Ziel ...) nimmt EuroScope nur an, wenn kein anderer Lotse den Flieger trackt. Klappt es nicht, steht das im ES-Chat. `.strips icao` zeigt, ob und wo die Stand-Liste gefunden wurde.

## Streifen erstellen und Recover

**Streifen erstellen:** Button **+ Streifen** oben rechts, Callsign eintippen, **Erstellen**.
- Gibt es einen Flugplan mit diesem Callsign, entsteht ein normaler Streifen, auch wenn der Flieger sonst keinen bekäme (z. B. VFR, anderer Platz, zu weit weg). Er liegt in der Bay, die zu seinem Ground State passt, und bleibt, bis du ihn entfernst, der Pilot sich abmeldet oder er weiter als 20 NM weg ist. Abflug oder Ankunft ergibt sich aus seinem Flugplan.
- Ohne Flugplan (oder mit **Als Notiz-Streifen**) entsteht ein **Notiz-Streifen**: grauer Streifen mit dem Text und der Marke NOTIZ, z. B. für ein Fahrzeug, eine Pistenkontrolle oder eine Sperrung. Er landet in der ersten Spalte der Ansicht, lässt sich in jede Bay ziehen (auch auf die Runway, um sie zu blockieren) und kann einen **Hold Short** tragen. Notiz-Streifen sind nur bei dir sichtbar und setzen keinen Status.

**Recover:** Button **Recover** oben rechts (orange mit Zahl, wenn etwas drin ist).
- Darin stehen von Hand **entfernte** Streifen, **weggeschickte** Abflüge (solange sie noch in Reichweite sind) und entfernte **Notiz-Streifen**, die neuesten oben.
- **Antippen** holt einen Streifen zurück, **Recover all** alle. Ein weggeschickter Abflug verliert dabei das Handover-Flag (auch bei den anderen Lotsen).
- Entfernte Streifen bleiben in Recover, bis der Flieger weiter als **20 NM** weg ist oder sich abmeldet. Dann ist er komplett weg.

## Apron-Ansicht

**Ohne Delivery (ab 2.0.9):** Ist an deinem Platz kein `<APT>_*DEL` online, zeigt die Apron-Ansicht vorne zusätzlich **PENDING** und **ON FREQUENCY**, der Button heißt dann "Apron + DEL". Für diese Flieger gelten auch die Delivery-Frequenzen nach Stand. Kommt ein Delivery online, verschwinden die beiden Spalten wieder.

Für Apron-Lotsen (Vorfeld): **STARTUP → PUSHBACK → TAXI → TO GROUND**, dazu **TAXI IN**: oben die an Apron übergebenen (TXIN), darunter die noch bei Ground. Siehe [Aufteilung](#aufteilung-der-ansichten-ab-2015).

- Startup, Pushback und Taxi setzen wie überall den Ground State (ST-UP, PUSH, TAXI).
- **AN GROUND** übergibt den Flieger an Ground. Das ist nur lokal, EuroScope hat dafür keinen Status, der Ground State bleibt TAXI. Der Streifen bekommt das Badge **GND**. In den Details gibt es dafür den Button **An Ground**.
- In der **Ground-Ansicht** stehen übergebene Flieger oben in Taxi Out, im **Tower** in Taxi / Holding.
- Wie bei "An Tower": Ist Ground ein anderer Lotse, sieht er die Übergabe nicht, nur den Status TAXI.

## Hold Short

Ein Hold Short wird auf dem Streifen als **rote Marke** "HS V1" oben rechts angezeigt, dazu eine rote Linie über den ganzen Streifen.

- **Eintragen (schnell, ab 2.0.7):** auf dem Streifen die **RWY-Zelle** antippen. Es öffnet sich eine Auswahl mit den Pisten des Platzes, der Intersection des Fliegers und den üblichen Haltepunkten. Ein Tipp, fertig. Eigene Eingabe ist dort auch möglich (höchstens 6 Zeichen).
- **Ändern / löschen:** die rote Marke antippen, es kommt dieselbe Auswahl. **Leeren** löscht den Hold Short. Auch in den Details über **Hold Short** oder das Feld HOLD SHORT.
- **Liste der Haltepunkte ändern:** `.strips hs V1 V3 V4 P1 P2 L3 ...` (mit Leerzeichen getrennt), `.strips hs reset` für die Werte ab Werk.
- Mit **Line-up** oder **Startfreigabe** ist der Hold Short erledigt und verschwindet von selbst.
- **Geteilt:** Andere Lotsen mit StripBay sehen den Hold Short auch. StripBay verteilt ihn wie vSID als kurzes Scratchpad `.SB_HS_V1` (`.SB_HS_NONE` = gelöscht). Die Nachricht bleibt mindestens einen Takt (1 s) stehen und geht so sicher ans Netz; danach steht der alte Scratchpad-Inhalt wieder da (ab 2.0.23). Lotsen ohne StripBay sehen ihn nicht.

## METAR und ATIS

Die obere Leiste zeigt für den Platz (Berlin, sonst den ersten aktiven) den **ATIS-Buchstaben** als farbigen Kasten und daneben den **METAR**.

- **METAR:** von EuroScope selbst (sobald EuroScope einen neuen METAR bekommt), sonst von `metar.vatsim.net`, jede Minute. Ist er zu lang für den Platz, steht nur das Wichtigste da: **Wind und QNH**, z. B. "EDDB  WIND 25010G22KT 220V280  QNH 1020".
- In schmalen Fenstern (z. B. 1280 Pixel) bekommt die Leiste eine zweite Zeile für ATIS und METAR.
- **ATIS:** aus dem VATSIM-Datenfeed (`data.vatsim.net`), jede Minute. Bei getrennter Abflug-/Anflug-ATIS stehen beide da, z. B. "ATIS A R  D Q".
- Wechselt der ATIS-Buchstabe, ist der Kasten **eine Minute lang orange**.
- `.strips wx aus` schaltet die Abfrage bei VATSIM ab, dann kommt nur noch der METAR aus EuroScope.

## Hell (DFS-Stil) und dunkel

Button **Hell** / **Dunkel** oben rechts oder `.strips hell` / `.strips dunkel`. Das helle Schema ist angelehnt an Tower-Streifensysteme, wie die DFS sie nutzt: heller Arbeitsplatz, schwarze Schrift, **Abflüge auf leicht blauem**, **Ankünfte auf cremefarbenem** Streifen (wie im dunklen Schema). Die genauen DFS-Farben sind nicht öffentlich dokumentiert, die Werte sind eine Annäherung. StripBay merkt sich die Wahl.

## Weggeschickt (Abflüge nach dem Start)

Ein Abflug verschwindet nicht mehr beim Abheben, sondern erst, wenn er **weggeschickt** ist. Dafür nutzt StripBay das **Handover-Flag von vSID** (`HOV` im vSID-Tag), es gilt also für alle Lotsen gleich.

- Ist ein Abflug in der Luft, noch nicht weggeschickt und **mindestens 2000 ft über dem Platz** (ab 2.0.13), ist sein Feld **FREQ orange**, wie `HOV` in vSID. Darunter bleibt es neutral, antippen geht trotzdem. **Rot** wird es, sobald er weniger als 1500 ft unter seiner CFL ist, wie `HOV!` in vSID.
- **FREQ antippen** (nur in der Luft) oder in den Details **Weggeschickt**: StripBay setzt das Handover-Flag (`.VSID_HOV_TRUE`), der Streifen verschwindet. Andere Lotsen und vSID sehen das Flag sofort.
- Setzt jemand anderes das Flag (im vSID-Tag oder mit StripBay), verschwindet der Streifen auch bei dir.
- Aus Versehen weggeschickt: in **Recover** antippen oder `.strips hov CALLSIGN`.
- In der Ground-Ansicht verschwinden Abflüge schon beim Abheben aus Departing Tower.
- Sicherheitsnetz: Wer gar nicht weggeschickt wird, verschwindet ab 20 NM Entfernung (`.strips range`, höchstens 20).

## Piste verlassen: automatisch Taxi In (ab 2.0.13)

StripBay liest die Pisten aus dem Sektorfile (beide Enden jeder Piste). Ist eine gelandete Ankunft zweimal hintereinander (zwei Sekunden) **neben jeder Piste des Zielplatzes** (mehr als 45 m neben der Mittellinie oder hinter den Enden), gibt der Tower sie automatisch an Ground: der Streifen geht von **Runway** nach **Taxi In GND**. Das passiert einmal je Landung; zieht der Tower ihn zurück auf die Runway, bleibt er dort. Hat das Sektorfile für den Platz keine Pisten, passiert nichts.

- `.strips vacate an` / `aus` schaltet das ein oder aus und zeigt, wie viele Pisten gefunden wurden.

## CPDLC und DCL über Hoppie (ab 2.0.13)

StripBay spricht mit dem Hoppie-ACARS-Netz: **DCL/PDC** (Departure Clearance per Datalink) und **CPDLC** (Logon, Contact/Monitor, Freitext, Antworten).

**Einrichten (einmal):**
1. Auf hoppie.nl einen Logon-Code holen.
2. Im Einstellungsfenster **Logon code…** antippen und den Code eingeben oder mit **Paste** einfügen (Groß-/Kleinschreibung bleibt erhalten), oder in EuroScope: `.strips cpdlc code DEINCODE`. Der Code wird in den EuroScope-Einstellungen gespeichert.
3. Station: ab Werk der Platz (z. B. `EDDB`). Ändern mit `.strips cpdlc station EDDB` (`auto` = Platz). Die Station darf nur **ein** Lotse gleichzeitig nutzen, sonst meldet Hoppie "callsign already in use" bzw. nur einer bekommt die Nachrichten.
4. `.strips cpdlc` zeigt den Status, `.strips cpdlc aus` / `an` schaltet ab und an.

StripBay fragt Hoppie alle 45–75 Sekunden ab, nach dem Senden eine Weile alle 20 Sekunden (wie von Hoppie empfohlen).

**Auf dem Streifen** ein Farbpunkt oben rechts (ab 2.0.23; Antippen öffnet das CPDLC-Fenster):

| Punkt | Bedeutung |
|---|---|
| orange, blinkt | Pilot fragt nach einer Clearance (DCL) oder möchte sich anmelden (Logon) |
| blau | Pilot ist angemeldet bzw. DCL gesendet (Clearance-Status **SENT**) |
| rot | Pilot hat den DCL abgelehnt (UNABLE) |
| magenta, blinkt | ungelesene Nachricht oder Anfrage, die eine Antwort braucht |

**CPDLC-Fenster** (über die Marke oder in den Details **CPDLC**):
- Oben Logon- und DCL-Status, die Anfrage des Piloten und **der DCL, wie er gesendet wird**, z. B. `CLR TO EGLL RWY 24R DEP KLF2X INIT CLB 5000FT SQUAWK 1000 ATIS C WHEN RDY CALL FREQ 129.605 IF UNABLE CALL VOICE`. Er kommt aus dem Streifen: Ziel, Piste, SID, CFL, Squawk, CTOT (sonst TSAT), ATIS-Buchstabe und die nächste Frequenz (bei Delivery die Frequenz nach Stand, sonst deine eigene). Fehlt etwas (SID, Squawk, CFL ...), steht dort, was fehlt, und **DCL senden** ist gesperrt.
- **DCL senden**: Antwortet der Pilot mit **WILCO**, setzt StripBay automatisch das **Clearance-Flag**. **DCL: per Sprechfunk** antwortet "UNABLE, CALL ON VOICE".
- **Logon annehmen / ablehnen** (ab Werk werden Logons automatisch angenommen, `.strips cpdlc auto aus` schaltet das ab).
- **Contact / Monitor** mit der nächsten Frequenz, z. B. "CONTACT BERLIN GROUND 121.705".
- Antworten auf Anfragen des Piloten: **Standby, Unable, Roger, Affirm, Negative**.
- **Freitext** (Tastatur oder Stift). Ist der Pilot nicht per CPDLC angemeldet, geht er als Telex.
- **Logoff**, darunter der Verlauf mit Uhrzeit (↑ von dir, ↓ vom Piloten) und den Antworten (WILCO grün, UNABLE rot).
- Ton bei neuen Anfragen: `.strips cpdlc ton aus` / `an`.

## Handschrift (Stift) im Editor (ab 2.0.13)

In jedem Eingabefeld (Scratchpad, Felder in den Details, Hold Short, Intersection, neuer Streifen, CPDLC-Freitext) gibt es oben rechts **Stift (Handschrift)**. Statt der Tastatur erscheint ein Schreibfeld: mit Stift oder Finger schreiben, nach einer kurzen Pause wird der Text erkannt und eingefügt (in Großbuchstaben). Darunter: **←** (Zeichen zurück), **Leerzeichen**, **Tinte weg**, **Erkennen** (sofort). **Tastatur** schaltet zurück.

Die Erkennung macht Windows selbst (Handschrifterkennung von Windows Ink). Sie muss installiert sein: Einstellungen > Zeit und Sprache > Sprache und Region > Deutsch (oder Englisch) > Sprachoptionen > **Handschrift**. Auf Tablets und Geräten mit Stift ist das meist schon da. Fehlt sie, steht im Schreibfeld "Windows-Handschrifterkennung nicht verfügbar". Am besten erkennt sie deutlich getrennte Großbuchstaben und Ziffern.

## Intersection

Die Intersection aus vSID steht bei Abflügen im Feld **INT** direkt neben RWY. **Hellgrün** heißt zugewiesen, **grau** "able for" (wie grün bzw. weiß in vSID).

- Gesetzt wird sie wie gewohnt in der Departure List (vSID-Menü) oder per Scratchpad `+L6` bzw. `-L6`. StripBay übernimmt jede Änderung, auch von anderen Lotsen.
- **In StripBay setzen:** INT-Feld antippen (oder in den Details INTERSECTION). Es öffnet sich die Tastatur, höchstens 3 Zeichen. **Zugewiesen** setzt sie als zugewiesen, **Able for** als möglich, **Leeren** und dann Zugewiesen entfernt sie. StripBay verteilt das im Format von vSID (`.VSID_INT_L6_TRUE`), dein vSID und alle anderen übernehmen es.
- vSID selbst blendet die Intersection mit der Startfreigabe (DEPA) aus. StripBay zeigt sie weiter an, bis der Streifen verschwindet.

## Nächste Frequenz

Das Feld **FREQ** auf jedem Streifen zeigt, wohin der Flieger als Nächstes wechselt. In den Details steht dazu die Station, z. B. "120.340  (EDDB_N_APP)". Reihenfolge:

1. **Laufender Handoff** in EuroScope: dessen Ziel.
2. **Berlin (EDDB):** feste Ketten, die erste passende gewinnt. Die eigene Station zählt nie mit.
3. Sonst der nächste Lotse, wie EuroScope ihn aus Route und Sektoren bestimmt, danach Rufzeichen-Muster (`{APT}_*DEP`, `{APT}_*APP` bzw. `{APT}_*GND`).

**Berlin ab Werk:**

| Fall | Kette |
|---|---|
| Abflug Nordbahn | Bremen Radar **120.340**, wenn ein `EDDB_*APP` online ist. Sonst die erste besetzte von 136.450 > 123.225 > 136.050 > 133.725, sonst **122.800** (UNICOM) |
| Abflug Südbahn | Bremen Radar **120.630**, wenn ein `EDDB_*APP` online ist. Sonst die erste besetzte von 136.450 > 123.225 > 136.050 > 133.725, sonst **122.800** (UNICOM) |
| Ankunft auf Stand **W1, W2, W3, W4** | Ground Süd **121.705**, nur wenn Ground Süd online ist, sonst **-** |
| Ankunft Nordbahn | Apron Nord **129.605**, sonst Ground Nord **129.505**, sonst **-** |
| Ankunft Südbahn | Apron Süd **121.855**, sonst Ground Süd **121.705**, sonst **-** |
| **Delivery** (ab 2.0.7): Stand **A\***, **D01–D03**, **E\*** | Apron Nord **129.605**, sonst Ground Nord **129.505**, sonst **-** |
| Delivery: Stand **B\***, **C\***, übrige **D\*** | Apron Süd **121.855**, sonst Ground Süd **121.705**, sonst **-** |
| Delivery: Stand **W\*** | Ground Süd **121.705**, wenn online, sonst **-** |

Die Delivery-Regeln gelten, wenn dein Callsign auf `_DEL` endet oder die Delivery-Ansicht offen ist. Die erste passende Gruppe gewinnt. Ist bei einem Treffer niemand aus der Kette online, bleibt das Feld leer.

**Es erscheinen nur Stationen, die online sind** (ab 2.0.25 auch Apron): Ist kein Nachbar da, steht im FREQ-Feld **-**. Kommst du selbst in der Kette vor, zählt nichts danach (das wäre deine eigene Vertretung, kein Nachbar): Sitzt du auf Ground Süd und Apron Süd ist offline, steht bei Süd-Inbounds **-**, nicht deine eigene Frequenz. Alte gespeicherte Werkseinstellungen (`129.605!`, `121.855!`) werden beim Laden automatisch umgestellt, eigene Ketten bleiben. Die Stand-Regel gilt für W1 bis W4 und Unterpositionen wie W4A, nicht für W12. In den Details lässt sich die Frequenz je Flieger auch von Hand setzen.

Ketten ändern (Einträge mit `>` trennen):

| Eintrag | Bedeutung |
|---|---|
| `EDDB_*APP=120.340` | ist ein Lotse mit passendem Rufzeichen online, gilt 120.340 (`*` = beliebig) |
| `136.450` | jemand ist auf dieser Frequenz online |
| `122.800!` | gilt immer (Ende der Kette) |

| Befehl | Wirkung |
|---|---|
| `.strips freq` | zeigt die Ketten |
| `.strips freq dep EDDB_*APP=120.340 > 136.450 > 122.800!` | Abflüge, beide Bahnen |
| `.strips freq dep süd EDDB_*APP=120.630 > 122.800!` | Abflüge nur einer Bahn (`nord` oder `süd`) |
| `.strips freq nord 129.605 > 129.505` | Ankünfte Nordbahn (`süd` für die Südbahn) |
| `.strips freq stand W1,W2,W3,W4=121.705` | Ankünfte nach Stand (vor den Bahn-Regeln). Mehrere Gruppen mit `;`, `+` ergänzt vorne, z. B. `.strips freq stand +G*=129.505`. Trifft eine Gruppe und dort ist niemand online, bleibt das Feld leer |
| `.strips freq del A*,E*=129.605 > 129.505; B*,C*=121.855 > 121.705` | Delivery nach Stand: Gruppen mit `;` trennen, Stands mit `,`, `*` = beliebig |
| `.strips freq del +G*=129.505` | Gruppe **vorne** ergänzen (`+`), hier: GAT zu Ground Nord, ohne Ground Nord leer |
| `.strips freq reset` | zurück auf die Berliner Werte |

Ein Eintrag nur mit Rufzeichen (z. B. `EDDB_N_TWR`) bedeutet: ist dieser Lotse online, gilt seine Frequenz.

## Berlin: zwei getrennte Pisten

Am BER laufen Nord- und Südbahn unabhängig, oft mit zwei Tower-Lotsen. StripBay trennt deshalb alle pistenbezogenen Bays nach Piste. Ab Werk ist EDDB eingestellt:

| Gruppe | Pisten |
|---|---|
| NORD | 06L / 24R (alte Sektorpakete: 07L / 25R) |
| SÜD | 06R / 24L (alte Sektorpakete: 07R / 25L) |

**Welche Piste zählt:** immer die im Tag eingetragene, bei Abflügen die Departure-Runway, bei Ankünften die Arrival-Runway. Ändert sie jemand (im Tag, über vSID, ein anderer Lotse), wandert der Streifen sofort in die andere Pistenspalte. StripBay selbst ändert nie eine Piste.

**Getrennt werden:**

| Ansicht | Je Piste | Gemeinsam |
|---|---|---|
| Tower | alle Spalten: Taxi / Holding, Runway, Final, Taxi In | |
| Ground | Departing Tower (Übergabe an Tower Nord bzw. Süd) | Startup, Pushback, Taxi Out, Taxi In |
| Alle Bays | Taxi / Holding, Runway, Final | Pending bis Pushback, Taxi In |
| Delivery | | alles |

**Anzeige:** Der Button oben rechts (links neben der Ansicht) schaltet um: **Beide Pisten** → **NORD 24R** → **SÜD 24L**. Bei beiden Pisten liegt jede getrennte Spalte zweimal übereinander, oben Nord, unten Süd. Gemeinsame Spalten gehen über die volle Höhe. Im Tower sind das zwei Reihen, eine je Bahn. Mit nur einer Piste sieht ein Tower-Lotse ausschließlich seine Bahn. Die Kopfzeile jeder Pistenspalte trägt eine farbige Marke mit Gruppe und aktiver Piste aus dem Runway-Dialog, das RWY-Feld auf dem Streifen hat dieselbe Farbe (Nord rötlich, Süd violett).

**Eingaben bleiben synchron:** Ziehen und Abhaken setzen wie bisher den Ground State in EuroScope (LINEUP, DEPA, TAXI ...) bzw. das CTL-Flag über vSID. Andere Lotsen, TopSky und GRP sehen das sofort, egal welche Piste sie bei sich anzeigen. Stellt der Tower Süd einen Flieger auf Line-up, erscheint er auch beim Tower Nord in "Beide Pisten" unten in Runway Süd.

**Regeln beim Ziehen:**
- Ein Streifen geht nur in die Spalte seiner Piste. Ein Flieger für 24L lässt sich nicht nach Runway Nord ziehen. Soll er die Bahn wechseln, Piste im Tag ändern.
- Umsortieren innerhalb einer Pistenspalte ändert nur die Reihenfolge dieser Piste, die Flieger der anderen Bahn bleiben, wo sie sind.
- Passt die Piste im Tag zu keiner Gruppe (leer oder fremd), steht der Streifen bei **beiden** Pisten, damit ihn niemand übersieht. Sein RWY-Feld ist dann orange, in den Details steht "keiner Piste zugeordnet". Er lässt sich in beide Pistenspalten ziehen.
- Flieger anderer Plätze (z. B. EDDB und ein Nebenplatz aktiv) haben keine Gruppe und stehen ebenfalls bei beiden Pisten.

Die Trennung ist nur aktiv, wenn EDDB einer deiner aktiven Plätze ist. Sonst sieht alles aus wie bisher.

| Befehl | Wirkung |
|---|---|
| `.strips piste` | Status: Gruppen, aktive Pisten, Anzeige |
| `.strips piste beide` | beide Pisten anzeigen |
| `.strips piste nord` / `süd` (auch `sued`, `n`, `s`, `24R`, `1`, `2`) | nur eine Piste anzeigen |
| `.strips piste aus` / `an` | Trennung ganz aus- oder einschalten |
| `.strips piste berlin` | zurück auf die EDDB-Werte |
| `.strips piste set EDDF A:07C,25C B:07L,25R` | eigene Gruppen für einen anderen Platz (genau zwei Gruppen, jede Piste nur in einer) |

Anzeige und Gruppen werden mit den Plugin-Einstellungen gespeichert.

## Ankünfte und Parkposition

- Ankünfte erscheinen ab 15 NM vor dem Zielplatz in **Final** und wechseln nach **Taxi In**, sobald sie am Boden sind: höchstens 4 NM vom Zielplatz, höchstens 60 kt und weder im Steig- noch im Sinkflug. In der Ground-Ansicht sind sie deshalb erst nach der Landung zu sehen. Zielplatz ist einer deiner aktiven Ankunfts-Flughäfen (Haken "arr" im Runway-Dialog, sonst der Platz aus deinem Callsign).
- In den Details über **Ground state**: **Taxi in** setzt TXIN (Badge **TXI** statt **ARR**), **Parked** setzt PARK und der Streifen verschwindet – **Parked** steht dort nur, wenn der Flieger am Gate steht (< 3 kt). Schneller: die grüne **P**-Marke im STAND-Feld.
- Die **Parkposition** kommt aus dem Ground Radar Plugin. GRP verteilt Stands als Scratchpad-Kommando `GRP/S/<Stand>` und legt sie lokal in der Flight-Strip-Annotation 6 ab. StripBay liest beides jede Sekunde, die jüngste Änderung gewinnt. Damit erscheinen auch Stands, die schon vergeben waren, bevor der Streifen da war, und jede spätere Änderung.
- Fehlt ein Stand, den GRP anzeigt: Flieger anwählen und `.strips stand` eingeben. StripBay schreibt dann in den ES-Chat, was in den Annotationen und im Scratchpad steht.

## Welche Flieger einen Streifen bekommen

**Abflüge:**
- Abflugplatz ist einer deiner aktiven Abflug-Flughäfen (Haken "dep" im Runway-Dialog). Ist keiner aktiv, nimmt StripBay den Platz aus deinem Callsign (EDDF_GND → EDDF). Mit `.strips apt` legst du die Plätze fest.
- Der Flieger ist höchstens 5 NM vom Platz entfernt und langsamer als 50 kt.
- Der Streifen verschwindet erst, wenn der Flieger in der Luft ist **und** weggeschickt wurde (Handover-Flag), beim Disconnect oder als Sicherheitsnetz ab 20 NM Entfernung (dann komplett, auch aus Recover). Entfernungen mit `.strips range` änderbar.

**Ankünfte:**
- Zielplatz ist einer deiner aktiven Ankunfts-Flughäfen, höchstens 15 NM entfernt.
- Der Streifen verschwindet bei PARK, beim Disconnect oder wenn der Flieger sich wieder mehr als 25 NM entfernt.

## Bedienung

| Geste | Wirkung |
|---|---|
| Streifen ziehen | in andere Bay verschieben (setzt Ground State) oder innerhalb der Bay umsortieren |
| Streifen antippen | Details öffnen, Flieger in EuroScope anwählen |
| SP-Feld antippen | Scratchpad-Editor öffnen |
| neben die Details tippen | Details schließen |
| Wischen auf freier Fläche, Scrollleiste, Mausrad | Bay scrollen |
| Ansicht-Button oben rechts | Alle Bays → Delivery → Apron → Ground → Tower |
| Hell/Dunkel-Button oben rechts | Farbschema umschalten |
| rote HS-Marke antippen | Hold Short ändern oder löschen |
| **+ Streifen** oben rechts | neuen Streifen erstellen |
| **Recover** oben rechts | entfernte und weggeschickte Streifen, Antippen holt zurück |
| Pisten-Button oben rechts (nur Berlin) | Beide Pisten → Nord → Süd |
| − / + oben rechts | Zoom |
| × oben rechts | Fenster ausblenden (`.strips show` holt es zurück) |

**Orange Ecke** am Streifen: EuroScope hat den Status nicht angenommen, z. B. weil ein anderer Lotse den Flieger trackt oder deine ES-Version ONFREQ nicht kennt. Der Streifen bleibt dann nur bei dir in der neuen Bay, bis sich der Status in EuroScope ändert. Im ES-Chat steht dazu eine Meldung.

## Befehle (EuroScope-Kommandozeile)

Ab 2.0.20 einheitlich englisch: **`.strips <command> [value]`**, Schalter immer mit **`on` | `off`**. Die alten deutschen Namen (`hell`, `neu`, `notiz`, `zurück`, `piste`, `view`, `wx`, `arr`, `hov`, `an`/`aus` …) funktionieren weiter. Fast alles geht auch im **Einstellungsfenster** (Button **Settings** oben oder `.strips settings`); jede Schaltfläche dort führt genau den passenden Befehl aus.

| Befehl | Wirkung |
|---|---|
| `.strips help` | Übersicht |
| `.strips settings` | Einstellungsfenster öffnen |
| `.strips window show` / `hide` | Fenster ein- und ausblenden |
| `.strips fullscreen [on\|off]` | Vollbild |
| `.strips ontop [on\|off]` | immer im Vordergrund |
| `.strips zoom 130` | Größe in Prozent (60 bis 250) |
| `.strips theme light` / `dark` | Farbschema |
| `.strips layout auto` / `columns` / `grid` | Bays nebeneinander oder in zwei Reihen |
| `.strips station del` / `apn` / `gnd` / `twr` / `all` | Station |
| `.strips area combined` / `north` / `south` | Bereich bei getrennten Pisten |
| `.strips runways on` / `off` / `berlin` | Pistentrennung an/aus, Berlin-Standard |
| `.strips runways set EDDB NORTH:06L,24R SOUTH:06R,24L` | eigene Pistengruppen |
| `.strips airports auto` / `EDDF,EDDS` | Flughäfen automatisch oder fest |
| `.strips takeoff lineup` / `depa` | Status, wenn ein Abflug auf die Runway gezogen wird |
| `.strips arrivals 15` | Ankünfte erscheinen ab dieser Entfernung (NM) in FINAL (3–20) |
| `.strips range 5 20` | Abflüge: Erfassen und Sicherheitsgrenze in NM (höchstens 20) |
| `.strips vacate on` / `off` | Ankunft nach dem Verlassen der Piste automatisch nach Taxi In |
| `.strips freq [dep\|north\|south\|stand\|del CHAIN \| reset]` | nächste Frequenz, siehe [Nächste Frequenz](#nächste-frequenz) |
| `.strips gndstands W*,G*` / `reset` | Stands ohne Apron: Startup bei Ground |
| `.strips holdshort V1 V2 V3 V4 VC ...` / `reset` | Haltepunkte im Hold-Short-Tastenfeld |
| `.strips grpauto 2` / `off` | Funktionsnummer von "Auto" im Stand-Menü des Ground Radar Plugins; `off` = StripBay wählt selbst |
| `.strips rwyswitch` | Betriebsrichtungswechsel: Abflüge vor dem Rollen auf die aktive Piste umstellen |
| `.strips profile EDDB` / `EDDP` / `EDDV` / `auto` | Platz-Profil festlegen (sonst automatisch nach Callsign bzw. aktivem Platz) |
| `.strips freq sid PENEM,ORTAG=126.065; *=126.175` | Abflug-Frequenz nach SID (erste passende Gruppe) |
| `.strips freq tower [north\|south] 125.955` | Tower je Bahn (nächste Station für Ground/Delivery) |
| `.strips resync` | Merker deiner Flieger noch einmal an alle schicken |
| `.strips give CS [STELLE]` / `accept CS` / `reject CS` / `take CS` / `offer CS CANCEL` | Übergabe: anbieten, annehmen, ablehnen, übernehmen, zurücknehmen |
| `.strips responsibility on` / `off` | Zuständigkeit und Übergaben an/aus |
| `.strips timeline on` / `off` / `range 3 6` / `night on\|off` / `estimate on\|off` | Tower-Zeitstrahl |
| `.strips intersections north\|south LISTE` / `reset` | Intersections je Piste im INT-Tastenfeld |
| `.strips weather on` / `off` | METAR/ATIS von VATSIM |
| `.strips cdm on` / `off` / `url URL` / `url auto` | vACDM (TSAT) |
| `.strips cpdlc on` / `off` / `code CODE` / `station EDDB` / `autologon on\|off` / `sound on\|off` | CPDLC/DCL über Hoppie, siehe [CPDLC](#cpdlc-und-dcl-über-hoppie-ab-2013) |
| `.strips new CALLSIGN` | Streifen erstellen (ohne Flugplan: Notiz-Streifen) |
| `.strips note TEXT` | Notiz-Streifen, z. B. `.strips note FOLLOWME 1` |
| `.strips recover CALLSIGN` / `all` | aus Recover zurückholen (auch `restore`) |
| `.strips handover undo CALLSIGN` | Weggeschickt zurücknehmen, der Streifen kommt zurück |
| `.strips stand [CALLSIGN]` | Diagnose: woher die Parkposition kommt |
| `.strips icao [folder\|auto]` | ICAO-Daten (Rufzeichen, Flughafennamen) |
| `.strips reset` | ausgeblendete Streifen zurückholen, lokale Abweichungen verwerfen |

`.sb` funktioniert als Kurzform von `.strips`.

## TSAT aus CDM

**Tower (ab 2.0.9):** In der Tower-Ansicht steht auf Abflug-Streifen statt der TSAT die **CTOT**. Farbe nach Slotfenster (ab 2.0.12): orange = noch früh, **gelb** = ab 15 Minuten vorher, **grün** = im Fenster −5/+10 Minuten, **rot** = Slot verpasst (auch in den Details). Ohne Slot steht dort grau die **TTOT**. Die Details zeigen immer TSAT, CTOT und TTOT. Quellen: CDM-Plugin (Annotation 0, Feld TTOT bzw. CTOC) oder vACDM (`ttot`, `ctot`).

StripBay holt die TSAT aus zwei Quellen, in dieser Reihenfolge:

1. **CDM-Plugin** (rpuig2001): Es legt die TSAT in der Flight-Strip-Annotation ab, StripBay liest sie direkt aus EuroScope. Dafür ist nichts einzustellen.
2. **vACDM** (VATGER): vACDM gibt seine Daten nicht über EuroScope weiter. StripBay fragt deshalb alle 10 Sekunden denselben Server ab wie dein vACDM-Plugin. Die Adresse liest es aus der `vacdm.txt` neben der geladenen `vACDM.dll`, du musst also nichts eintragen.

Grün wird das TSAT-Feld, solange die aktuelle Zeit zwischen TSAT − 5 und TSAT + 5 Minuten liegt. In den Details steht hinter der Zeit, aus welcher Quelle sie kommt.

| Befehl | Wirkung |
|---|---|
| `.strips cdm` | zeigt, ob und wann vACDM zuletzt geantwortet hat |
| `.strips cdm url https://...` | vACDM-Server fest einstellen (falls die DLL anders heißt) |
| `.strips cdm url auto` | Server wieder aus der `vacdm.txt` lesen |
| `.strips cdm aus` / `an` | vACDM-Abfrage aus- oder einschalten |

## Rufzeichen und Flughafennamen

StripBay nimmt dieselben Daten wie EuroScope: `ICAO_Airlines.txt` (Airline-Rufzeichen) und `ICAO_Airports.txt` (Flughafennamen) aus deinem Sektorpaket. Die Plugin-Schnittstelle gibt sie nicht heraus, deshalb sucht StripBay die Dateien beim Laden im Hintergrund: im Ordner der DLL und darüber, unter Dokumente\EuroScope, %APPDATA%\EuroScope und im EuroScope-Programmordner.

- Airline-Callsigns werden zu Rufzeichen plus Flugnummer: DLH4AB wird LUFTHANSA 4AB, CFG4PN wird CONDOR 4PN.
- Kennzeichen ohne Airline werden buchstabiert: DIABC wird DELTA INDIA ALFA BRAVO CHARLIE.
- Flughäfen stehen mit Namen da, z. B. EGLL als London/Heathrow.

| Befehl | Wirkung |
|---|---|
| `.strips icao` | zeigt, wie viele Einträge geladen sind und aus welchen Dateien |
| `.strips icao C:\Pfad\zum\Paket` | Ordner festlegen, falls die Suche nichts findet |
| `.strips icao auto` | wieder automatisch suchen |

## Scratchpad schreiben

Tippe auf das SP-Feld eines Streifens, auf das Feld SCRATCHPAD in den Details oder auf den Button **Scratchpad**. Es öffnet sich ein Editor mit QWERTZ-Bildschirmtastatur:

- **OK** schreibt den Text ins EuroScope-Scratchpad, **Abbrechen** oder daneben tippen verwirft ihn, **Leeren** löscht alles.
- Solange der Editor offen ist, funktioniert auch die echte Tastatur (Enter = OK, Esc = Abbrechen). Danach geht der Fokus zurück an EuroScope.
- Höchstens 40 Zeichen. EuroScope wertet manche Texte als Befehl aus, genau wie beim Tippen im Tag (z. B. ein Wegpunktname als Direct).

## Clearance-Flag

StripBay setzt das Flag zuerst über die Scratchpad-Kommandos `CLEA` und `NOTC`. Klappt das bei deiner ES-Version nicht, nutzt es die eingebaute EuroScope-Funktion über einen Radarschirm. Dafür im Plug-ins-Dialog **StripBay** auswählen und **Standard ES radar screen** von "Forbidden to draw on types" nach "Allowed to draw on types" schieben. StripBay zeichnet dort nichts.

## Gut zu wissen

- Die Reihenfolge innerhalb einer Bay ist nur bei dir. EuroScope kennt keine Reihenfolge, andere Lotsen sehen nur den Status.
- Ein entfernter Streifen bleibt weg, bis der Flieger den Platz verlässt, du ihn in Recover zurückholst oder `.strips reset` eingibst.
- Die Übergabe an Tower ist nur bei dir sichtbar. EuroScope hat dafür keinen Ground State.
- Mit acht Spalten ordnet die Ansicht Alle Bays sie auf kleineren Bildschirmen in zwei Reihen an. `.strips layout spalten` erzwingt eine Reihe.
- Zum Ausprobieren eignet sich eine Sweatbox-Session oder ein EuroScope-Simulator-Szenario.
- Landen Berührungen auf dem falschen Monitor, in Windows **Tablet PC-Einstellungen → Setup** öffnen und den Touchscreen dem richtigen Bildschirm zuordnen.
- ONFREQ und LINEUP gibt es erst in neueren EuroScope-3.2-Versionen. Kennt deine Version sie nicht, greift die orange Ecke (lokal), der Rest funktioniert normal.

## Aufbau des Codes

| Datei | Inhalt |
|---|---|
| `src/Model.h` | Datenmodell (Bays, Ground States, Streifendaten), unabhängig von EuroScope |
| `src/Cdm.*` | TSAT: Zeitumrechnung, vACDM-Abfrage im Hintergrund (WinHTTP), kleiner JSON-Parser |
| `src/Hoppie.*` | Hoppie-ACARS-Client im Hintergrund (Pollen, Senden), Antworten und CPDLC-Pakete zerlegen |
| `src/Ink.*` | Handschrifterkennung über die Windows-Ink-Automation (COM, späte Bindung) |
| `src/Icao.*` | ICAO_Airlines/Airports suchen und lesen, ausgeschriebenes Rufzeichen |
| `src/StripBayPlugin.*` | EuroScope-Anbindung: Flieger einsammeln, Status lesen und setzen, Pistengruppen, Befehle, Einstellungen |
| `src/StripWindow.*` | Win32/GDI-Fenster: Layout (auch geteilte Pistenspalten), Zeichnen, Touch und Maus, Drag and Drop, Detailansicht |
| `src/dllmain.cpp` | Einstiegspunkte für EuroScope |
