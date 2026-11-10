// StripBay Remote - Protokoll (ohne Sockets, siehe RemoteProto.h)
#include "RemoteProto.h"

#include <algorithm>

namespace remote
{
    // ---------------------------------------------------------------------------
    // Rahmen
    // ---------------------------------------------------------------------------
    std::string Frame(const std::string& payload)
    {
        wire::Writer w;
        w.u32((uint32_t)payload.size());
        w.buf += payload;
        return w.buf;
    }

    void FrameReader::Feed(const char* data, size_t n)
    {
        if (m_bad) return;
        m_buf.append(data, n);
    }

    bool FrameReader::Next(std::string& payload)
    {
        if (m_bad || m_buf.size() < 4) return false;
        uint32_t len = 0;
        for (int k = 0; k < 4; ++k) len |= (uint32_t)(uint8_t)m_buf[k] << (8 * k);
        if (len == 0 || len > kMaxFrame) { m_bad = true; return false; }
        if (m_buf.size() - 4 < len) return false;
        payload.assign(m_buf, 4, len);
        m_buf.erase(0, 4 + (size_t)len);
        return true;
    }

    // Kleine Helfer: Werte schreiben, ohne erst Kopien anzulegen
    namespace
    {
        void S(wire::Writer& w, const std::string& v) { std::string x = v; w(x); }
        void I(wire::Writer& w, int v) { w(v); }
        void B(wire::Writer& w, bool v) { w(v); }
        std::string RS(wire::Reader& r) { std::string v; r(v); return v; }
        int RI(wire::Reader& r) { int v = 0; r(v); return v; }
        bool RB(wire::Reader& r) { bool v = false; r(v); return v; }
    }

    std::string Hello(const std::string& code, const std::string& name, const std::string& version)
    {
        wire::Writer w;
        w.u8(M_HELLO);
        w.u32(kMagic);
        I(w, kProtocol);
        S(w, code);
        S(w, name);
        S(w, version);
        return w.buf;
    }

    bool ParseWelcome(const std::string& payload, std::string& name, std::string& version, std::string& reason)
    {
        wire::Reader r(payload);
        uint8_t t = r.u8();
        if (t == M_WELCOME)
        {
            name = RS(r);
            version = RS(r);
            return r.ok();
        }
        reason = t == M_REFUSED ? RS(r) : std::string("protocol");
        if (reason.empty()) reason = "refused";
        return false;
    }

    // ---------------------------------------------------------------------------
    // Hub (Plugin)
    // ---------------------------------------------------------------------------
    void Hub::Connected(int client, const std::string& addr, long long nowMs)
    {
        Client c;
        c.addr = addr;
        c.lastRx = nowMs;
        m_c[client] = c;
    }

    void Hub::Closed(int client)
    {
        auto it = m_c.find(client);
        if (it == m_c.end()) return;
        bool was = it->second.authed;
        if (was && log) log("Remote: " + it->second.name + " (" + it->second.addr + ") disconnected.");
        m_c.erase(it);
        if (was && changed) changed();
    }

    void Hub::SendTo(int client, const std::string& payload)
    {
        if (send) send(client, Frame(payload));
    }

    void Hub::Broadcast(const std::string& payload)
    {
        std::string f = Frame(payload);
        for (auto& kv : m_c)
            if (kv.second.authed && send) send(kv.first, f);
    }

