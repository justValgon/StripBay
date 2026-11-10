# Änderungsverlauf

## Neu in 4.1.1

- **Lizenz:** MIT (vorher CC0).
- **Englische Dateinamen:** `INSTALLIEREN.bat` heißt jetzt `INSTALL.bat`, `REMOTE-FREIGABE.bat` heißt `REMOTE-FIREWALL.bat`, `SDK-PAKET.bat` heißt `SDK-PACK.bat`, `tools\RemoteFreigabe.ps1` heißt `RemoteFirewall.ps1`. Die Meldungen in EuroScope und in StripBay Remote nennen die neuen Namen.
- **READMEs zweisprachig:** `README.md` (Englisch) und `README.de.md` (Deutsch), ebenso `Remote\README.txt` / `README.de.txt` und `StripBay\sdk\README.txt` / `README.de.txt`. Handbuch und Änderungsverlauf bleiben Deutsch (`docs\MANUAL.de.md`, `docs\CHANGELOG.de.md`).
- Keine Funktionsänderung, kein Protokollwechsel: 4.1.1 verträgt sich mit allen 4.x.

## Neu in 4.1.0

### Gemischter Betrieb: besetzte Stellen ohne StripBay

Sind alle Stellen besetzt, aber nur manche nutzen StripBay (z. B. nur Delivery und Tower), läuft die Kette trotzdem durch:

- **Übergabe an eine Stelle ohne StripBay** (wie seit 4.0.0): Sie wird direkt eingetragen, der Streifen ist bei dir erledigt (30 s grau, dann weg). Nichts wartet auf eine Annahme.
- **Neu: Streifen von einer Stelle ohne StripBay.** Für alle Stellen *nach* ihr ist der Streifen frei. Der Tower sieht den rollenden Abflug wie gewohnt im Holding und macht mit der Weiter-Taste (bzw. dem Tipp aufs Label) sofort den nächsten Schritt, ohne ihn erst übernehmen zu müssen. Dabei wird der Tower zuständig. Für die Stellen *vor* ihr bleibt der Streifen abgegeben und taucht nicht wieder auf.
- Beispiel Delivery und Tower mit StripBay, Apron und Ground ohne: Delivery gibt nach dem Startup ab (direkt an Apron). Apron und Ground arbeiten in EuroScope. Sobald der Flieger rollt, steht er beim Tower im Holding; Tippen = Line-up. Ankünfte gibt der Tower direkt an Ground ab.
- Ob eine Stelle StripBay nutzt, erkennt StripBay selbst (Anmeldung beim Einloggen). Einzustellen ist nichts.

### Verlauf je Streifen

- **History** in den Details und im Menü am Zeitstrahl-Label zeigt, was mit dem Flieger passiert ist, neueste Zeile oben: Ground State, Clearance, Zuständigkeit, Übergabe angeboten/abgelehnt, Hold Short, Line-up, Landefreigabe, Go around, Push-Richtung, Reihenfolge, Weggeschickt.
- Jede Zeile hat die Uhrzeit (UTC) und, wo es feststeht, **(you)** oder **(other)**. Bei der Zuständigkeit steht die Stelle selbst in der Zeile. Welcher andere Lotse etwas gesetzt hat, verrät EuroScope nicht.
- Der Verlauf wird bei dir mitgeschrieben, solange dein StripBay läuft (höchstens 40 Zeilen je Flieger). Er wird nicht an andere geschickt; auf dem Remote-Gerät ist er zu sehen.
- In den Details heißen die Tasten jetzt kürzer **Clearance** / **Revoke CLR** und **Remove**, damit History Platz hat.

## Neu in 4.0.0

Großes Aufräumen: eine einzige Regel für Übergaben, der Zeitstrahl nach dem DFS-Vorbild bedient, weniger Sonderwege. **Hauptversion 4:** Alle StripBay-Lotsen an einem Platz und das Remote-Gerät brauchen 4.x (geänderte Meldungen, Remote-Protokoll 2).

### Übergabe: eine Regel für alle Wege

Egal ob Weiter-Taste ▶, Ziehen, Menü, Zeitstrahl oder Befehl, es gilt immer dasselbe:

1. **Wer einen Streifen bearbeitet, ist zuständig.**
2. **Übergeben heißt anbieten.** Die nächste Stelle sieht den Streifen hervorgehoben (nach 30 s blinkend, nach 60 s mit Ton) und nimmt an: mit **ACCEPT**, mit der Weiter-Taste (jetzt ein orangefarbener Haken ✓) oder im Zeitstrahl durch Tippen aufs Label. Bis dahin bleibt der Streifen bei dir und zeigt „→ TWR“.
3. **Ablehnen oder zurücknehmen:** Der Streifen steht wieder ganz bei dir (ein rollender Abflug geht aus der Übergabe-Spalte zurück nach Taxi).
4. **Nach der Annahme** ist er bei dir noch 30 s grau zu sehen, dann weg.
5. **Die nächste Stelle hat kein StripBay** (oder hat die Zuständigkeit ausgeschaltet): Sie wird direkt eingetragen, es wartet nichts auf eine Annahme. StripBay erkennt das selbst: Jede Stelle mit StripBay meldet sich beim Einloggen an (`.SB_UN_<Stelle>`).
6. **Grauer Streifen (kommt gleich zu dir):** Weiter-Taste oder **Take over** übernimmt ihn, ohne dass die Stelle davor etwas tun muss.
7. **Ein Lotse ohne StripBay schiebt den Flieger weiter**, als deine Stelle ihn führt (z. B. Push, während Delivery noch eingetragen ist): Deine Zuständigkeit gilt dann nicht mehr, der Streifen wird grau und verschwindet. Er rutscht nirgends zurück.
8. **Delivery führt bis zum Startup.** Die Weiter-Taste übergibt danach, statt selbst zu pushen.

Weggefallen sind dafür: Streifen **anfordern** (Request/Give, `.SB_RQ_`), das **Bestätigen** für Delivery (`.SB_AK_`) und der Sonderfall, dass Delivery abgegebene Streifen minutenlang behält. Befehle: `.strips give CS [STELLE]`, `accept CS`, `reject CS`, `take CS`, `offer CS CANCEL`.

**Behoben:** Ein Resync, der gerade lief, konnte einen neueren Stand überschreiben (z. B. blieb nach dem Annehmen das alte Angebot stehen, und der nächste Schritt ging nicht). Trifft jetzt ein neuerer Merker eines anderen Lotsen ein, werden veraltete eigene Merker derselben Art nicht mehr gesendet.

### Zeitstrahl nach dem DFS-Vorbild

Grundlage ist die veröffentlichte Beschreibung der DFS-Planungsanzeige (Bergner, König, Hofmann, Ebert: „Entwurf einer integrierten Planungsanzeige für den Tower-Controller“, DGLR 2008).

- **Das ganze Label ist eine Taste:** Tippen irgendwo aufs Label macht den nächsten logischen Schritt (annehmen → Line-up → Startfreigabe → abgeben; Landefreigabe → an Ground). Die einzelnen Klickfelder im Label gibt es nicht mehr.
- **Alles Weitere im Menü am Label** (Rechtsklick, Stifttaste, Radierer oder langes Drücken): Sequence time, Hold short, Intersection bzw. Vacate via, SID, Runway, Stand, Remark, Colour, Go around, All details. Bei vielen Einträgen zweispaltig.
- **Label-Aufbau wie im Vorbild:** Statusfarbe am linken Rand, Spitze zur Achse. Zeile 1 Rufzeichen (groß) mit Status-Kürzel, Zeile 2 Typ/Wirbelschleppe und klein Zeit bzw. Slot, Hold Short und Bemerkung, letzte Zeile (gegenüber der Spitze) SID in Pastell und Ziel bzw. Stand und Herkunft. Leere Felder werden nicht mehr als Kästchen gezeigt.
- **Listen HAND OVER IN / OUT:** einzeilig mit festen Spalten (Rufzeichen, Typ, Zeit), rechteckiger Kopf rechts.
- **Nur noch das DFS-Farbschema** mit Tag/Nacht. Das Schema „Window“ und der Intersection-Vorschlag für Hannover sind entfallen.
- Unverändert: 3 min zurück, 6 min voraus, „jetzt“ links der Mitte, Nordbahn oben, Südbahn unten, Ankünfte an der Achse, Abflüge außen, Reihenfolge durch Ziehen.

### Fehlerprüfung

- Neuer **Mehr-Lotsen-Test**: mehrere StripBay-Instanzen und Lotsen ohne StripBay teilen sich die Flugdaten wie im Netz. Sieben feste Abläufe (Abflug und Ankunft durch alle Stellen, gemischt mit/ohne StripBay, ablehnen, zurücknehmen, übernehmen, Zuständigkeit aus, Zeitstrahl) werden Schritt für Schritt geprüft.
- Neuer **Zufallstest**: Lotsen arbeiten durcheinander (Tippen, Ziehen, Status aus EuroScope, Annehmen, Ablehnen, Resync, zufällige Tipps ins Fenster) für Berlin, Leipzig und Hannover. Nach jedem Schritt wird geprüft, dass alle dieselbe Zuständigkeit und dieselben Angebote haben, kein Angebot hängen bleibt und jeder Streifen bei seiner Stelle zu sehen ist.
- Aus der statischen Analyse: mögliche Division durch null bei einer Ansicht ohne Spalten abgesichert, zwei ungenutzte Variablen entfernt.

## Neu in 3.1.0

### Zuständigkeit abschaltbar

- **Neu in den Settings: Responsibility / handover On | Off** (Befehl `.strips responsibility on|off`, gespeichert als `Responsibility`).
- **Off:** StripBay trägt **niemanden als zuständig** ein, auch nicht, wer einen Streifen bearbeitet (verschieben, Status, Freigaben).
  - Die Streifen stehen nur nach ihrem Ground State in den Spalten. Nichts wird mehr nach Zuständigkeit ausgeblendet oder grau angekündigt.
  - Das Zuständigkeitsfeld oben rechts am Streifen, die Zeile RESPONSIBLE in den Details sowie Übergeben, Annehmen, Ablehnen und Anfordern sind weg. Es gibt auch kein Blinken und keinen Ton für Angebote.
  - Beim Ausschalten gibt StripBay deine bestehenden Zuständigkeiten für alle frei (`.SB_RS_NONE`) und nimmt deine offenen Angebote und Anforderungen zurück.
  - Die Einstellung gilt für deine StripBay. Lotsen mit eingeschalteter Zuständigkeit können sich weiter eintragen, bei dir zählt das nicht.
