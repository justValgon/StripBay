// Auffangen von Ausnahmen und ungueltigen CRT-Argumenten.
// Nichts davon darf bis in EuroScope durchschlagen ("ungueltiges Argument"); stattdessen wird
// Ort und Text gemerkt und von OnTimer im Chat und in der Diagnose gemeldet.
#pragma once
#include <exception>
#include <mutex>
#include <string>
#ifdef _WIN32
#include <stdlib.h>
#endif

inline std::mutex& SbErrMx() { static std::mutex m; return m; }
inline std::string& SbErrText() { static std::string s; return s; }
inline int& SbErrCount() { static int n = 0; return n; }

inline void SbNoteError(const char* where, const char* what)
{
    std::lock_guard<std::mutex> lk(SbErrMx());
    SbErrText() = std::string(where) + ": " + (what ? what : "?");
    ++SbErrCount();
}

// true, wenn seit dem letzten Abruf ein Fehler aufgetreten ist; n = Anzahl seit dem letzten Abruf
inline bool SbTakeError(std::string& text, int& n)
{
    std::lock_guard<std::mutex> lk(SbErrMx());
    if (SbErrCount() == 0) return false;
    text = SbErrText();
    n = SbErrCount();
    SbErrCount() = 0;
    return true;
}

#ifdef _WIN32
inline void __cdecl SbInvalidParam(const wchar_t* expr, const wchar_t* func, const wchar_t* file, unsigned int line, uintptr_t)
{
    std::string s = "invalid CRT argument";
    if (func)
        for (const wchar_t* p = func; *p && s.size() < 120; ++p) { if (p == func) s += " in "; s += (*p < 128 ? (char)*p : '?'); }
    (void)expr; (void)file; (void)line;
    SbNoteError("CRT", s.c_str());   // danach gibt die CRT-Funktion EINVAL zurueck, das Programm laeuft weiter
}
inline void SbInstallErrorHandlers() { _set_invalid_parameter_handler(SbInvalidParam); }
#else
inline void SbInstallErrorHandlers() {}
#endif

#define SB_TRY try {
#define SB_CATCH(where) } catch (const std::exception& e_) { SbNoteError(where, e_.what()); } catch (...) { SbNoteError(where, "unknown exception"); }