    void Hub::Received(int client, const std::string& payload, long long nowMs)
    {
        auto it = m_c.find(client);
        if (it == m_c.end() || payload.empty()) return;
        it->second.lastRx = nowMs;
        wire::Reader r(payload);
        uint8_t t = r.u8();
        if (t == M_HELLO)
        {
            uint32_t magic = r.u32();
            int proto = RI(r);
            std::string cd = RS(r), nm = RS(r), ver = RS(r);
            wire::Writer w;
            if (!r.ok() || magic != kMagic || proto != kProtocol)
            {
                w.u8(M_REFUSED);
                S(w, "version " + version + " (protocol " + std::to_string(kProtocol) + ")");
                SendTo(client, w.buf);
                if (log) log("Remote: " + it->second.addr + " refused (different version " + ver + ").");
                if (drop) drop(client);
                return;
            }
            if (code.empty() || cd != code)
            {
                w.u8(M_REFUSED);
                S(w, "code");
                SendTo(client, w.buf);
                if (log) log("Remote: " + it->second.addr + " refused (wrong code).");
                if (drop) drop(client);
                return;
            }
            it->second.authed = true;
            it->second.name = nm.empty() ? it->second.addr : nm;
            w.u8(M_WELCOME);
            S(w, name);
            S(w, version);
            SendTo(client, w.buf);
            if (!m_board.empty())
            {
                std::string p(1, (char)M_BOARD);
                SendTo(client, p + m_board);
            }
            if (log) log("Remote: " + it->second.name + " (" + it->second.addr + ") connected.");
            if (changed) changed();
            return;
        }
        if (!it->second.authed)
        {
            if (drop) drop(client);
            return;
        }
        if (t == M_PING)
        {
            SendTo(client, std::string(1, (char)M_PONG));
            return;
        }
        if (t == M_CALL) { Dispatch(client, r); return; }
        if (t == M_REQ) { Request(client, r); return; }
    }

    void Hub::Dispatch(int client, wire::Reader& r)
    {
        (void)client;
        if (!host) return;
        uint8_t c = r.u8();
        switch (c)
        {
        case C_STRIP_MOVED: { std::string cs = RS(r); int bay = RI(r), idx = RI(r); if (r.ok()) host->StripMoved(cs, bay, idx); break; }
        case C_STRIP_TAPPED: { std::string cs = RS(r); if (r.ok()) host->StripTapped(cs); break; }
        case C_SET_GROUND_STATE: { std::string cs = RS(r); int g = RI(r); if (r.ok()) host->SetGroundState(cs, g); break; }
        case C_TOGGLE_CLEARANCE: { std::string cs = RS(r); if (r.ok()) host->ToggleClearance(cs); break; }
        case C_TOGGLE_RWY_CLEARANCE: { std::string cs = RS(r); if (r.ok()) host->ToggleRunwayClearance(cs); break; }
        case C_GO_AROUND: { std::string cs = RS(r); if (r.ok()) host->GoAround(cs); break; }
        case C_COMMAND: { std::string l = RS(r); if (r.ok()) host->Command(l); break; }
        case C_SET_STRIP_GROUP: { std::string cs = RS(r); int g = RI(r); if (r.ok()) host->SetStripGroup(cs, g); break; }
        case C_DATALINK: { std::string cs = RS(r); int a = RI(r); std::string t = RS(r); if (r.ok()) host->Datalink(cs, a, t); break; }
        case C_SET_INTERSECTION: { std::string cs = RS(r), t = RS(r); bool as = RB(r); if (r.ok()) host->SetIntersection(cs, t, as); break; }
        case C_TOGGLE_HANDOVER: { std::string cs = RS(r); if (r.ok()) host->ToggleHandover(cs); break; }
        case C_SET_HOLD_SHORT: { std::string cs = RS(r), t = RS(r); if (r.ok()) host->SetHoldShort(cs, t); break; }
        case C_CREATE_STRIP: { std::string t = RS(r); bool note = RB(r); int bay = RI(r); if (r.ok()) host->CreateStrip(t, note, bay); break; }
        case C_RESTORE_STRIP: { std::string id = RS(r); if (r.ok()) host->RestoreStrip(id); break; }
        case C_SET_FIELD: { std::string cs = RS(r); int f = RI(r); std::string v = RS(r); if (r.ok()) host->SetField(cs, f, v); break; }
        case C_SET_SCRATCHPAD: { std::string cs = RS(r), t = RS(r); if (r.ok()) host->SetScratchpad(cs, t); break; }
        case C_REMOVE_STRIP: { std::string cs = RS(r); if (r.ok()) host->RemoveStrip(cs); break; }
        case C_VIEW_FILTER: { int v = RI(r), f = RI(r); if (r.ok() && setViewFilter) setViewFilter(v, f); break; }
        default: break;
        }
    }