- **On:** wie bisher.
- **Behoben: Push eines anderen Lotsen landet nicht mehr in PENDING.** Warst du noch als zuständig eingetragen und hat ein anderer Lotse den Flieger weitergeschoben (ohne StripBay oder mit abgeschalteter Zuständigkeit), stand der Streifen bei dir in der ersten Spalte, bei Delivery also in PENDING. Jetzt kommt er in die Spalte, die seinem Stand am nächsten ist (bei Delivery STARTUP), und rutscht nie zurück an den Anfang. Das gilt ebenso für die anderen Stationen.
- StripBay Remote 3.0 verbindet sich weiter mit 3.1. Damit auch das Gerät die Felder ausblendet, dort ebenfalls 3.1 verwenden (Ordner **Remote** neu kopieren).

## Neu in 3.0.1

### StripBay Remote: Verbindung robuster, Fehler mit Grund

- **Grund statt „keine Antwort“:** Das Verbinden-Fenster und die Karte „NOT CONNECTED“ nennen jetzt, woran es liegt:
  - „… answers, but StripBay Remote is not on there“: Der PC ist erreichbar, aber in EuroScope ist Remote aus → `.strips remote on`.
  - „No answer from …“: fast immer die **Windows-Firewall** am EuroScope-PC → **REMOTE-FIREWALL.bat** (siehe unten).
  - „… not in the network of this device“: Die Adresse gehört zu einem anderen Netz (VPN, Gast-WLAN).
  - „PC … not found“: Den Namen kennt das Netz nicht → IP-Adresse eingeben (`.strips remote status`).
- **Neu: REMOTE-FIREWALL.bat** (neben INSTALL.bat), einmal am EuroScope-PC ausführen. Mit Administratorrechten:
  - erlaubt eingehend TCP 47810 und UDP 47811 in privaten Netzwerken,
  - findet Sperren der Firewall für EuroScope (entstehen, wenn die Firewall-Abfrage einmal mit „Abbrechen“ geschlossen wurde) und schaltet sie auf Wunsch ab,
  - bietet an, ein als „öffentlich“ eingestuftes Heimnetz als „privat“ einzustufen,
  - zeigt die Adresse des PCs.
  - Bei anderem Port: `REMOTE-FIREWALL.bat 47820`.
- **Search** ruft jetzt auf jedem Netzwerkadapter, nicht nur auf dem ersten. Vorher konnte die Suche mit VPN, Hyper-V oder WSL ins Leere gehen.
- **Adresse in EuroScope:** `.strips remote status` und die Settings zeigen zuerst die Adresse im Heimnetz, virtuelle Adapter nicht mehr.
- **Neue IP-Adresse:** Bekommt der EuroScope-PC vom Router eine andere IP, findet das Gerät ihn nach zwei Fehlversuchen über seinen Namen wieder.
- **Karte „NOT CONNECTED“ antippen:** öffnet das Verbinden-Fenster für einen anderen PC oder einen neuen Code.
- **Selbsttest:** StripBayRemote.exe am EuroScope-PC starten und als PC `localhost` eingeben. Klappt das, liegt es am Netz oder an der Firewall.
- Protokoll unverändert: 3.0.1 verbindet sich auch mit 3.0.0.

## Neu in 3.0.0

### StripBay Remote: die Streifen auf einem weiteren Windows-Gerät

Neues Programm **StripBayRemote.exe** für einen zweiten Windows-PC, einen Laptop oder ein Windows-Tablet im selben Netz (WLAN oder LAN). Es zeigt dasselbe Streifenfenster wie in EuroScope; es ist derselbe Code, alle Funktionen sind da.

- **Synchron ohne Abstriche:** Das Gerät ist nur Anzeige und Eingabe. Jede Aktion (Ziehen, Freigaben, Felder, Hold Short, Farben, Zeitstrahl, CPDLC, Papierkorb …) geht an StripBay in EuroScope und läuft dort durch denselben Code wie ein Tipp am PC, inklusive Abgleich mit allen anderen Lotsen über das Scratchpad. Der Zustand kommt bei jeder Änderung zurück, im Netz in wenigen Millisekunden.
- **Einrichten:**
  - `INSTALL.bat` baut jetzt auch StripBayRemote.exe und legt sie in den Ordner **Remote**.
  - In EuroScope `.strips remote on` (oder Settings → **Remote (other PC)**). Der Chat zeigt Adresse und 6-stelligen Code, die Settings unter **Remote pairing**.
  - Fragt Windows nach der Firewall: EuroScope für **private Netzwerke** zulassen.
  - Ordner **Remote** auf das andere Gerät kopieren und StripBayRemote.exe starten. **Search** findet EuroScope im Netz (sonst PC-Name oder IP eingeben), Code eingeben, **Connect**.
  - Danach verbindet sich das Gerät beim Start von selbst.
- **Was je Gerät gilt:** Zoom, Layout, Hell/Dunkel, Vollbild, „immer oben“ und die Fensterposition. Gespeichert wird das in `%APPDATA%\StripBay\StripBayRemote.ini`.
- **Was für die Sitzung gilt:** Ansicht und Bereich (Delivery, Apron, Ground, Tower, Nord/Süd) bestimmen, welche Stelle du bist (Zuständigkeit, Angebote, nächste Frequenz). Wechselst du sie am Gerät, zieht das Fenster am PC mit und umgekehrt.
- **Töne und Meldungen** von StripBay kommen auch auf dem Gerät, die Meldungen unten im Fenster.
- **Verbindung weg:** Das Fenster wird abgedunkelt („NOT CONNECTED“) und nimmt nichts an, damit keine Eingabe verloren geht. Alle 3 s verbindet es sich neu.
- **Sicherheit:** Ohne den Code wird nichts angenommen. `.strips remote code new` erzeugt einen neuen Code und trennt alle Geräte. Ohne `remote on` hört StripBay gar nicht ins Netz.
- **Befehle:** `.strips remote on | off | status | code new | code 123456 | port 47820 | disconnect`.
  - Standard-Port: TCP 47810, für die Suche UDP 47811.
  - Bei einem anderen Port am Gerät `PC:47820` eingeben.
- Plugin und Remote müssen aus demselben Paket stammen (gleiche Hauptversion). Sonst sagt das Gerät, welche Version EuroScope hat.

### Neues Dunkelschema

- Neutrales Anthrazit um **#212326**: obere Leiste genau in diesem Ton, der Grund eine Stufe dunkler, Bays, Karten und Tasten je eine Stufe heller. Die Papierstreifen sind leicht gedämpft, damit sie nachts nicht blenden.
- Der Zeitstrahl im Schema **Window** nutzt dieselben Töne. Das DFS-Schema bleibt eigenständig.
- **Pisten mit CAUTION:** Kopf gelb mit **dunkler Schrift**, auch die Zahl rechts, damit sie im Dunkelschema lesbar bleibt.

### Neue Versionsnummern und Dateinamen

- Ab jetzt **MAJOR.MINOR.PATCH**, jede Lieferung bekommt eine neue Nummer:
  - **MAJOR:** Der Abgleich zwischen Lotsen oder das Remote-Protokoll ändert sich. Alle StripBay-Lotsen und das Remote-Gerät brauchen dieselbe Hauptversion.
  - **MINOR:** neue Funktionen, verträglich.
  - **PATCH:** nur Fehlerbehebungen.
- Dateiname ohne Platz: `StripBay-3.0.0.zip`. Berlin, Leipzig und Hannover stecken alle im selben Paket.

## Neu in 2.0.85

- **Übergaben an mich:** Ein angebotener Streifen ist zuerst nur farbig markiert, **blinkt erst nach 30 s** und gibt **erst nach 60 s einmal einen Ton**.
  - Einstellen in den Settings (**Handover blinks after**: 10/30/60 s, **Handover sound after**: 60/90/120 s oder aus) oder mit `.strips offerblink SEKUNDEN` (0 = sofort) und `.strips offerremind SEKUNDEN` (0 = kein Ton).
  - Der Ton kommt immer nach dem Blinken. Wer ihn gleich oder früher einstellt, bekommt ihn 30 s nach dem Blinken.
  - Wer noch die alte Erinnerung nach 30 s gespeichert hatte, bekommt automatisch 60 s.
- **Staffelungshilfen aus 2.0.84 wieder entfernt:** kein roter Balken an der Achse, kein roter Rahmen ums Zeitfeld, kein Hinweis und kein "Fit spacing" im Menü, kein **SEP** im Zeit-Tastenfeld. Die Mischbetriebs-Prüfung (Start und Landung auf derselben Piste) ist ebenfalls weg.
- **Geschätzte Zeiten (EST)** für Abflüge ohne TTOT bleiben, ohne Wirbelschleppen- und SID-Regeln.
  - Frühestens: am Rollhalt 1 min, rollend 2:30, noch bei Ground 4 min.
  - Je Piste kommt jede weitere Schätzung mindestens 1 min hinter die vorige, damit die Labels lesbar nebeneinander stehen.
  - Abstellen wie bisher mit `.strips timeline estimate off`.
- Hannover mit Nord- und Südbahn, Pistenchip, VFR-Anzeige und Standard-Intersection-Vorschlag bleiben wie in 2.0.84.

## Neu in 2.0.84

- **Hannover im Zeitstrahl mit beiden Bahnen:** oben die **Nordbahn 09L/27R**, unten die **Südbahn 09R/27L**, je Bahn außen die Abflüge, an der Achse die Ankünfte. Die Seite hängt allein an der Piste im Flugplan (ein Tower, keine Aufteilung nötig).
  - **09C/27C** (nur VFR unter 5,7 t) läuft bei der Nordbahn mit: Sie liegt direkt daneben, und Abflüge von 09L/27R kreuzen sie. Sie hat links einen eigenen kleinen Kopf, Antippen schaltet ihren Status (frei, Caution, Closed) unabhängig von 09L/27R.
  - Der Kopf jeder Seite zeigt die aktive Piste aus EuroScope (z. B. **27R NORTH**, **27L SOUTH**).
  - **Helipad H2/H3 und Area** bleiben eigene Spalten rechts neben dem Zeitstrahl.
  - **Auf die andere Bahn:** im Menü **→ Runway 27L SOUTH** oder das Label auf die andere Seite ziehen. StripBay setzt dann die Piste der anderen Bahn in derselben Richtung (27R ↔ 27L, 09L ↔ 09R), die Bay bleibt.
  - Der Pistenstatus nutzt dieselben Namen wie ohne Zeitstrahl ("RWY 09L/27R"). Er bleibt damit für alle Lotsen gleich, auch wenn einer ohne Zeitstrahl arbeitet.
  - Bei Abflügen ohne Intersection steht die **Standard-Intersection** aus der VATSIM-SOP als Vorschlag im INT-Feld, z. B. "N?" (27R L/M), "M?" (27R H), "A?" (27L M/H), "B?" (27L L), "E?"/"D?" (09R), "J/K?"/"H/G?"/"K?" (09L).
