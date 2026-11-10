// StripBay - Einstiegspunkte, die EuroScope beim Laden und Entladen aufruft.
#include "StripBayPlugin.h"

static StripBayPlugin* g_instance = nullptr;

void __declspec(dllexport) EuroScopePlugInInit(EuroScopePlugIn::CPlugIn** ppPlugInInstance)
{
    *ppPlugInInstance = g_instance = new StripBayPlugin();
}

void __declspec(dllexport) EuroScopePlugInExit(void)
{
    delete g_instance;
    g_instance = nullptr;
}