    void Hub::Request(int client, wire::Reader& r)
    {
        uint32_t id = r.u32();
        uint8_t k = r.u8();
        wire::Writer w;
        w.u8(M_REPLY);
        w.u32(id);
        if (k == R_FIELD_OPTIONS)
        {
            std::string cs = RS(r);
            int f = RI(r);
            std::vector<std::string> o;
            if (r.ok() && host) o = host->FieldOptions(cs, f);
            w(o);
        }
        else if (k == R_STAND_STATUS)
        {
            std::string cs = RS(r);
            std::map<std::string, int> m;
            if (r.ok() && host) m = host->StandStatus(cs);
            w(m);
        }
        SendTo(client, w.buf);
    }

    void Hub::Board(const ::Board& b)
    {
        if (Count() == 0) { m_board.clear(); return; }   // niemand da: nichts kodieren
        std::string enc = wire::EncodeBoard(b);
        if (enc == m_board) return;
        m_board.swap(enc);
        Broadcast(std::string(1, (char)M_BOARD) + m_board);
    }

    void Hub::Sound(const std::string& tone)
    {
        if (Count() == 0 || tone.empty() || tone == "off") return;
        wire::Writer w;
        w.u8(M_SOUND);
        S(w, tone);
        Broadcast(w.buf);
    }

    void Hub::Note(const std::string& text)
    {
        if (Count() == 0 || text.empty()) return;
        wire::Writer w;
        w.u8(M_NOTE);
        S(w, text);
        Broadcast(w.buf);
    }

    void Hub::Tick(long long nowMs)
    {
        std::vector<int> quiet;
        for (auto& kv : m_c)
            if (nowMs - kv.second.lastRx > (kv.second.authed ? 15000 : 10000)) quiet.push_back(kv.first);
        for (int c : quiet)
            if (drop) drop(c);
    }

    void Hub::DropAll()
    {
        std::vector<int> all;
        for (auto& kv : m_c) all.push_back(kv.first);
        for (int c : all)
            if (drop) drop(c);
    }

    int Hub::Count() const
    {
        int n = 0;
        for (auto& kv : m_c)
            if (kv.second.authed) ++n;
        return n;
    }

    std::vector<std::string> Hub::Names() const
    {
        std::vector<std::string> v;
        for (auto& kv : m_c)
            if (kv.second.authed) v.push_back(kv.second.name);
        return v;
    }

    // ---------------------------------------------------------------------------
    // Link (Geraet)
    // ---------------------------------------------------------------------------
    void Link::Out(const std::string& payload)
    {
        if (send && online) send(Frame(payload));
    }

    namespace
    {
        wire::Writer CallW(uint8_t c)
        {
            wire::Writer w;
            w.u8(M_CALL);
            w.u8(c);
            return w;
        }
    }

    void Link::SendViewFilter(int view, int filter)
    {
        wire::Writer w = CallW(C_VIEW_FILTER);
        I(w, view);
        I(w, filter);
        Out(w.buf);
    }

    void Link::Ping() { Out(std::string(1, (char)M_PING)); }

