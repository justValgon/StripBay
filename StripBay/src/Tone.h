// StripBay - eingebaute Toene (Plugin und StripBay Remote spielen dieselben)
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#ifdef _MSC_VER
#pragma comment(lib, "winmm.lib")
#endif

#include <cmath>
#include <map>
#include <string>
#include <vector>

// Toene (fest eingebaut, unabhaengig vom Windows-Soundschema): als WAV im Speicher erzeugt und
// asynchron abgespielt; ohne Erfolg der Systemton.
//   triple = drei kurze hohe Pieptoene, chime = zwei Toene ding-dong, double = zwei mittlere,
//   low = zwei tiefe, single = ein kurzer hoher
static const char* const kToneNames[] = { "triple", "chime", "double", "low", "single" };
inline void PlayTone(const std::string& name)
{
    struct Seg { double hz; int ms; int gap; };
    static std::map<std::string, std::vector<unsigned char>> cache;
    if (name.empty() || name == "off") return;
    std::vector<Seg> segs;
    if (name == "chime") segs = { { 1320, 160, 40 }, { 990, 260, 0 } };
    else if (name == "double") segs = { { 1000, 110, 80 }, { 1000, 110, 0 } };
    else if (name == "low") segs = { { 520, 180, 90 }, { 520, 180, 0 } };
    else if (name == "single") segs = { { 1500, 120, 0 } };
    else segs = { { 1750, 85, 70 }, { 1750, 85, 70 }, { 1750, 85, 0 } };   // triple
    std::vector<unsigned char>& wav = cache[name];
    if (wav.empty())
    {
        const int rate = 22050;
        std::vector<short> pcm;
        for (const Seg& g : segs)
        {
            int len = rate * g.ms / 1000, fade = rate * 6 / 1000;
            for (int i = 0; i < len; ++i)
            {
                double env = 1.0;
                if (i < fade) env = (double)i / fade;                       // weich ein- und ausblenden (kein Knacken)
                else if (i > len - fade) env = (double)(len - i) / fade;
                pcm.push_back((short)(std::sin(2.0 * 3.14159265358979 * g.hz * i / rate) * 9000.0 * env));
            }
            pcm.insert(pcm.end(), rate * g.gap / 1000, (short)0);
        }
        unsigned dataBytes = (unsigned)(pcm.size() * 2);
        auto put32 = [&](unsigned v) { for (int k = 0; k < 4; ++k) wav.push_back((unsigned char)(v >> (8 * k))); };
        auto put16 = [&](unsigned v) { wav.push_back((unsigned char)v); wav.push_back((unsigned char)(v >> 8)); };
        auto tag = [&](const char* t) { wav.insert(wav.end(), t, t + 4); };
        tag("RIFF"); put32(36 + dataBytes); tag("WAVE");
        tag("fmt "); put32(16); put16(1); put16(1); put32(rate); put32(rate * 2); put16(2); put16(16);
        tag("data"); put32(dataBytes);
        const unsigned char* raw = reinterpret_cast<const unsigned char*>(pcm.data());
        wav.insert(wav.end(), raw, raw + dataBytes);
    }
    if (!PlaySoundW(reinterpret_cast<const wchar_t*>(wav.data()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT))
        MessageBeep(MB_ICONASTERISK);
}