- **Staffelungsprüfung je Piste:** Ist eine Bewegung zu dicht hinter ihrem Vorgänger auf derselben Piste, erscheint ein **roter Balken** an der Achse und ein roter Rahmen um das Zeitfeld. Das Menü nennt den Grund, z. B. "2:00 needed behind CXI3A (now 1:00)".
  - **Abflüge**, zeitbasiert nach ICAO:
    - 2 min hinter HEAVY (für M/L)
    - L hinter M: 2 min
    - hinter A388 (J): 2 min für H, 3 min für M/L
    - von einer Intersection hinter einem Start von der vollen Länge: 1 min mehr
    - sonst 1 min, bei gleichem erstem SID-Punkt 1:30
  - **Anflüge:** Radar-Wirbelschleppenstaffelung in NM, mindestens 3 NM, über die Geschwindigkeit in Zeit umgerechnet.
  - **Mischbetrieb:** Ein Start braucht die Piste 1:30 vor einer Landung auf derselben Piste frei und 1:00 danach.
  - Die Werte sind eine Planungshilfe, die Staffelung bleibt Sache des Lotsen.
- **Gestaffelt einreihen:** Im Menü **Fit spacing → hhmm** (erscheint nur bei einem Konflikt) oder im Zeit-Tastenfeld **SEP**. Der Flieger bekommt dann die früheste Zeit hinter seinem Vorgänger, die auch nach hinten und zu Landungen passt. Die Zeit wird wie jede Reihenfolge an alle StripBay-Lotsen synchronisiert.
- **Abflüge ohne TTOT oder Slot** kommen jetzt mit einer **geschätzten Zeit (EST)** auf den Zeitstrahl, statt in HAND OVER IN zu warten.
  - Frühestens: am Rollhalt 1 min, rollend bei mir 2:30, noch bei Ground 4 min.
  - Danach reiht StripBay sie in die erste Lücke ein, in der der Abstand vor und hinter passt, auch zu Landungen (ein kleines DMAN).
  - Die Verbindungslinie ist gepunktet, das Zeitfeld blass.
  - Abstellen mit `.strips timeline estimate off` oder in den Settings (**Timeline: no TTOT**).
- **Pistenchip im Label:** Weicht die Piste von der aktiven Piste der Seite ab (Hannover 27C, Pistenwechsel), steht sie vorn im Label. Antippen öffnet die Pistenliste. Fehlt die Piste, steht dort orange "RWY?".
- **VFR-Abflüge** zeigen "VFR" statt eines orangen "NO SID".
- **Labels bei viel Verkehr:** Ist über dem Zeitpunkt kein Platz, rückt das Label seitlich aus, die Linie zeigt weiter genau auf die Zeit. Das Zeitstrahl-Feld bekommt außerdem mehr Breite gegenüber den Spalten daneben.

## Neu in 2.0.83

- **Zeitstrahl im DFS-Farbschema:** dunkelgrauer Grund wie bei der echten Planungsanzeige, das ganze Fenster wird dunkel (auch wenn sonst Hell eingestellt ist). Ankünfte dunkelgrau mit weißer Schrift, Abflüge hellgrau mit schwarzer Schrift, Status in kräftigen Farben, SID-Gruppen in Pastell. Oben **Night / Day** (wie in der Statusleiste der DFS-Anzeige): Nacht = fast schwarz, alles gedimmt, blendfrei. Befehle `.strips timeline night on|off`, `.strips timeline day`; das alte Schema (wie das Fenster) mit `.strips timeline theme classic`, zurück mit `theme dfs`. Beides auch in den Settings (**Timeline theme**).
- **Klickfelder im Label:** jedes Feld ist ein eigenes Kästchen, die Maus darüber zeigt einen blauen Rahmen.
  - **Statusbalken links, Spitze und Status-Kürzel** (und freie Fläche) = nächster Schritt (Line-up, Start-/Landefreigabe, an Departure, an Ground).
  - **Rufzeichen** = Menü am Label.
  - **Typ** = Farbe.
  - **Zeit** = Reihenfolge (Tastenfeld −2, −1, +1, +2, +5 min, NOW, None = zurück).
  - **HS** = Hold Short.
  - **Bemerkung** (auch leer) = Text schreiben.
  - **SID** bzw. **Stand** = Liste.
  - **INT** bzw. **TWY** = Intersection.
  - Rechtsklick, Stifttaste, Radierer und langes Drücken öffnen weiter überall das Menü.
- **Reihenfolge durch Ziehen** (wie bei der DFS): ein Label auf seiner Seite entlang der Zeitleiter ziehen. Eine gestrichelte Linie und ein blaues Kästchen auf der Achse zeigen die neue Zeit (auf 10 s); loslassen setzt sie. Das Label zeigt dann **PLN hhmm** (blau) statt ETA/TTOT, auch in den Listen. Gilt für Ankünfte und rollende Abflüge (nicht für Abflüge auf der Piste) und wird mit `.SB_TP_` an alle StripBay-Lotsen synchronisiert. Zurück: Menü **Reset sequence**, Zeitfeld → **None** oder `.strips tlplan CALLSIGN NONE`. Auf die andere Seite gezogen wechselt wie bisher nur die Piste.
- **Bereiche wie bei der DFS benannt:** rechts **HAND OVER IN** (kommt später bzw. hat noch keine Zeit), links **HAND OVER OUT** (gestartet bzw. gelandet und noch nicht abgegeben), je Piste und Abflug/Ankunft, z. B. "HAND OVER IN 24L ARR".
- Schmale Fenster: passt die Uhr oben nicht mehr in die erste Zeile, steht sie rechts in der zweiten.
- Neuer Befehl `.strips tlplan CALLSIGN HHMMSS|NONE` (auch `sequence`). Einstellungen `TimelineTheme` und `TimelineNight` werden gespeichert und exportiert.

## Neu in 2.0.82

- **Tower-Zeitstrahl nach der DFS-Planungsanzeige** (WFF TWR-HMI von DFS, delair und TU Darmstadt, iF design award 2009, für Frankfurt, München und Berlin gebaut). Im Tower oben **Timeline** antippen (oder `.strips timeline on`). Holding, Runway und Final werden ein gemeinsames Feld; Taxi In, Startup/Push und Delivery (bei Top-down) bleiben als Spalten daneben.
  - **Zeitachse waagerecht**, 3 min zurück bis 6 min voraus, **Jetzt** (rote Linie) bei einem Drittel. Abstand zwischen zwei Labels = **Zeit**, nicht Entfernung; die Labels wandern langsam auf Jetzt zu, eine dünne Linie zeigt den genauen Zeitpunkt auf der Achse.
  - **Berlin:** Nordbahn (**24R/06L**) oberhalb, Südbahn (**24L/06R**) unterhalb der Achse; je Bahn außen die Abflüge, an der Achse die Ankünfte. Nur Nord oder Süd gewählt: eine Bahn, Abflüge oben, Ankünfte unten.
  - **Zeit eines Fliegers:** Ankunft = ETA (Entfernung / Geschwindigkeit), nach der Landung die Landezeit; Abflug am Rollhalt = **TTOT** (vACDM/CDM), sonst der Slot; Line-up/Startfreigabe = jetzt; in der Luft = Startzeit. TTOT vorbei und noch nicht auf der Piste: rote Ecke.
  - **Label wie bei der DFS:** Kopf als Spitze zur Achse; Ankunft dunkelgrau, Abflug hellgrau; Status als kräftiger Balken links und Kürzel (TAXI/HOLD, L/U, T/O, AIR, FIN, LDG, LND, G/A, grau GND/APP = noch bei Ground bzw. Anflug); Rufzeichen groß, Typ und Wirbelschleppe; Zeit bzw. **CTOT** (Slotfarbe), Hold Short, Remark; gegenüber der Spitze **SID** (Pastellfarbe je SID-Gruppe) und Ziel bzw. **Stand** und Herkunft.
  - **Rechts** einzeilig, was später kommt oder keine Zeit hat (je Bahn und Richtung), **links** „Hand Over Out“ (seit über 3 min gestartet/gelandet und noch nicht abgegeben) und der **Pistenkopf**: antippen = frei → CAUTION → CLOSED (gesperrt: rot schraffiert, kein Line-up).
  - **Antippen = nächster Schritt:** rollend → Line-up, Line-up → Startfreigabe, in der Luft → abgeben (FREQ), Final → Landefreigabe, gelandet → an Ground (Taxi In), angeboten → annehmen.
  - **Menü direkt am Label:** Rechtsklick, **Stifttaste oder Radierer**, langes Drücken (Touch). Darin Landefreigabe, Go around, Startfreigabe, zurück zum Holding, Hold short, Intersection, Stand, andere Piste, Remark, Farbe, alle Details. Vorankündigungen (grau) öffnen beim Antippen das Menü (Request).
  - **Ziehen** nach oben bzw. unten wechselt die Piste (Nord ↔ Süd), die Bay bleibt.
  - **Day / Night** oben direkt umschaltbar. Fenster ändern: `.strips timeline range 3 6` (Minuten zurück / voraus) oder in den Settings.

## Neu in 2.0.81

- **PDC halbautomatisch:** Fragt ein Pilot per PDC an und sind alle Daten da, zeigt das CLR-Feld eine grüne, blinkende Taste **PDC** – ein Tipp sendet. Fehlt etwas (oder Hoppie ist nicht verbunden), öffnet sich wie bisher das Fenster und zeigt, was fehlt.
- **Startup-Vorschlag (CDM):** Pending und On Freq sind nach **TSAT** sortiert (früheste oben, ohne TSAT unten). Ist das TSAT-Fenster erreicht (±5 min), blinkt der Strip grün umrandet. Ein-/ausschalten: `.strips tsatsort on|off`.
- **TOBT an vACDM:** TSAT-Feld antippen → **NOW / +5 / +10 / +15 / +20 / +30**. StripBay meldet die TOBT bestätigt an den vACDM-Server (wie das vACDM-Plugin, PATCH /api/v1/pilots/…); vACDM rechnet die TSAT neu. Befehl: `.strips tobt CS NOW|+MIN|HHMM`. Hinweis: der vACDM-Server muss Änderungen von dir annehmen (beim vACDM-Plugin: `.vacdm master`).
- **Eigene Plätze aus Datei:** `StripBayAirports.txt` neben der StripBay.dll (Vorlage: `.strips airportfile template`, neu laden: `.strips airportfile reload`). Je Platz `[ICAO]` mit city, runways, split, rwyA/rwyB, apron, splitGnd, autoHandoffFt, dep/depA/depB, arr/arrA/arrB, twr/twrA/twrB, freqStand, freqDel, gndStands, holdShort, intA/intB, sidFreq. Plätze aus der Datei gehen den eingebauten vor.
- **Einstellungen exportieren/importieren:** `.strips export` schreibt alle StripBay-Einstellungen nach `StripBaySettings.txt` neben der DLL (ohne Hoppie-Code; mit: `.strips export code`), `.strips import` liest sie ein (danach StripBay neu laden für Fenster und Ansicht).
- **Protokoll für die Fehlersuche:** `.strips log on` schreibt `StripBay.log` neben die DLL (alle 5 s, die letzten 5000 Zeilen): Meldungen, gesendete und empfangene Sync-Nachrichten, Ground-State-Wechsel, Verschiebungen. `.strips log off` beendet es.

