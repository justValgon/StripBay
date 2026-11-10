// StripBay - Handschrifterkennung ueber die Windows-Tablet-PC-Automation (spaete Bindung)
#include "Ink.h"

#include <objbase.h>
#include <oleauto.h>

namespace
{
// CLSIDs aus msinkaut.h, falls die ProgIDs nicht registriert sind
const CLSID kClsidInkDisp = { 0x937C1A34, 0x151D, 0x4610, { 0x9C, 0xA6, 0xA8, 0xCC, 0x9B, 0xDB, 0x5D, 0x83 } };
const CLSID kClsidRecoContext = { 0xAAC46A37, 0x9229, 0x4FC0, { 0x8C, 0xCE, 0x44, 0x97, 0x56, 0x9B, 0xF4, 0xD1 } };

struct ComPtr
{
    IDispatch* p = nullptr;
    ~ComPtr() { if (p) p->Release(); }
};

IDispatch* Create(const wchar_t* progId, const CLSID& fallback)
{
    CLSID clsid;
    if (FAILED(CLSIDFromProgID(progId, &clsid))) clsid = fallback;
    IDispatch* d = nullptr;
    if (FAILED(CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, IID_IDispatch, (void**)&d))) return nullptr;
    return d;
}

// args in umgekehrter Reihenfolge (letzter Parameter zuerst), wie IDispatch es erwartet
HRESULT Call(IDispatch* d, const wchar_t* name, WORD flags, VARIANT* args, UINT nargs, VARIANT* result)
{
    DISPID id = 0;
    LPOLESTR n = const_cast<LPOLESTR>(name);
    HRESULT hr = d->GetIDsOfNames(IID_NULL, &n, 1, LOCALE_USER_DEFAULT, &id);
    if (FAILED(hr)) return hr;
    DISPPARAMS dp = {};
    dp.rgvarg = args;
    dp.cArgs = nargs;
    DISPID put = DISPID_PROPERTYPUT;
    if (flags & (DISPATCH_PROPERTYPUT | DISPATCH_PROPERTYPUTREF))
    {
        dp.rgdispidNamedArgs = &put;
        dp.cNamedArgs = 1;
    }
    return d->Invoke(id, IID_NULL, LOCALE_USER_DEFAULT, flags, &dp, result, nullptr, nullptr);
}
}

