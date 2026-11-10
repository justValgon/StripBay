// StripBay - Handschrift mit Stift oder Finger in Text umwandeln
//
// Nutzt die Handschrifterkennung von Windows (Tablet-PC-Erkenner, COM-Objekte
// InkDisp und InkRecognizerContext). Die Anbindung laeuft ueber IDispatch,
// so braucht der Build keine Tablet-PC-Header. Erkannt wird in der Sprache der
// installierten Windows-Handschrift (Einstellungen > Zeit und Sprache > Sprache:
// beim Sprachpaket "Handschrift" installieren).
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include <vector>

// strokes in Bildschirmpunkten, dpi des Fensters. true = erkannt (text kann leer sein)
bool RecognizeInk(const std::vector<std::vector<POINT>>& strokes, unsigned dpi, std::string& text, std::string& error);