## Neu in 2.0.80

- **Pistenstatus getrennt vom Strip:** Kopf der Runway-Bay antippen schaltet **frei → CAUTION (gelb) → CLOSED (rot)**. Es entsteht kein Strip mehr. **CLOSED sperrt die Piste:** die Bay ist schraffiert, Flieger lassen sich nicht mehr auf die Piste ziehen.
- **Designator-Strip** (Trennstreifen, wie die „designator strips“ auf Papier-Strip-Boards): Button **+ Strip** → Text eingeben → **Designator**. Ohne Text heißt er „RUNWAY OBSTRUCTED“. Er ist ein farbiges, schraffiertes Band, lässt sich in jede Bay ziehen (auch auf eine gesperrte Piste), nur lokal. Mit OBSTRUCTED/CLOSED/VEHICLE/BLOCK im Text rot. Befehl: `.strips designator BAY TEXT`.
- **Getrennte Pisten:** Strip auf die andere Pistenseite ziehen geht jetzt wirklich (vorher blockiert) und ändert die Piste im Flugplan.
- **Übergabe ablehnen:** Im Menü oben rechts bzw. in den Details **REJECT**. Beim Anbietenden steht 30 s rot „REJECTED …“ mit Ton. Befehl: `.strips reject CS`.
- **Erinnerung:** Ist ein Angebot an dich lange nicht angenommen, kommt einmal ein Ton und eine Meldung (seit 2.0.85 nach 60 s, Blinken nach 30 s). Zeit: `.strips offerremind SEKUNDEN` (0 = aus).
- **Töne je Ereignis:** request (CPDLC-Anfrage), wilco, unable, offer (Übergabe). Auswahl: triple, chime, double, low, single, off. `.strips sound offer chime`, Übersicht `.strips sounds`, Probehören `.strips beep wilco`.
- **Push-Richtung:** In Startup und Pushback das Feld **PB** antippen → N / E / S / W (Nase). Für alle synchron (`.SB_PB_`), Befehl `.strips pushdir CS N`.
- **Spaltenbreiten:** Rand zwischen zwei Bays ziehen (Maus: Pfeil ↔). Gespeichert je Ansicht; zurück mit `.strips colw reset`.
- **Rückgängig:** Button **↶ Undo** oben (erscheint nach einer Aktion) oder `.strips undo`: macht Verschieben (mit Zuständigkeit), Entfernen, Übernehmen und Angebote der Reihe nach rückgängig (die letzten 10).

## Neu in 2.0.79

- **Angebotene Strips blinken:** Ein Strip, der dir zur Übergabe geschickt wird, ist zuerst leicht orange; nimmst du ihn nach 5 Sekunden noch nicht an, blinkt er, bis du oben rechts auf ACCEPT tippst.

## Neu in 2.0.78

- **Startup wird wieder gesetzt (und Strips springen nicht mehr zurück):** Seit 2.0.68 merkte sich StripBay den „echten“ Scratchpad-Text, um ihn nach Sync-Nachrichten zurückzuschreiben. Dabei konnte ein Ground-State-Befehl wie „NSTS“ oder „ST-UP“ (so verteilt EuroScope Statuswechsel) als Text hängen bleiben – beim Zurückschreiben stellte er den Status wieder um (z. B. Startup → Pending). Solche Befehle gelten jetzt nie als Scratchpad-Text.

## Neu in 2.0.77

- **Berlin: ein Apron für beide Seiten – Frequenz je Seite.** Ist nur ein Apron online (z. B. nur EDDB_A_GND oder nur EDDB_E_GND), gilt trotzdem für die Nordseite 129.605 und für die Südseite 121.855 (wie beim Tower). Erst wenn gar kein Apron da ist, kommt Ground der Seite. Ketten können dafür jetzt auch „Frequenz=Frequenz“ enthalten (`121.855=129.605`: wer auf 121.855 ist, wird mit 129.605 angezeigt). Eigene, geänderte Ketten bleiben; neu: `.strips freq reset`.

## Neu in 2.0.76

- **DEL: keine alten Strips mehr in PENDING.** Abgegebene (graue) Strips erscheinen nur noch in der Spalte ihrer Bay – ein Flieger, den Apron schon gepusht hat, landet bei DEL nicht mehr in PENDING. DEL hält einen Strip nur, solange er noch in einer DEL-Bay steht (kein Status, On Freq, Startup). Nimmt die nächste Stelle die Übergabe an, verschwindet er bei DEL sofort.

## Neu in 2.0.75

- **Neuer CPDLC-Ton:** drei kurze hohe Pieptöne (fest eingebaut, unabhängig vom Windows-Soundschema). Probehören: `.strips beep`. Abschalten wie bisher in den Einstellungen bzw. `.strips cpdlc sound off`.

## Neu in 2.0.74

- **Berlin: Main Apron macht Apron Nord mit.** Ist kein EDDB_E_GND (Apron Nord, 129.605) online, übernimmt EDDB_A_GND (Main Apron, 121.855): nächste Frequenz für Delivery (A-/E-Stands, Nordbahn ohne Stand) und für Ankünfte Nord, nächste Stelle beim Übergeben (APN S statt APN N).
- **E_GND zählt als Apron**, nicht als Ground (Ground online, Top-down, CPDLC-Reihenfolge).

## Neu in 2.0.73

- Feld **SP** auf dem Strip heißt jetzt **REMARK** (Inhalt weiterhin das Scratchpad; antippen öffnet den Editor).

## Neu in 2.0.72

- **Delivery: FREQ = nächste Stelle.** Nach dem Stand (A/E → Apron Nord, B/C/D → Main Apron, GAT/Apron 1 → Ground Nord, W → Ground Süd), jeweils nur wer online ist, sonst Ground und Tower der Bahn. Ist der Stand unbekannt, gilt die Pistenseite (Apron → Ground → Tower) – nicht mehr gleich das Radar.

## Neu in 2.0.71

- **Tower + GND (Top-down):** TAXI und HOLDING wieder in einer Spalte („TAXI / HOLDING“). Ganz rechts TAXI IN, darunter STARTUP / PUSH (mit Apron online nur GAT, Apron 1, W, klein).
- **CPDLC nur bei der untersten Station:** Es verbindet immer nur die unterste Station am Platz mit Hoppie (DEL vor Apron vor Ground vor Tower; bei gleichem Rang der alphabetisch erste Callsign). Alle anderen verbinden nicht und melden das einmal im Chat.
- **Apron:** Bereichsauswahl nur „Combined“, „South“, „North“ (ohne Pistennamen).
- **Delivery:** keine Bereichsauswahl mehr – Delivery sieht immer beide Seiten.

## Neu in 2.0.70

- **Tower + GND (Top-down):** statt STARTUP eine Spalte **TAXI** (je Piste), HOLDING enthält dann nur die an den Tower übergebenen. In Berlin bei Apron online darunter klein STARTUP / PUSH für GAT, Apron 1, W. Ist in Berlin kein Apron online (Tower + GND + APN), bleibt STARTUP.
- **Anfordern übers Feld oben rechts:** Ist ein anderer zuständig, steht im Menü ganz oben „REQUEST from …“ (nochmal: „UNREQUEST“).

## Neu in 2.0.69

- **Übergabe neu (Feld oben rechts):** Das Zuständigkeits-Feld oben rechts am Strip öffnet das Menü zum Übergeben. Die gewählte Stelle bekommt ein **Angebot** (`.SB_HO_`): bei ihr ist der Strip leicht orange hinterlegt, das Feld zeigt **ACCEPT**. Erst wenn sie dort antippt (oder den Flieger bearbeitet), wechselt die Zuständigkeit – beim Abgebenden wird der Strip dann als übergeben (grau) markiert. Bis dahin zeigt das Feld „GND → TWR“; im Menü steht „CANCEL handover“. Details: „Hand over → …“ / „Accept handover“. Befehle: `.strips offer CS UNIT|CANCEL`, `.strips accept CS`.
- **Ground mit Top-down:** kein grauer Startup mehr, stattdessen rollende Flieger einer anderen Stelle grau in TAXI.
- **Stand Auto:** zuerst das „Auto“ des Ground Radar Plugins; nur wenn GRP nicht antwortet, wählt StripBay selbst (und zählt dabei auch die Stände anderer Strips als vergeben).
- **Tower: CTOT immer farbig** hinterlegt (sonst TTOT).
- **Andere Piste per Ziehen:** Strip auf die Spalte der anderen Pistenseite ziehen (Final, Runway, Holding) – die Piste im Flugplan wird angepasst (`.strips rwygroup CS A|B`). Hannover wie bisher je Pistenbay.
- **Automatisch geparkt:** STD-Haken gesetzt und der Flieger steht im Stand → PARK (macht die zuständige Stelle).
- **Von Hand erstellte Strips** sind immer zu sehen, in der passenden Spalte der aktuellen Ansicht.
- **Einfärben:** Flugzeugtyp auf dem Strip antippen → Farbwahl (synchron).

## Neu in 2.0.68

- **Weniger Unruhe im Netz und im Chat:**
  - Automatische Meldungen (CPDLC-Status, Sync-Warnungen, Ground State usw.) erscheinen höchstens einmal je Minute und höchstens 4 je 10 s – der EuroScope-Chat springt nicht mehr ständig auf.
  - Die Merker-Runde nach einem Login antwortet höchstens alle 30 s, leicht zeitversetzt; On Freq ohne Zuständigkeit schickt nur Delivery (ohne DEL online die anderen). Um die Runde gebeten wird nur, wenn andere Lotsen da sind.
- **Scratchpad-Text bleibt erhalten:** Stand beim Senden gerade die Sync-Nachricht eines anderen Lotsen im Scratchpad, schrieb StripBay danach ein leeres Scratchpad zurück. Jetzt kommt der letzte echte Inhalt zurück.
- **Kein Sync-Text auf dem Streifen:** „.SB_…“-Nachrichten erscheinen nicht mehr kurz im Scratchpad-Feld des Streifens.

## Neu in 2.0.67

- **Ground sieht Startups grau:** Flieger im Startup bei Delivery/Apron erscheinen bei Ground schon grau in der Spalte STARTUP (bzw. STARTUP / PUSH).
- **Push durch Ground:** Der Streifen springt bei Delivery nicht mehr nach PENDING. Sobald die nächste Stelle ihn bewegt oder bestätigt, ist er bei DEL sofort weg.

## Neu in 2.0.66

- **On Freq beim Login synchron:** Wer sich einloggt (oder StripBay neu lädt), bittet die anderen einmal per Scratchpad (`.SB_SQ_`) um ihre Merker. Alle mit StripBay schicken dann Zuständigkeit, Hold Short usw. ihrer Flieger und **On Freq** – auch für Flieger, für die noch niemand zuständig ist. Das passiert auch, wenn ein neuer Lotse online kommt.

