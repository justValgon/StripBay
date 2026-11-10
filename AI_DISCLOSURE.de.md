# Hinweis zum Einsatz von KI

🇬🇧 [English](AI_DISCLOSURE.md) · 🇩🇪 Deutsch

## Erklärung

StripBay wurde unter **starkem Einsatz von künstlicher Intelligenz** entwickelt. Der überwiegende Teil des Quellcodes, der Build-Skripte, der Tests und der Dokumentation wurde vom KI-Assistenten **Claude** (Anthropic) im fortlaufenden Dialog mit den Autoren Luis Korb und Sören Pflug erstellt.

## Aufgabenteilung

- **Autoren:** Anforderungen, Entwurfsentscheidungen, Funktionswünsche, Prioritäten, Prüfung der Ergebnisse, Veröffentlichung und Verantwortung für dieses Repository.
- **KI (Claude):** Entwurf und Änderung von C++-Code, Skripten und Workflows, Erstellung und Ausführung automatisierter Tests, Erstellung von Dokumentation und Vorschaubildern, Übersetzung von Texten.

Über die Veröffentlichung entscheiden die Autoren. Die KI handelt nicht unabhängig von ihnen.

## Überprüfung

- Bei jeder mit der KI vorbereiteten Änderung laufen automatisierte Tests: Unit-Tests, eine Simulation mit mehreren Lotsen und zufallsgesteuerte Invariantenprüfungen (mit AddressSanitizer und UBSan). Sie laufen in einer Linux-Umgebung mit einem Ersatz für die Windows- und EuroScope-Schnittstellen.
- Die Windows-DLL wird durch GitHub Actions erstellt (MSBuild, Release/x86).
- Die Tests **ersetzen nicht** den Einsatz in einer echten EuroScope-Sitzung. Das Verhalten bei realem Verkehr, mit anderen Plugins (TopSky, GRP, vACDM, vSID, Hoppie) und mit anderen EuroScope-Versionen kann abweichen.

## Einschränkungen

- Die Software wird **ohne Gewähr** bereitgestellt (siehe [LICENSE](LICENSE)). Fehler, auch falsche Streifen- oder Übergabestatus, sind möglich.
- KI-generierter Code kann Fehler enthalten, die nicht offensichtlich sind. Probleme bitte als [Issue](../../issues/new/choose) melden.
- StripBay ist für die **Flugsimulation** (z. B. in Online-Netzwerken) gedacht. Es ist **kein** zertifiziertes System und darf **nicht** für die reale Flugsicherung oder andere betriebliche Zwecke der Luftfahrt verwendet werden.
- Die beiden Dateien in `StripBay/sdk/` stammen aus dem EuroScope-SDK und wurden weder von den Autoren noch von der KI erstellt.

## Lizenz

Der Code steht unter der [MIT-Lizenz](LICENSE). Der Einsatz von KI ändert die Lizenzbedingungen nicht.
