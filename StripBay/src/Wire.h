// StripBay - Remote: Datenmodell ueber das Netz (binaer, little endian, beide Seiten x86/x64 Windows)
// Ein Io() je Struktur fuer Schreiben UND Lesen, damit beide Richtungen nie auseinanderlaufen.
// WICHTIG: Jedes Feld aus Model.h muss hier stehen (der Test vergleicht beide Listen).
#pragma once

#include "Model.h"

#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace wire
{
    class Writer
    {
    public:
        std::string buf;
        void u8(uint8_t v) { buf.push_back((char)v); }
        void u32(uint32_t v) { for (int k = 0; k < 4; ++k) buf.push_back((char)((v >> (8 * k)) & 0xFF)); }
        void u64(uint64_t v) { for (int k = 0; k < 8; ++k) buf.push_back((char)((v >> (8 * k)) & 0xFF)); }
        bool more() const { return true; }   // optionale Felder am Ende: immer schreiben
        void operator()(bool& v) { u8(v ? 1 : 0); }
        void operator()(int& v) { u32((uint32_t)v); }
        void operator()(unsigned& v) { u32(v); }
        void operator()(long long& v) { u64((uint64_t)v); }
        void operator()(double& v) { uint64_t x; std::memcpy(&x, &v, 8); u64(x); }
        void operator()(std::string& v) { u32((uint32_t)v.size()); buf.append(v); }
        void operator()(GroundCmd& v) { int x = (int)v; (*this)(x); }
        void operator()(std::map<std::string, int>& m)
        {
            u32((uint32_t)m.size());
            for (auto& kv : m) { std::string k = kv.first; (*this)(k); (*this)(kv.second); }
        }
        void operator()(std::map<std::string, std::string>& m)
        {
            u32((uint32_t)m.size());
            for (auto& kv : m) { std::string k = kv.first; (*this)(k); (*this)(kv.second); }
        }
        template<class T> void operator()(std::vector<T>& v)
        {
            u32((uint32_t)v.size());
            for (T& e : v) (*this)(e);
        }
        template<class T, size_t N> void operator()(T (&arr)[N])
        {
            for (size_t i = 0; i < N; ++i) (*this)(arr[i]);
        }
        template<class T> void operator()(T& v) { Io(*this, v); }
    };

    class Reader
    {
    public:
        Reader(const std::string& b, size_t pos = 0) : m_b(b), m_p(pos) {}
        bool ok() const { return m_ok; }
        bool atEnd() const { return m_p >= m_b.size(); }
        bool more() const { return m_ok && m_p < m_b.size(); }   // optionale Felder: nur wenn noch Daten da sind
        size_t pos() const { return m_p; }
        uint8_t u8() { if (!need(1)) return 0; return (uint8_t)m_b[m_p++]; }
        uint32_t u32()
        {
            if (!need(4)) return 0;
            uint32_t v = 0;
            for (int k = 0; k < 4; ++k) v |= (uint32_t)(uint8_t)m_b[m_p + k] << (8 * k);
            m_p += 4;
            return v;
        }
        uint64_t u64()
        {
            if (!need(8)) return 0;
            uint64_t v = 0;
            for (int k = 0; k < 8; ++k) v |= (uint64_t)(uint8_t)m_b[m_p + k] << (8 * k);
            m_p += 8;
            return v;
        }
        void operator()(bool& v) { v = u8() != 0; }
        void operator()(int& v) { v = (int)u32(); }
        void operator()(unsigned& v) { v = u32(); }
        void operator()(long long& v) { v = (long long)u64(); }
        void operator()(double& v) { uint64_t x = u64(); std::memcpy(&v, &x, 8); }
        void operator()(std::string& v)
        {
            uint32_t n = u32();
            if (!need(n)) { v.clear(); return; }
            v.assign(m_b, m_p, n);
            m_p += n;
        }
        void operator()(GroundCmd& v) { int x = 0; (*this)(x); v = (x >= GC_UNKNOWN && x < GC_COUNT) ? (GroundCmd)x : GC_UNKNOWN; }
        void operator()(std::map<std::string, int>& m)
        {
            m.clear();
            uint32_t n = u32();
            if (n > 100000) { m_ok = false; return; }
            for (uint32_t i = 0; i < n && m_ok; ++i) { std::string k; int v = 0; (*this)(k); (*this)(v); m[k] = v; }
        }
        void operator()(std::map<std::string, std::string>& m)
        {
            m.clear();
            uint32_t n = u32();
            if (n > 100000) { m_ok = false; return; }
            for (uint32_t i = 0; i < n && m_ok; ++i) { std::string k, v; (*this)(k); (*this)(v); m[k] = v; }
        }
        template<class T> void operator()(std::vector<T>& v)
        {
            v.clear();
            uint32_t n = u32();
            if (n > 100000 || !m_ok) { m_ok = false; return; }
            v.resize(n);
            for (uint32_t i = 0; i < n && m_ok; ++i) (*this)(v[i]);
        }
        template<class T, size_t N> void operator()(T (&arr)[N])
        {
            for (size_t i = 0; i < N; ++i) (*this)(arr[i]);
        }
        template<class T> void operator()(T& v) { Io(*this, v); }
    private:
        bool need(size_t n)
        {
            if (!m_ok || m_b.size() - m_p < n || m_p > m_b.size()) { m_ok = false; return false; }
            return true;
        }
        const std::string& m_b;
        size_t m_p = 0;
        bool m_ok = true;
    };

    template<class A> void Io(A& a, DlLine& d);
    template<class A> void Io(A& a, StripData& s);
    template<class A> void Io(A& a, TrashItem& t);
    template<class A> void Io(A& a, Board::Options& o);
    template<class A> void Io(A& a, Board& b);

    template<class A> void Io(A& a, DlLine& d)
    {
        a(d.time);
        a(d.up);
        a(d.text);
        a(d.status);
    }

    template<class A> void Io(A& a, StripData& s)
    {
        a(s.callsign);
        a(s.acType);
        a(s.wtc);
        a(s.rules);
        a(s.adep);
        a(s.ades);
        a(s.altn);
        a(s.sid);
        a(s.rwy);
        a(s.squawkAssigned);
        a(s.squawkCurrent);
        a(s.scratch);
        a(s.eobt);
        a(s.tsat);
        a(s.tsatUtc);
        a(s.tsatSource);
        a(s.ctot);
        a(s.ttot);
        a(s.ctotSource);
        a(s.route);
        a(s.remarks);
        a(s.groundStateRaw);
        a(s.comm);
        a(s.spoken);
        a(s.adesName);
        a(s.adepName);
        a(s.arrival);
        a(s.stand);
        a(s.star);
        a(s.arrRwy);
        a(s.onGround);
        a(s.distDest);
        a(s.landingCleared);
        a(s.dlLogon);
        a(s.dlDcl);
        a(s.dlUnread);
        a(s.dlPending);
        a(s.dlPendingText);
        a(s.dlRequest);
        a(s.dlDclText);
        a(s.dlContactText);
        a(s.dlLog);
        a(s.goAround);
        a(s.ctotUtc);
        a(s.standConflict);
        a(s.standConflictWith);
        a(s.rwyInactive);
        a(s.clearedAlt);
        a(s.clearedApproach);
        a(s.finalAlt);
        a(s.assignedHeading);
        a(s.assignedSpeed);
        a(s.groundSpeed);
        a(s.altitude);
        a(s.intersection);
        a(s.intAssigned);
        a(s.airborne);
        a(s.gndStand);
        a(s.hovDue);
        a(s.handedOver);
        a(s.holdShort);
        a(s.toGround);
        a(s.freeStrip);
        a(s.designator);
        a(s.planUtc);
        a(s.pushDir);
        a(s.nextFreq);
        a(s.nextCtrl);
        a(s.rwyGroup);
        a(s.rwyUnknown);
        a(s.state);
        a(s.clearance);
        a(s.selected);
        a(s.localOnly);
        a(s.handedOff);
        a(s.xpdrOff);
        a(s.standBusy);
        a(s.standBusyBy);
        a(s.standMissing);
        a(s.atGate);
        a(s.vsidErr);
        a(s.vsidErrText);
        a(s.resp);
        a(s.respMine);
        a(s.stdClr);
        a(s.heli);
        a(s.diverted);
        a(s.trackedOther);
        a(s.hl);
        a(s.divertFrom);
        a(s.pinned);
        a(s.nextResp);
        a(s.offerTo);
        a(s.offerForMe);
        a(s.offerBlink);
        a(s.offerRejectedBy);
        a(s.fading);
    }

    template<class A> void Io(A& a, TrashItem& t)
    {
        a(t.id);
        a(t.label);
        a(t.kind);
        a(t.arrival);
    }

    template<class A> void Io(A& a, Board::Options& o)
    {
        a(o.splitEnabled);
        a(o.autoVacate);
        a(o.wx);
        a(o.vacdm);
        a(o.dlOn);
        a(o.dlCode);
        a(o.dlAutoLogon);
        a(o.dlSound);
        a(o.takeoffDepa);
        a(o.nextBtn);
        a(o.tsatSort);
        a(o.timeline);
        a(o.tlPast);
        a(o.tlFuture);
        a(o.tlDfs);
        a(o.tlNight);
        a(o.tlEst);
        a(o.offerBlink);
        a(o.offerRemind);
        a(o.arrNm);
        a(o.transitionAlt);
        a(o.dlStation);
        a(o.gndStands);
        a(o.hsList);
        a(o.intNorth);
        a(o.intSouth);
        a(o.colWidths);
        // ab 3.1.0 bzw. 4.2.0 (optional, fehlt bei aelteren Gegenstellen)
        if (a.more()) a(o.respOn);
        if (a.more()) a(o.taxi);
        if (a.more()) a(o.deice);
        if (a.more()) a(o.updAvail);
        if (a.more()) a(o.verWarn);
        if (a.more()) a(o.updCheck);
    }

    template<class A> void Io(A& a, Board& b)
    {
        a(b.bays);
        a(b.airports);
        a(b.transitionAltitude);
        a(b.connected);
        a(b.icaoLoaded);
        a(b.rwySplit);
        a(b.delOnline);
        a(b.apnOnline);
        a(b.gndOnline);
        a(b.topDown);
        a(b.vacdmError);
        a(b.rwyState);
        a(b.hasApron);
        a(b.splitGnd);
        a(b.profile);
        a(b.profileForced);
        a(b.autoHandoffFt);
        a(b.dlEnabled);
        a(b.dlConnected);
        a(b.dlStation);
        a(b.wxAirport);
        a(b.metar);
        a(b.atis);
        a(b.trash);
        a(b.rwyName);
        a(b.rwyActive);
        a(b.undoLabel);
        a(b.rwySwitchCount);
        a(b.rwySwitchTo);
        a(b.view);
        a(b.rwyFilter);
        a(b.remoteOn);
        a(b.remoteClients);
        a(b.remotePort);
        a(b.remoteCode);
        a(b.remoteAddr);
        a(b.opt);
    }

    inline std::string EncodeBoard(const Board& board)
    {
        Board b = board;   // Io braucht nicht-konstante Referenzen
        Writer w;
        Io(w, b);
        return w.buf;
    }

    inline bool DecodeBoard(const std::string& data, size_t pos, Board& b)
    {
        Reader r(data, pos);
        Board x;
        Io(r, x);
        if (!r.ok()) return false;
        b = std::move(x);
        return true;
    }
}