## Neu in 2.0.65

- **Flackern behoben:** Fehlt EuroScope der Ground State, hält StripBay den Streifen nur noch bei sich fest und schreibt den Status nicht mehr zurück nach EuroScope (das konnte sich mit dem Netz hochschaukeln). Neue Lotsen lösen die Merker-Runde per Scratchpad höchstens einmal je Minute aus.
- **Ground ohne Top-down:** An Plätzen ohne Apron (Hannover, Leipzig) hat Ground immer STARTUP und PUSHBACK. In Berlin heißt die kleine Spalte jetzt „STARTUP / PUSH“ (GAT, Apron 1, W).

## Neu in 2.0.64

- **Hannover Intersections:** Nordbahn 09L/27R: N, M, L, KTO, K, J, H, G – Südbahn 09R/27L: A, B, C, D, E. Die Auswahl richtet sich nach der Piste des Fliegers (09C: beide).

## Neu in 2.0.63

- **PDC:** Unter NEXT FREQ steht immer die Frequenz dessen, der die PDC schickt (eigene Primärfrequenz: DEL, GND oder TWR) – also die Stelle, die den Anruf beantwortet.

## Neu in 2.0.62

- **Delivery behält abgegebene Strips:** Gibt DEL einen Abflug an Apron/Ground/Tower ab, bleibt der Strip bei DEL grau stehen – ohne Zeitlimit –, bis die nächste Stelle ihn **bewegt** (Ground State ändert sich, z. B. Push) oder **bestätigt** (einmal antippen; synchron per `.SB_AK_`). Erst dann verschwindet er bei DEL.

## Neu in 2.0.61

- **Strips fallen nicht mehr nach PENDING:** Verliert EuroScope den Ground State eines Abflugs (Feld plötzlich leer, ohne dass jemand ihn zurückgesetzt hat), bleibt der Streifen in seiner Bay (Meldung „EuroScope has no ground state … strip kept in place“; ab 2.0.65 wird nichts zurück nach EuroScope geschrieben). Wer bewusst nach PENDING zieht, setzt ihn wirklich zurück – synchron für alle mit StripBay.
- **NOFP:** Nur neue Streifen ohne Flugplan werden unterdrückt; ein vorhandener Streifen verschwindet nicht mehr.

## Neu in 2.0.60

- **Hannover (und Leipzig):** Die Stand-Auswahl hat keinen „Apron 2“-Ordner mehr – die Stands stehen direkt da, ohne Reiter.

## Neu in 2.0.59

- **Nächster Lotse mit einem Tipp:** In den Details gibt es den Knopf „→ GND“ (bzw. TWR, APN …) – macht die nächste Stelle zuständig. Auch in der Auswahl „Responsible“ steht sie ganz oben. Befehl: `.strips nextresp CALLSIGN`.
- **Inbound/Outbound von Hand:** Knopf „Make inbound“ / „Make outbound“ in den Details (synchron). Befehl: `.strips inbound|outbound|flip CALLSIGN`.
- **Diversions nur noch von Hand:** Knopf „Diversion“ in den Details trägt den Flieger als Ankunft an deinem Platz ein (synchron, DIV-Pill). Die automatische Erkennung ist raus. Befehl: `.strips divert CALLSIGN [ICAO|OFF]`.
- **Final grau**, solange ein anderer Lotse (Anflug) den Flieger assumed hat.
- **Flieger ohne Flugplan (NOFP)** bekommen keinen Streifen.
- **Hannover:** Tower-Pisten wie auf der Karte untereinander (09L/27R oben, 09C/27C flach in der Mitte, 09R/27L unten), daneben Helipad H2/H3 über AREA. Ohne Apron heißt die Ground-Ansicht nur „Ground“ (kein „+ Apron“).

## Neu in 2.0.58

- **Hannover:** Gelandete Ankünfte werden nicht mehr automatisch von der Piste nach Taxi In gezogen – der Tower zieht selbst.
- **Tippen trifft das Oberste:** Knöpfe in Fenstern (z. B. die Farbknöpfe in den Details) haben Vorrang vor Elementen darunter; der Kopf einer Runway-Bay reagiert nicht mehr, solange ein Fenster offen ist.

## Neu in 2.0.57

- **Notfall-Squawk:** Streifen mit 7500, 7600, 7601 oder 7700 (Transponder oder zugewiesen) bekommen einen dicken roten Rand.

## Neu in 2.0.56

- **PDC statt CPDLC:** Datalink nur noch für PDC (Pre-Departure Clearance) über Hoppie. CPDLC-Logons werden mit "UNABLE PDC ONLY" abgelehnt. Format wie an den Plätzen der FIR Bremen (TopSky): `CLD 1215 261101 EDDB PDC 001 @EWG4AB@ CLRD TO @EGLL@ OFF @24R@ VIA @KLF2X@ CLIMB @5000FT@ SQUAWK @1000@ ADT @1230@ NEXT FREQ @120.340@ ATIS @C@` (ADT = TSAT, bei Slot CTOT). Das Fenster heißt "PDC" mit Send/Resend PDC und "PDC: call on voice".
- **Farblich hervorheben:** In den Details oben rechts Farbknöpfe (keine, gelb, grün, blau, rot, lila); das Streifenpapier bekommt die Farbe, synchron bei allen (`.SB_HL_`, Befehl `.strips hl CALLSIGN 0-5`).
- **Fenster-Knopf:** im Vollbild "Exit full" statt "Window"; die Einstellungszeile heißt "Strip window".
- **Pisten gelb / rot:** Kopf einer Runway-Bay antippen schaltet die Piste gelb (CAUTION) → rot (CLOSED) → frei. Der Kopf wird eingefärbt, und ein farbiger Marker-Streifen "… CAUTION/CLOSED" steht in der Runway-Bay, der sich dort normal verschieben lässt. (Nur bei dir – EuroScope bietet dafür keinen Sync-Weg ohne Flieger.)
- **Hannover, Tower:** eigene **TAXI**-Bay (rollende Abflüge, Ankünfte nach der Landung), HOLDING nur noch übergebene Abflüge; Helipad-Bay schmaler, dazu **AREA** (Hubschrauber ohne Helipad); die Bay der Centerbahn ist schmaler.
- **Abgeben = grau, dann weg:** Gibst du einen Flieger ab (z. B. Tower an Ground), bleibt er bei dir noch 60 s grau sichtbar und verschwindet dann.

## Neu in 2.0.55

- **Hannover, Tower: drei Runway-Bays** – RWY 09L/27R, RWY 09C/27C, RWY 09R/27L – plus **HELIPAD H2/H3**. Jeder Streifen steht in der Bay seiner Piste; ohne bekannte Piste in der ersten. Hineinziehen setzt die Piste gleich mit (gleiche Richtung wie bisher, z. B. 27C), aufs Helipad = H2.
- **Hubschrauber** werden erkannt (gängige Typen wie EC35, H145, R44 … oder ein Helipad als Piste) und stehen in der Helipad-Bay. In der Pistenauswahl für EDDV gibt es zusätzlich **H2** und **H3**.

## Neu in 2.0.54

- **Stand bei Abflügen:** Ohne vergebenen Stand ermittelt StripBay ihn wie GRP ("Departure stand") aus der Position: der Stand aus GRpluginStands.txt, auf dem der Flieger steht (Kreis bzw. Polygon, langsamer als 5 kt). Gilt auch für die Delivery-Frequenz nach Stand.
- **Ausweichlandungen (Diversion):** Ein Flieger mit anderem Ziel im Flugplan, der nah (bis zur Ankunftsentfernung), tief (unter 6000 ft), langsam und sinkend auf einen unserer Plätze zufliegt – oder dort schon gelandet ist –, wird als Ankunft geführt. Marke **DIV**, in den Details das geplante Ziel, Meldung im ES-Chat. Überflüge in der Höhe zählen nicht.

## Neu in 2.0.53

- **Stands direkt aus dem GRP:** Ist das Ground Radar Plugin geladen, liest StripBay dessen `GRpluginStands.txt` (neben GRplugin.dll) – Vorrang vor einer anderswo gefundenen Datei. Ändert sich die Datei, wird sie automatisch neu gelesen (alle 10 s geprüft). Im ES-Chat steht beim ersten Laden die Anzahl.
- **On Freq → Pending:** Zurückschieben klappt wieder, auch wenn On Freq über StripBay verteilt wurde (EuroScope ohne ONFREQ). Gleiches für Line-up.

## Neu in 2.0.52

- **Intersection schnell "able for":** Im Intersection-Tastenfeld die Taste **Able for** antippen (orange, Titel "ABLE FOR"), dann die Intersection wählen – sie wird als "able for" gesetzt statt zugewiesen. Beim nächsten Öffnen ist wieder "zugewiesen" eingestellt.

## Neu in 2.0.51

- **Line-up ohne orange Ecke:** Nimmt EuroScope den Status LINEUP nicht an, verteilt StripBay "Line-up" selbst (`.SB_LU_TRUE`, wie bei On Freq). Der Streifen steht bei allen mit StripBay auf der Runway, ohne orange Ecke. Takeoff (DEPA) oder ein anderer Status nimmt es zurück.

## Neu in 2.0.50

- **Berlin Tower-Frequenzen fest nach Bahn:** Nordbahn **120.030**, Südbahn **118.805** – auch wenn nur ein Tower online ist (er hört beide ab). Bist du selbst Tower, geht es direkt zum Radar.

## Neu in 2.0.49

- **Parkposition auf Abflug-Streifen** vor dem Pushback (Pending, On Freq, Startup): Feld STAND rechts, antippen = Standauswahl.
- **Frequenzen Apron:** Apron → Ground der Bahn (Berlin nach Frequenz: Nord 129.505, Süd 121.705) → Tower → Radar.
- **Nur ein Tower online:** Er macht beide Bahnen; Abflüge beider Seiten bekommen seine Frequenz.

## Neu in 2.0.48

- **Frequenzen nach Bahn:** Die nächste Station richtet sich nach der Seite der Piste (Nord/Süd). Ist Ground Süd offline, geht es zum Tower Süd und dann zum Radar Süd, nicht zu Ground Nord oder Tower Nord.
- **Ins Leere tippen** hebt die Auswahl jetzt auch auf, wenn gerade die Details offen sind (der Tipp schließt sie und hebt die Markierung auf).
- **Apron mit Delivery (Top-down):** PENDING und ON FREQUENCY als eigene Bays.

## Neu in 2.0.47

- **Keine Übergabe-Spalten mehr** (TO GROUND, TO TOWER, HANDOVER, Tower-"TO GROUND"): Die Zuständigkeit regelt das. Übergibst du (Details "To Ground"/"To Tower", Weiter-Taste ▶, Taxi In an Apron), wird die übernehmende Stelle zuständig und der Streifen verschwindet bei dir. Streifen anderer Stellen siehst du nur, wenn sie gleich zu dir kommen (grau, Vorankündigung), du sie angefordert hast oder in "All Bays".

