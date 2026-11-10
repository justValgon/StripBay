// StripBay - Version (Plugin und StripBay Remote aus demselben Paket)
//
// Nummerierung MAJOR.MINOR.PATCH, jede Lieferung bekommt eine neue Nummer:
//   MAJOR  - Abgleich zwischen Lotsen (Scratchpad-Meldungen) oder Remote-Protokoll geaendert:
//            alle StripBay-Lotsen und das Remote-Geraet brauchen dieselbe Hauptversion
//   MINOR  - neue Funktionen, mit derselben Hauptversion vertraeglich
//   PATCH  - nur Fehlerbehebungen
#pragma once

#define STRIPBAY_VERSION   "4.1.1"
#define STRIPBAY_WIDEN2(x) L##x
#define STRIPBAY_WIDEN(x)  STRIPBAY_WIDEN2(x)
#define STRIPBAY_VERSION_W STRIPBAY_WIDEN(STRIPBAY_VERSION)