    void Link::StripMoved(const std::string& cs, int bay, int index)
    {
        wire::Writer w = CallW(C_STRIP_MOVED);
        S(w, cs); I(w, bay); I(w, index);
        Out(w.buf);
    }
    void Link::StripTapped(const std::string& cs) { wire::Writer w = CallW(C_STRIP_TAPPED); S(w, cs); Out(w.buf); }
    void Link::SetGroundState(const std::string& cs, int g) { wire::Writer w = CallW(C_SET_GROUND_STATE); S(w, cs); I(w, g); Out(w.buf); }
    void Link::ToggleClearance(const std::string& cs) { wire::Writer w = CallW(C_TOGGLE_CLEARANCE); S(w, cs); Out(w.buf); }
    void Link::ToggleRunwayClearance(const std::string& cs) { wire::Writer w = CallW(C_TOGGLE_RWY_CLEARANCE); S(w, cs); Out(w.buf); }
    void Link::GoAround(const std::string& cs) { wire::Writer w = CallW(C_GO_AROUND); S(w, cs); Out(w.buf); }
    void Link::Command(const std::string& line)
    {
        if (localCommand && localCommand(line)) return;
        wire::Writer w = CallW(C_COMMAND);
        S(w, line);
        Out(w.buf);
    }
    void Link::SetStripGroup(const std::string& cs, int g) { wire::Writer w = CallW(C_SET_STRIP_GROUP); S(w, cs); I(w, g); Out(w.buf); }
    void Link::Datalink(const std::string& cs, int a, const std::string& t)
    {
        wire::Writer w = CallW(C_DATALINK);
        S(w, cs); I(w, a); S(w, t);
        Out(w.buf);
    }
    void Link::SetIntersection(const std::string& cs, const std::string& t, bool as)
    {
        wire::Writer w = CallW(C_SET_INTERSECTION);
        S(w, cs); S(w, t); B(w, as);
        Out(w.buf);
    }
    void Link::ToggleHandover(const std::string& cs) { wire::Writer w = CallW(C_TOGGLE_HANDOVER); S(w, cs); Out(w.buf); }
    void Link::SetHoldShort(const std::string& cs, const std::string& t) { wire::Writer w = CallW(C_SET_HOLD_SHORT); S(w, cs); S(w, t); Out(w.buf); }
    void Link::CreateStrip(const std::string& t, bool asNote, int bay)
    {
        wire::Writer w = CallW(C_CREATE_STRIP);
        S(w, t); B(w, asNote); I(w, bay);
        Out(w.buf);
    }
    void Link::RestoreStrip(const std::string& id) { wire::Writer w = CallW(C_RESTORE_STRIP); S(w, id); Out(w.buf); }
    void Link::SetField(const std::string& cs, int f, const std::string& v)
    {
        wire::Writer w = CallW(C_SET_FIELD);
        S(w, cs); I(w, f); S(w, v);
        Out(w.buf);
    }
    void Link::SetScratchpad(const std::string& cs, const std::string& t) { wire::Writer w = CallW(C_SET_SCRATCHPAD); S(w, cs); S(w, t); Out(w.buf); }
    void Link::RemoveStrip(const std::string& cs) { wire::Writer w = CallW(C_REMOVE_STRIP); S(w, cs); Out(w.buf); }
    void Link::WindowStateChanged()
    {
        if (windowState) windowState();
    }

    std::vector<std::string> Link::FieldOptions(const std::string& cs, int field)
    {
        std::vector<std::string> o;
        if (!online || !send || !wait) return o;
        uint32_t id = m_nextId++;
        wire::Writer w;
        w.u8(M_REQ);
        w.u32(id);
        w.u8(R_FIELD_OPTIONS);
        S(w, cs);
        I(w, field);
        send(Frame(w.buf));
        std::string rep;
        if (!wait(id, rep)) return o;
        wire::Reader r(rep);
        if (r.u8() != M_REPLY || r.u32() != id) return o;
        r(o);
        if (!r.ok()) o.clear();
        return o;
    }

    std::map<std::string, int> Link::StandStatus(const std::string& cs)
    {
        std::map<std::string, int> m;
        if (!online || !send || !wait) return m;
        uint32_t id = m_nextId++;
        wire::Writer w;
        w.u8(M_REQ);
        w.u32(id);
        w.u8(R_STAND_STATUS);
        S(w, cs);
        send(Frame(w.buf));
        std::string rep;
        if (!wait(id, rep)) return m;
        wire::Reader r(rep);
        if (r.u8() != M_REPLY || r.u32() != id) return m;
        r(m);
        if (!r.ok()) m.clear();
        return m;
    }
}