## Neu in 2.0.46

- **Anforderungen synchron:** Stellen mit und ohne Seite (APN / APNN, GND / GNDS) gelten jetzt als dieselbe, Anforderung und "zuständig" passen damit auch bei geteiltem Ground/Apron. Kann EuroScope das Scratchpad nicht setzen (Flieger von jemand anderem getrackt), steht das einmal im ES-Chat.
- **Vorankündigung:** Was gleich zu dir kommt, siehst du vorher **grau**: Ground sieht Abflüge, die beim Apron schon rollen; Tower sieht rollende Abflüge (Taxi, an Ground); Ground sieht gelandete Ankünfte, die der Tower noch nicht abgegeben hat; Apron sieht Ankünfte bei Ground. Nach der Übergabe erscheinen sie in Farbe.
- **Apron ohne Ground:** keine TO-GROUND-Spalte, stattdessen **TO TOWER** (auch Details-Button und Weiter-Taste).
- **Nächste Frequenz (Abflüge):** Delivery → Apron bzw. Ground (nach Stand) → Tower → erst dann Radar. Apron → Ground → Tower → Radar. Ground → Tower → Radar.
- **vACDM-Fehler:** rote Marke "vACDM: ..." oben neben der Uhr, TSAT-Felder orange ("ERR" ohne TSAT).
- **Ins Leere tippen** hebt die Markierung des angewählten Streifens auf.

## Neu in 2.0.45

- **Ground/Apron getrennt nach Zuständigkeit:** Ist Apron zuständig, sieht Ground den Streifen nicht (und umgekehrt). Ausnahmen: deine eigenen Übergabe-Bays (TO GROUND bei Apron, Taxi In an Apron bei Ground) und Streifen, die du angefordert hast oder per Top-down mitmachst.
- **Streifen anfordern:** In den Details "Request" (bei Streifen einer anderen Stelle) oder im "+ Strip"-Editor Callsign eingeben und **Request** (auch für ausgeblendete Streifen), Befehl `.strips request CALLSIGN` (nochmal = zurücknehmen). Bei der zuständigen Stelle **blinkt** der Streifen (oranger Rahmen, Marke "REQ <Stelle>"). Tipp auf die Marke bzw. "Give" in den Details (`.strips give CALLSIGN`) übergibt die Zuständigkeit an den Anfordernden. Synchron über `.SB_RQ_`.
- **Angewählter Streifen** nur noch umrahmt, ohne andere Papierfarbe.
- Blinken (Anforderung, DCL) läuft jetzt gleichmäßig (alle 0,5 s neu gezeichnet).

## Neu in 2.0.44

- **Weiter-Taste ▶** rechts oben auf jedem Streifen: schiebt ihn in die nächste Bay. Abflüge: Pending → On Freq → Startup → Pushback → Taxi → (Apron: an Ground, Ground: an Tower, Tower: Runway) → Takeoff. Ankünfte: Final → Runway → Landefreigabe → Taxi In → (an Apron) → geparkt (nur am Gate). Ein-/ausschalten: Einstellungen → "Next bay button" bzw. `.strips nextbtn on|off`.
- **Platz wählen = dessen Flieger:** Wählst du in den Einstellungen einen Platz (z. B. EDDV), zeigt StripBay nur noch die Flieger dieses Platzes; "Auto" nimmt wieder die aktiven Plätze.
- **Einstellungen:** "Stands without apron", "Hold short list" und die beiden Intersection-Zeilen sind raus (die Befehle `.strips gndstands`, `holdshort`, `intersections` gibt es weiter). "Drag onto runway sets" heißt jetzt **Line-up** / **Takeoff**.

## Neu in 2.0.43

- **Zuständig = sichtbar:** Ist deine Station als zuständig eingetragen, siehst du den Streifen immer, auch außerhalb deines Bereichs (andere Piste, andere Bay, GAT/W-Stand). Mit Top-down gilt das auch für die unbesetzten unteren Stationen, die du mitmachst (Tower allein: GND, APN und DEL).
- **Top-down** (Einstellungen → Top-down, `.strips topdown on|off`, Standard an): Unbesetzte untere Stationen macht die nächsthöhere mit und sieht deren Bays. Delivery: DEL → Apron → Ground → Tower. Startup/Pushback: Apron → Ground → Tower. Taxi In: Ground → Tower. Ein Tower allein hat also DELIVERY, STARTUP und TAXI IN mit in seiner Ansicht.
- **Taxi In bis zum Stand:** Ankünfte am Boden haben ein Kästchen **STD**. Antippen = bis zur Parkposition freigegeben (grüner Haken), synchron mit allen (`.SB_SC_`). Befehl: `.strips stdclr CALLSIGN`.
- **Heading per Tastenfeld:** Statt der vollen Tastatur ein Ziffernblock (drei Ziffern 001–360, ungültige Ziffern gehen nicht), C | None | OK.
- **Keine STBY-Warnung** mehr bei Abflügen.
- **Farben:** Light-Mode-Hintergründe blasser; die Bay-Namen haben in beiden Themes keine Farbbalken mehr.

## Neu in 2.0.42

- **Ground: eine Spalte für alle Übergaben.** Statt DEPARTING TOWER je Piste und TO APRON gibt es bei Ground nur noch **HANDOVER**: Abflüge an Tower (beide Pisten) und Ankünfte an Apron zusammen. Hineinziehen: Abflug = an Tower, Ankunft = an Apron (TXIN). Ground ohne Apron: HANDOVER nur mit den Abflügen.

## Neu in 2.0.41

- **Auto, Apron 1 und GAT antippbar:** Die drei Buttons der Standauswahl (und die ▲/▼-Pfeile der Höhenliste) wurden bei Touch/Maus nicht erkannt. Behoben.
- **Stand wie GRP vergeben:** Aus dem Ground Radar Plugin (1.5) ausgelesen, wie es selbst einen Stand vergibt, und genau so umgesetzt: Stand in die Flight-Strip-Annotation 6 als `s/<stand>/s` (die liest das eigene GRP jeden Takt) und `GRP/S/<stand>` ins Scratchpad (so erfahren es die GRPs der anderen Lotsen). Leeren entfernt den Eintrag. Im ES-Chat steht "stand ... assigned".
- **Auto:** StripBay wählt nach den GRP-Regeln aus GRpluginStands.txt und vergibt wie oben. Nur ohne Stand-Daten wird das Auto des GRP versucht.

## Neu in 2.0.40

- **Stand über das Ground Radar Plugin selbst:** Ist GRP geladen, ruft StripBay dieselben Funktionen auf wie das Stand-Menü von GRP (für den angewählten Flieger): Stand aus der Liste (GRP-Funktion 7 mit dem Stand), **[---]** zum Leeren (4) und **Auto** (2). GRP speichert und verteilt den Stand dann wie gewohnt. Ohne GRP verteilt StripBay wie bisher `GRP/S/<stand>` bzw. wählt selbst aus den GRP-Daten. Voraussetzung: StripBay darf auf dem Radarschirm zeichnen (wie für Clearance-Flag und Squawk).
- **Handschrift:** OK (oder Enter) erkennt noch nicht erkannte Schrift zuerst und übernimmt sie dann.
- **Squawk Auto:** Die TopSky-Funktionsnummer ist einstellbar: `.strips sqkfn N` (Standard 667 wie vSID).

## Neu in 2.0.39

- **Routen-Editor neu:** Die Route steht mehrzeilig als einzelne Punkte (Airways hellblau, DCT grau). Einen Punkt antippen = ihn bearbeiten (blau, mit Cursor). Werkzeuge: ◀ ▶ (Punkt wechseln, ▶ am Ende = neuer Punkt), **Insert** (neuer Punkt davor), **Delete** (Punkt weg), **DCT**, **Cut to end** (ab hier alles weg, z. B. für eine neue Fortsetzung), **Original** (Ausgangsroute), **Paste** (als neue Punkte dahinter). Leertaste = nächster Punkt, ← auf einem leeren Punkt springt zum vorherigen. OK übernimmt die Route.
- **Details:** Die Route hat dort jetzt Platz für bis zu vier Zeilen.
- **Paste und abc/ABC** im Einstellungs-Editor (z. B. Hoppie-Code) lassen sich jetzt auch per Touch/Maus antippen (vorher nur Strg+V).
- **Recover-Button** ohne orange Hervorhebung, nur noch mit der Anzahl.

## Neu in 2.0.38

- **Neuer Flugplan am Stand:** Steht ein Flieger auf PARK und gibt einen neuen Flugplan von hier auf, setzt StripBay den Ground State auf **kein Status (NSTS)**. Der alte Ankunftsstreifen und alle Merker verschwinden, der Abflug erscheint in Pending. Im ES-Chat steht ein Hinweis.
- **Stand Auto zuverlässig:** StripBay wählt den Stand jetzt selbst aus den GRP-Daten (GRpluginStands.txt) und verteilt ihn wie GRP (`GRP/S/<stand>`). Dabei zählen: frei, Nachbarn nicht gesperrt, WTC, Priorität und neu die Airline-Stands (`CALLSIGN:`). Diese Stands gibt es nur für die genannten Airlines, für die sind sie bevorzugt. Das Ground Radar Plugin wird nur noch gefragt, wenn StripBay keinen passenden freien Stand findet.
- **GAT- und Apron-1-Button:** Die Buttons sind jetzt immer da, auch wenn dahinter mehrere Stands liegen (G1, G2 ...). Ein Tipp vergibt den ersten freien Stand der Gruppe, eine Fläche mit genau dem Namen (z. B. GAT) oder den Namen selbst. Für die Einzelwahl gibt es zusätzlich die Kategorie.

## Neu in 2.0.37

- **Aktueller Streifen:** Der in EuroScope angewählte Streifen hat jetzt einen leichten schwarzen Rahmen statt des orangen.

## Neu in 2.0.36

- **Wetter kürzer:** In der Wetterzeile steht kein "WIND" mehr vor dem Wind, das QNH nur noch als **Q1021** (bzw. A2992).
- **Uhr:** `14:45:12Z` statt `14:45:12 UTC`.
- **Recover zuverlässig:** Ein zurückgeholter Streifen bleibt jetzt sicher stehen (wie ein von Hand erstellter), bis du ihn wieder entfernst oder der Flieger weiter als 20 NM weg ist. Im ES-Chat steht "CALLSIGN recovered." bzw. warum es nicht ging.
- **Intersection ab Pushback:** INT statt CLR, sobald der Ground State Pushback (oder später) ist, auch wenn der Streifen noch in einer Startup-Spalte liegt.
- **Streifenrahmen:** Die Farbe der Flugregeln (IFR/VFR/Y/Z) ist kein Balken mehr an der Seite, sondern ein Rahmen oben und unten.