bool RecognizeInk(const std::vector<std::vector<POINT>>& strokes, unsigned dpi, std::string& text, std::string& error)
{
    text.clear();
    error.clear();
    if (strokes.empty()) return true;
    if (dpi == 0) dpi = 96;

    HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool uninit = SUCCEEDED(init);
    bool ok = false;
    {
        ComPtr ink, ctx;
        ink.p = Create(L"msinkaut.InkDisp", kClsidInkDisp);
        ctx.p = Create(L"msinkaut.InkRecognizerContext", kClsidRecoContext);
        if (!ink.p || !ctx.p)
        {
            error = "Windows handwriting recognition not available";
        }
        else
        {
            // Striche anlegen: Punkte in HIMETRIC (1/100 mm)
            bool strokesOk = true;
            for (const std::vector<POINT>& s : strokes)
            {
                if (s.empty()) continue;
                std::vector<POINT> pts = s;
                if (pts.size() == 1) pts.push_back({ pts[0].x + 1, pts[0].y + 1 });   // Punkt (z. B. i-Punkt)
                SAFEARRAY* sa = SafeArrayCreateVector(VT_I4, 0, (ULONG)(pts.size() * 2));
                if (!sa) { strokesOk = false; break; }
                LONG* data = nullptr;
                if (SUCCEEDED(SafeArrayAccessData(sa, (void**)&data)))
                {
                    for (size_t i = 0; i < pts.size(); ++i)
                    {
                        data[2 * i] = (LONG)((long long)pts[i].x * 2540 / dpi);
                        data[2 * i + 1] = (LONG)((long long)pts[i].y * 2540 / dpi);
                    }
                    SafeArrayUnaccessData(sa);
                }
                VARIANT args[2];
                VariantInit(&args[0]);            // PacketDescription: leer = X, Y
                VariantInit(&args[1]);
                args[1].vt = VT_ARRAY | VT_I4;    // PacketData
                args[1].parray = sa;
                VARIANT res;
                VariantInit(&res);
                HRESULT hr = Call(ink.p, L"CreateStroke", DISPATCH_METHOD, args, 2, &res);
                if (FAILED(hr))
                {
                    // manche Versionen wollen den optionalen Parameter als "fehlt"
                    args[0].vt = VT_ERROR;
                    args[0].scode = DISP_E_PARAMNOTFOUND;
                    hr = Call(ink.p, L"CreateStroke", DISPATCH_METHOD, args, 2, &res);
                }
                VariantClear(&res);
                SafeArrayDestroy(sa);
                if (FAILED(hr)) { strokesOk = false; break; }
            }
            VARIANT vStrokes;
            VariantInit(&vStrokes);
            if (!strokesOk || FAILED(Call(ink.p, L"Strokes", DISPATCH_PROPERTYGET, nullptr, 0, &vStrokes)) ||
                vStrokes.vt != VT_DISPATCH || !vStrokes.pdispVal)
            {
                error = "strokes could not be passed on";
            }
            else
            {
                // keine Woerterbuch-Vorgabe: Rufzeichen und Kuerzel statt Woerter
                VARIANT fac;
                VariantInit(&fac);
                fac.vt = VT_BSTR;
                fac.bstrVal = SysAllocString(L"NONE");
                Call(ctx.p, L"Factoid", DISPATCH_PROPERTYPUT, &fac, 1, nullptr);
                VariantClear(&fac);

                VARIANT ref;
                VariantInit(&ref);
                ref.vt = VT_DISPATCH;
                ref.pdispVal = vStrokes.pdispVal;
                HRESULT hr = Call(ctx.p, L"Strokes", DISPATCH_PROPERTYPUTREF, &ref, 1, nullptr);
                if (FAILED(hr)) hr = Call(ctx.p, L"Strokes", DISPATCH_PROPERTYPUT, &ref, 1, nullptr);
                LONG status = 0;
                VARIANT st;
                VariantInit(&st);
                st.vt = VT_BYREF | VT_I4;
                st.plVal = &status;
                VARIANT vRes;
                VariantInit(&vRes);
                if (SUCCEEDED(hr)) hr = Call(ctx.p, L"Recognize", DISPATCH_METHOD, &st, 1, &vRes);
                if (FAILED(hr) || vRes.vt != VT_DISPATCH || !vRes.pdispVal)
                {
                    error = "recognition failed";
                }
                else
                {
                    VARIANT vStr;
                    VariantInit(&vStr);
                    if (SUCCEEDED(Call(vRes.pdispVal, L"TopString", DISPATCH_PROPERTYGET, nullptr, 0, &vStr)) &&
                        vStr.vt == VT_BSTR && vStr.bstrVal)
                    {
                        for (const wchar_t* w = vStr.bstrVal; *w; ++w)
                        {
                            wchar_t c = *w;
                            // haeufige Verwechslungen bei Rufzeichen und Codes glaetten
                            if (c == 0x00D6 || c == 0x00F6) c = L'O';       // Oe
                            else if (c == 0x00DC || c == 0x00FC) c = L'U';  // Ue
                            else if (c == 0x00C4 || c == 0x00E4) c = L'A';  // Ae
                            if (c < 128) text += (char)c;
                        }
                        ok = true;
                    }
                    else error = "no result";
                    VariantClear(&vStr);
                }
                VariantClear(&vRes);
                // Strokes-Referenz am Kontext loesen, bevor die Tinte freigegeben wird
                VARIANT none;
                VariantInit(&none);
                none.vt = VT_DISPATCH;
                none.pdispVal = nullptr;
                Call(ctx.p, L"Strokes", DISPATCH_PROPERTYPUTREF, &none, 1, nullptr);
            }
            VariantClear(&vStrokes);
        }
    }
    if (uninit) CoUninitialize();
    return ok;
}