## Neu in 2.0.35

- **Clearance per Sprechfunk setzt ON FREQ:** Gibst du die Freigabe (CLR-Kästchen, Details), wechselt ein Abflug aus Pending automatisch auf den EuroScope-Status **ONFREQ**. Kommt die Freigabe per CPDLC (DCL, WILCO), bleibt der Status unverändert. Zurücknehmen der Freigabe ändert den Status nicht.
- On Freq ist weiterhin der normale EuroScope-Status ONFREQ. Der StripBay-Ersatz aus 2.0.34 springt nur ein, falls EuroScope den Status einmal ablehnt.

## Neu in 2.0.34

- **Squawk automatisch:** SQK-Feld auf dem Streifen antippen (oder SQUAWK in den Details) öffnet das Tastenfeld, dort **Auto**. StripBay ruft die Squawk-Funktion von **TopSky** auf (wie vSID, Funktion 667); ist nur CCAMS geladen, dessen Funktion (871). Kommt der Code verzögert (Server), wartet StripBay bis 5 Sekunden; im ES-Chat steht das Ergebnis. Voraussetzung wie beim Clearance-Flag: StripBay darf auf dem Standard-Radarschirm zeichnen.
- **Pushback zeigt die Intersection:** Das CLR-Kästchen steht nur noch bei Pending, On Frequency und Startup; ab Pushback wieder INT.
- **On Frequency zuverlässig:** Nimmt EuroScope den Status ONFREQ nicht an (manche ES-Versionen kennen ihn nicht), verteilt StripBay "On Freq" selbst (`.SB_OF_TRUE`, wie vSID). Bei allen mit StripBay steht der Streifen dann in ON FREQUENCY, ohne orange Ecke. Startup usw. ersetzt es, zurück nach Pending nimmt es zurück.
- **Squawk-Warnung bei Abflügen** (SQK rot, Transponder sendet anderen Code) erst, wenn der Flieger in der Luft ist.
- **CPDLC überarbeitet:** Abfragen an Hoppie jetzt per GET (wie vSMR/TopSky) mit langem Timeout (Hoppie antwortet unter Last langsam). Verbindung und Fehler von Hoppie (z. B. "illegal logon code") stehen jetzt im ES-Chat.
- **Berlin, Main Apron:** Das südliche Apron heißt **Main Apron** (Bereich in der Apron-Ansicht, Frequenzname). Es wird auch über das Rufzeichen **EDDB_A_GND** erkannt, egal auf welcher Frequenz (Ankünfte Süd und Delivery für B/C/D: 121.855 > EDDB_A_GND > Ground Süd).
- **GAT und Apron 1 ohne Apron:** Stands `G*` (GAT), `APRON1*`/`APN1*`/`AP1` gehören zu Ground Nord wie die W-Stands zu Ground Süd: Startup bei Ground, Delivery und Ankünfte bekommen Ground Nord 129.505. Gespeicherte alte Standards werden umgestellt.
- **Farben konsequent nach Richtung:** Abflug-Bays in Blautönen, Ankunfts-Bays (Final, Taxi In) in Ocker/Braun wie das Ankunftspapier, Runway neutral. Marken ARR/TXI in Ankunftsfarben, Recover zeigt entfernte Ankünfte auf Ankunftspapier.

## Neu in 2.0.33

- **Parken nur am Gate:** Ein Ankunftsstreifen verschwindet nur noch, wenn der Flieger am Gate angekommen ist (am Boden, < 3 kt, im Standkreis laut GRP-Daten) **und du selbst** auf die **P**-Marke tippst (oder in den Details "Parked" wählst). Vorher gibt es weder die Marke noch den Eintrag. PARK von anderen (Lotse, GRP) entfernt den Streifen bei dir nicht mehr.
- **STAND antippen** öffnet bei Ankünften immer die Standauswahl (früher parkte ein Tipp auf das ganze Feld).
- **Stand Auto repariert:** StripBay fragt das Ground Radar Plugin und **wartet bis zu 3 Sekunden**, weil GRP seine Zuweisung oft erst im nächsten Takt verteilt (vorher wurde sofort abgebrochen). Kommt nichts, wählt StripBay selbst einen freien Stand aus `GRpluginStands.txt` und verteilt ihn wie GRP. Im ES-Chat steht jedes Mal, was passiert ist (von GRP, von StripBay oder warum kein Stand). `.strips grpauto off` fragt GRP gar nicht erst (sofort StripBay), `.strips grpauto 2` wieder mit GRP.

## Neu in 2.0.32

- Die Marke **LU** (Line-up) auf den Streifen ist entfernt. Line-up erkennt man an der Runway-Bay bzw. am Ground State; TO für die Startfreigabe bleibt.

## Neu in 2.0.31

- Leipzig: nur **ein Ground** (121.805), Tower Nord 125.955 / Süd 121.105, Abflüge nach SID (PENEM/ORTAG → 126.065, sonst 126.175).
- Hannover: Abflugkette 119.490 > 124.240 > 125.025 > 127.625 > 133.725 > 122.800, Ground 121.955, Tower 120.180; **VFR bleiben sichtbar** (automatisch weg nur IFR über 1500 ft).
- Ground/Apron zeigen bei Abflügen jetzt den **Tower der Bahn** als nächste Frequenz, wo das Profil Tower-Frequenzen kennt (Leipzig, Hannover). Berlin unverändert.

## Neu in 2.0.30: Berlin, Leipzig, Hannover (plug and play)

StripBay erkennt den Platz selbst und stellt sich darauf ein. Gewählt wird nach deinem **Callsign** (EDDV_TWR → Hannover), sonst nach dem **aktiven Platz** im Runway-Dialog. Festlegen lässt es sich im Einstellungsfenster (Zeile **Airport**) oder mit `.strips profile EDDB|EDDP|EDDV|auto`.

| | Berlin EDDB | Leipzig EDDP | Hannover EDDV |
|---|---|---|---|
| Tower | 2 (Nord 06L/24R, Süd 06R/24L) | 2: Nord 08L/26R **125.955**, Süd 08R/26L **121.105** | 1 (alle Bahnen) **120.180** |
| Apron | ja (Nord/Süd) | nein | nein |
| Ground | Nord/Süd | **einer**, 121.805 | einer, 121.955 |
| Abflug weg | nach "Weggeschickt" (vSID HOV) | nach "Weggeschickt" | **IFR automatisch über 1500 ft**, VFR bleibt |
| Abflug (Tower → Radar) | Bremen Radar 120.340 / 120.630 | nach SID: **PENEM, ORTAG → 126.065**, alle anderen → **126.175** | **119.490 > 124.240 > 125.025 > 127.625 > 133.725 > 122.800** |
| Ground → | Radar (wie bisher) | Tower der Bahn (125.955 / 121.105) | Tower 120.180, sonst Radar-Kette |
| Delivery → | Apron/Ground nach Stand | Ground 121.805, sonst Tower der Bahn | Ground 121.955, sonst Tower 120.180 |
| Nach der Landung | Apron, sonst Ground der Bahn | Ground 121.805 | Ground 121.955 |

Alle Frequenzen gelten nur, wenn dort jemand online ist (außer 122.800 als letzter Eintrag in Hannover). Ist niemand da, steht **-**; ist bei Leipzig das SID-Radar offline, entscheidet EuroScope (nächster Sektor). Welche Rolle du hast, ergibt sich aus der Ansicht (Delivery, Ground, Tower), bei "All Bays" aus deinem Callsign.

- **Ohne Apron** (Leipzig, Hannover) gibt es keine Apron-Ansicht; Ground zeigt Startup, Pushback, Taxi Out, Departing Tower und Taxi In bis zum Stand. Die Zuständigkeit kennt dort kein APN.
- **Ein Tower** (Hannover): keine Nord/Süd-Aufteilung, kein Bereich-Dropdown, Tower-Ansicht mit einer Reihe.
- **Hannover:** Ein **IFR**-Abflug verschwindet, sobald er in der Luft und höher als 1500 ft ist. Kein Tippen auf FREQ nötig, er kommt auch nicht in Recover. **VFR bleibt sichtbar** (Platzrunden, Rundflüge), bis du ihn wegschickst oder er 20 NM weg ist.
- **Leipzig:** ein Ground für beide Bahnen – in der Ground-, Delivery- und Apron-Sicht gibt es keinen Nord/Süd-Bereich, die Zuständigkeit heißt einfach GND. Der Bereich Nord/Combined/Süd gilt nur beim Tower.
- Ohne Nachbarstation steht im FREQ-Feld wie überall **-**.
- **Einstellungen je Platz:** Frequenzketten, Hold-Short-Liste, Intersections, Stands ohne Apron und die Pisten-Aufteilung werden je Platz gespeichert (Berlin unter den bisherigen Schlüsseln, sonst mit `_EDDP` bzw. `_EDDV`). Eine Hold-Short-Liste für Hannover ändert also Berlin nicht. Hold-Short- und Intersection-Listen sind für Leipzig und Hannover ab Werk leer (Pisten und freie Eingabe gehen immer); füllen mit `.strips hs …` bzw. `.strips intersections north|south …`.
- **Andere Plätze** laufen ohne Profil: keine Aufteilung, Frequenzen wie EuroScope bzw. Rufzeichen-Muster, Apron-Ansicht vorhanden.

## Neu in 2.0.29

- **Transponder-Warnung:** Steht ein Abflug auf Line-up oder hat Takeoff-Freigabe und sendet kein Mode C, wird das SQK-Feld rot mit **STBY**.
- **Betriebsrichtungswechsel:** Wird im Runway-Dialog die Abflugpiste gewechselt (z. B. 24 → 06), erscheint oben ein oranger Button **Runway change: N → 06L 06R**. Ein Tipp stellt alle Abflüge, die noch nicht rollen (Pending, On Freq, Startup, Pushback), auf die aktive Piste derselben Bahn um (Nordbahn bleibt Nordbahn). Die SID bleibt, wenn es sie auch für die neue Piste gibt, sonst wählt EuroScope. Rollende Flieger bleiben unverändert. Auch als `.strips rwyswitch`.
- **Stand-Warnung bei Ankünften** (nach den GRP-Daten): Ist der zugewiesene Stand **besetzt** (ein Flieger steht drauf) oder durch einen Nachbarn **gesperrt** (`BLOCKS`), wird das STAND-Feld rot; in den Details steht, wer dort steht bzw. welcher Stand sperrt. Ist eine Ankunft **gelandet und hat noch keinen Stand**, steht dort ein orangefarbenes **?**. Ein Tipp auf das STAND-Feld öffnet die Standauswahl (mit Auto); parken geht nur über die grüne **P**-Marke am Gate.
- **TSAT abgelaufen:** Ist TSAT + 5 Minuten vorbei und der Flieger noch nicht im Startup (Pending / On Freq), wird das TSAT-Feld rot (nicht in der Tower-Ansicht, dort steht die CTOT).
- **DCL mit einem Tipp:** Fragt ein Pilot per CPDLC nach der Clearance, blinkt der Rand des CLR-Kästchens orange. Ein Tipp öffnet dann direkt das CPDLC-Fenster mit der fertigen DCL und **Send DCL** (statt einfach das Flag zu setzen). Mit WILCO wird das Kästchen ausgefüllt.
- **Sync für später Eingeloggte:** Kommt an deinem Platz ein Lotse online, schickt StripBay nach 5 Sekunden einmal die Merker der Flieger, für die du zuständig bist (Zuständigkeit, Hold Short, Übergaben, G/A) – wie "Publish" im GRP. So sieht der Neue sofort den aktuellen Stand. Von Hand: `.strips resync`.

## Neu in 2.0.28

- **Clearance-Kästchen in der Spalte CLR:** Bei Abflügen, die noch nicht rollen (Pending, On Frequency, Startup, Pushback), heißt die zweite Spalte **CLR** statt INT und enthält das Kästchen. Ab Taxi steht dort wieder **INT** (Intersection). Oben rechts im Streifen ist kein Kästchen mehr.

## Neu in 2.0.27

- **Clearance als Kästchen:** Abflugstreifen haben ein Kästchen wie auf dem Papierstreifen (ab 2.0.28 in der Spalte CLR). **Leer** = noch keine Freigabe, **ausgefüllt** = freigegeben, **blauer Rand** = DCL per CPDLC gesendet (wartet auf WILCO). Antippen gibt bzw. nimmt die Freigabe (Clearance-Flag, für alle sichtbar); in der Tower-Ansicht ist es nur Anzeige. Das bisherige CLR-Feld mit Text und die Marken CLR/SENT entfallen; in den Details steht weiter PENDING / SENT / CLEARED.
- **Prüfung wie vSID:** Bei IFR-Abflügen am Boden wird das **SID-Feld rot**, wenn etwas nicht stimmt, mit derselben Anzeige wie im SID-Item von vSID:
  - **MANUAL**: keine SID für die Route (EuroScope konnte keine zuweisen)
  - **EQUIP**: keine RNAV-Ausrüstung (keiner von A, B, G, R, I und kein PBN/)
  - SID rot (Name bleibt stehen): die SID gehört laut Sektordatei nicht zur Piste
  - Piste nicht aktiv für Abflüge: wie bisher RWY-Feld rot

  In den Details steht unter **CHECK (vSID)** der Grund in Worten. Hinweis: Was vSID selbst anzeigt, können andere Plugins nicht auslesen (EuroScope gibt Tag-Items anderer Plugins nicht heraus). StripBay prüft deshalb nach denselben Kriterien aus den Flugplandaten; flugplatzspezifische Regeln aus der vSID-Konfiguration (Uhrzeit, Typ, Bereich …) sind nicht enthalten.

## Neu in 2.0.26

- **Standauswahl gruppiert:**
  - Oben **Auto (GRP)**: lässt das Ground Radar Plugin einen Stand zuweisen (dieselbe Funktion wie "Auto" im Stand-Menü von GRP). Voraussetzung wie beim Clearance-Flag: StripBay darf auf dem Standard-ES-Radarschirm zeichnen (Plug-ins-Dialog). Antwortet GRP nicht (anderer Schirm, andere GRP-Version), wählt StripBay selbst einen freien Stand aus `GRpluginStands.txt` (nicht besetzt, vergeben oder gesperrt, nicht `MANUAL`, passende Wirbelschleppenkategorie, höchste `PRIORITY`) und verteilt ihn wie GRP mit `GRP/S/<Stand>`. Erneutes Auto gibt einen anderen Stand. Steht in deiner GRP-Version "Auto" unter einer anderen Funktionsnummer: `.strips grpauto <Nummer>` (Standard 2, GRP 1.5).
  - Daneben die Direkt-Buttons **Apron 1** und **GAT** (Stands, deren Name mit `APRON1`/`APN1` bzw. `GAT` beginnt; gibt es mehrere, werden sie zur Kategorie).
  - Kategorien: **Apron 2** (Stands, die nur aus einer Zahl bestehen), **A**, **B**, **C**, **D**, **E** (es werden nur die Nummern gezeigt), **W** und **Other** für den Rest.
  - **Lage wie im GRP:** Stands, die gerade **besetzt** (ein Flieger steht im Standkreis), an einen anderen Flieger **vergeben** oder durch `BLOCKS` eines Nachbarn **gesperrt** sind, erscheinen blass mit der Kennung OCC / ASG / BLK. Wählbar bleiben sie trotzdem (wie die manuelle Auswahl im GRP). Händisch im GRP gesperrte Stands (Stands List) kennt StripBay nicht, die gibt GRP nicht weiter.
- **Initial Altitude** nur in vollen 1000 ft: 1000 bis 5000, darüber FL060, FL070 …
- **Hold Short V:** Reihenfolge **1 · 2 · 3 · 4 · C** (im Tastenfeld immer erst die Nummern aufsteigend, dann Buchstaben). Die Standardliste ist jetzt `V1 V2 V3 V4 VC …`, eine gespeicherte alte Standardliste wird umgestellt.

## Neu in 2.0.25

- **Inbounds zeigen keine Apron-Frequenz mehr, wenn Apron offline ist.** Bisher stand bei Süd-Inbounds immer 121.855 (bei Nord 129.605), auch ohne Apron. Jetzt: Apron der Seite, sonst Ground der Seite, sonst **-**. Dasselbe gilt für die Delivery-Regeln nach Stand. Gespeicherte alte Einstellungen werden automatisch umgestellt.

## Neu in 2.0.24

- **Zuständigkeit für alle sichtbar (wie bei vSID):** Jeder Streifen trägt eine Marke, wer den Flieger gerade hat: **DEL**, **APN** (Apron), **GND** (Ground), **TWR** (Tower), bei getrennten Pisten mit Seite (**APN N**, **GND S**, **TWR N** …). Deine eigene Stelle ist kräftig gefärbt, fremde Stellen blass. Verteilt wird sie wie bei vSID als kurzes Scratchpad `.SB_RS_<STELLE>` an alle mit StripBay.
  - **Automatisch:** Wer einen Flieger bearbeitet (Status setzen, verschieben, Freigabe, Landefreigabe), ist zuständig. Deine Stelle ergibt sich aus Station und Bereich oben (z. B. Ground + North = GND N; in "All Bays" wird nichts beansprucht).
  - **Übergaben** setzen gleich die übernehmende Stelle: An Tower → TWR (Seite nach der Piste), An Ground → GND, Ankunft an Ground → GND, an Apron (Taxi in) → APN, auf die Piste / zurück ins Final → TWR.
  - **Von Hand:** Marke auf dem Streifen oder das Feld **RESPONSIBLE** in den Details antippen, Stelle wählen (**NONE** = niemand).
  - Kurz hintereinander geänderte Zustände gehen nicht alle einzeln raus: nach der ersten Nachricht folgt nur noch der letzte Stand.
- **Höhen als eine Spalte zum Scrollen:** CFL / Initial Altitude und Final Altitude öffnen eine schmale Liste mit 9 Werten untereinander, der aktuelle Wert steht in der Mitte (sonst 5000 ft bzw. FL100). Blättern mit **Wischen**, **Mausrad** oder den Pfeilen **▲ ▼**; Antippen ohne Wischen wählt.
- **Hoppie-Logon-Code:** Groß- und Kleinbuchstaben möglich (Taste **abc/ABC** auf der Bildschirmtastatur, echte Tastatur sowieso), dazu **Paste** (auch Strg+V) zum Einfügen aus der Zwischenablage. Auch per Stift bleibt die Schreibweise erhalten.
- **Recover statt Trash:** Der Button heißt **Recover**. Flieger, die **weiter als 20 NM** von ihrem Platz entfernt sind, sind automatisch **komplett weg**: Streifen, Recover-Liste und alle Merker (Hold Short, Zuständigkeit …), auch bei von Hand erstellten Streifen. `.strips range` und `.strips arrivals` gehen deshalb nur noch bis 20 NM.
- **Keine Nachbarstation online:** Im FREQ-Feld steht dann **-** (z. B. kein Apron, kein Ground Süd).
- Oben links steht kein "STRIPBAY" mehr.

## Neu in 2.0.23

- **Pisten nur aus der festen Liste:** Im Änderungsmenü stehen nur die echten Pisten des Platzes zur Wahl, in Berlin **06L 06R 24L 24R** (die aktive Betriebsrichtung zuerst). Alte Bezeichner aus älteren Sektorpaketen (07/25) und freie Eingabe gibt es dort nicht mehr; andere Werte werden abgelehnt.
- **SIDs folgen der Piste:** Die SID-Liste zeigt nur die SIDs der Piste des Streifens aus der Sektordatei (dieselbe Quelle, aus der EuroScope und TopSky ihre Departure List füllen). Wechselt die Piste, bleibt die SID nur, wenn es sie für die neue Piste gibt, sonst wählt EuroScope die passende. Keine freie Eingabe; **Auto** lässt EuroScope wählen.
- **Clearance-Status:** **PENDING** (noch keine Freigabe), **SENT** (DCL per CPDLC gesendet, wartet auf WILCO), **CLEARED** (Clearance-Flag gesetzt). Steht so im Änderungsmenü und auf dem Streifen.
- **Delivery gibt Freigaben direkt am Streifen:** Bei Abflügen in Pending/On Frequency (und in der Delivery-Ansicht) steht statt INT das Feld **CLR**. Ein Tipp = Freigabe erteilt (CLEARED, grün), noch ein Tipp nimmt sie zurück. Kein Menü nötig.
- **CPDLC auf dem Streifen nur als Farbpunkt:** blau = angemeldet / DCL gesendet, orange blinkend = Anfrage (Logon oder DCL), magenta blinkend = neue Nachricht, rot = UNABLE. Antippen öffnet das CPDLC-Fenster.
- **Ground States hinter einem Button:** Im Änderungsmenü gibt es statt der vielen Status-Tasten einen Button **Ground state: …**. Er öffnet die kurze Liste (Pending, On freq, Startup, Pushback, Taxi out, beim Tower auch Line-up und Takeoff; bei Ankünften Taxi in und Parked).
- **Hold Short synchron:** Die Verteilung über das Scratchpad läuft jetzt wie bei vSID über eine Warteschlange: jede Nachricht (`.SB_HS_…`, Übergaben, Stand, G/A) bleibt mindestens einen EuroScope-Takt stehen, damit sie sicher ans Netz geht, mehrere laufen nacheinander, erst danach kommt der alte Scratchpad-Inhalt zurück. Das eigene Echo wird ignoriert. Was du in der Zwischenzeit ins Scratchpad schreibst, steht danach drin.

