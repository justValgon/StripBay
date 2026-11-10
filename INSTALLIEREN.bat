@echo off
rem ===========================================================================
rem  StripBay - Installer fuer EuroScope
rem
rem  Doppelklick genuegt. Der Installer
rem    1. holt EuroScopePlugIn.h und EuroScopePlugInDll.lib aus deinem
rem       EuroScope-Ordner (Unterordner PlugInEnvironment),
rem    2. sucht den Microsoft-C++-Compiler (Visual Studio oder Build Tools)
rem       und bietet an, ihn zu installieren, falls er fehlt,
rem    3. baut Release / x86 (StripBay.dll und StripBay Remote) und
rem    4. installiert StripBay.dll nach %APPDATA%\EuroScope\Plugins\StripBay.
rem       StripBay Remote (fuer ein weiteres Windows-Geraet) liegt danach im
rem       Ordner "Remote" neben diesem Skript.
rem
rem  Die SDK-Dateien duerfen auch einfach neben diesem Skript liegen.
rem
rem  Optional: EuroScope-Ordner als Argument, z. B.
rem    INSTALLIEREN.bat "D:\Spiele\EuroScope"
rem ===========================================================================
setlocal EnableExtensions
cd /d "%~dp0"
title StripBay - Installer

set "SDK=%~dp0StripBay\sdk"
set "ESDIR=%~1"

echo.
echo  StripBay - Installer fuer EuroScope
echo  ----------------------------------
echo.

rem ---------------------------------------------------------------------------
rem 1. EuroScope-SDK
rem ---------------------------------------------------------------------------
if exist "%SDK%\EuroScopePlugIn.h" if exist "%SDK%\EuroScopePlugInDll.lib" goto sdk_ok

if not "%ESDIR%"=="" goto sdk_try_arg

rem 0) neben diesem Skript abgelegt?
call :try_es "%~dp0."
if defined ESFOUND goto sdk_copy

rem a) uebliche Ordner (auch im EuroScope-Datenordner, z. B. AppData\Roaming\EuroScope\PlugIn)
call :try_es "%APPDATA%\EuroScope\PlugIn"
if defined ESFOUND goto sdk_copy
call :try_es "%APPDATA%\EuroScope\PlugInEnvironment"
if defined ESFOUND goto sdk_copy
call :try_es "%APPDATA%\EuroScope"
if defined ESFOUND goto sdk_copy
call :search_root "%APPDATA%\EuroScope"
if defined ESFOUND goto sdk_copy
call :try_es "%ProgramFiles(x86)%\EuroScope"
if defined ESFOUND goto sdk_copy
call :try_es "%ProgramFiles%\EuroScope"
if defined ESFOUND goto sdk_copy
call :try_es "%LOCALAPPDATA%\EuroScope"
if defined ESFOUND goto sdk_copy
call :try_es "%LOCALAPPDATA%\Programs\EuroScope"
if defined ESFOUND goto sdk_copy
call :try_es "C:\EuroScope"
if defined ESFOUND goto sdk_copy

rem b) laufendes EuroScope fragen, wo es liegt
set "ESEXE="
for /f "usebackq delims=" %%p in (`powershell -NoProfile -Command "(Get-Process EuroScope -ErrorAction SilentlyContinue | Select-Object -First 1).Path"`) do set "ESEXE=%%p"
if defined ESEXE call :try_es_exe "%ESEXE%"
if defined ESFOUND goto sdk_copy

rem c) suchen
echo  Suche EuroScopePlugIn.h auf diesem PC, das kann ein bis zwei Minuten dauern ...
call :search_root "%ProgramFiles(x86)%"
if defined ESFOUND goto sdk_copy
call :search_root "%ProgramFiles%"
if defined ESFOUND goto sdk_copy
call :search_root "%USERPROFILE%"
if defined ESFOUND goto sdk_copy
for %%d in (C D E F G H) do if not defined ESFOUND if exist "%%d:\EuroScope" call :search_root "%%d:\EuroScope"
if defined ESFOUND goto sdk_copy
echo  Nichts gefunden.
echo.

:sdk_ask
echo  Bitte den Ordner angeben, in dem EuroScope.exe liegt.
echo  So findest du ihn: Rechtsklick auf die EuroScope-Verknuepfung,
echo  "Dateipfad oeffnen" bzw. "Speicherort oeffnen", dann oben in der
echo  Adresszeile den Pfad kopieren. Oder den Ordner mit den beiden Dateien
echo  EuroScopePlugIn.h und EuroScopePlugInDll.lib angeben.
echo  (Nicht AppData\Roaming\EuroScope, dort liegen nur die Einstellungen.)
echo.
set "ESDIR="
set /p "ESDIR=  Ordner: "
if "%ESDIR%"=="" goto fail
set "ESDIR=%ESDIR:"=%"

:sdk_try_arg
set "ESFOUND="
call :try_es "%ESDIR%"
if defined ESFOUND goto sdk_copy
call :search_root "%ESDIR%"
if defined ESFOUND goto sdk_copy
echo.
if exist "%ESDIR%\EuroScope.exe" (
    echo  EuroScope.exe liegt dort, aber ohne Ordner PlugInEnvironment.
    echo  Deiner EuroScope-Installation fehlen die Plugin-Entwicklerdateien.
    echo  Abhilfe: siehe README, Abschnitt Bauen, "SDK-Dateien fehlen".
) else (
    echo  In diesem Ordner liegen weder EuroScope.exe noch die SDK-Dateien.
)
echo.
goto sdk_ask

:sdk_copy
echo  EuroScope-SDK gefunden: %ESFOUND%
if not exist "%SDK%" mkdir "%SDK%"
copy /y "%ESFOUND%\EuroScopePlugIn.h" "%SDK%\" >nul || goto fail
copy /y "%ESFOUND%\EuroScopePlugInDll.lib" "%SDK%\" >nul || goto fail

:sdk_ok
echo  [1/4] EuroScope-SDK ist bereit.
echo.

rem ---------------------------------------------------------------------------
rem 2. Compiler suchen
rem ---------------------------------------------------------------------------
:find_msbuild
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "MSBUILD="
set "VSVER="
if not exist "%VSWHERE%" goto no_vs
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find MSBuild\**\Bin\MSBuild.exe`) do if not defined MSBUILD set "MSBUILD=%%i"
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion`) do if not defined VSVER set "VSVER=%%i"
if not defined MSBUILD goto no_vs

rem Toolset passend zur installierten Version (VS 2022 = v143, VS 2026 = v145)
set "TOOLSET=v143"
if "%VSVER:~0,3%"=="18." set "TOOLSET=v145"
if "%VSVER:~0,3%"=="16." set "TOOLSET=v142"
echo  [2/4] Compiler gefunden (Visual Studio %VSVER%, Toolset %TOOLSET%).
echo.
goto build

:no_vs
echo  Der Microsoft-C++-Compiler ist nicht installiert.
echo  Benoetigt werden die kostenlosen "Build Tools fuer Visual Studio 2022"
echo  mit "Desktopentwicklung mit C++" (ca. 2-3 GB).
echo.
where winget >nul 2>nul
if errorlevel 1 goto no_winget
set "ANSWER="
set /p "ANSWER=  Jetzt automatisch installieren? (j/n): "
if /i not "%ANSWER%"=="j" goto no_winget
echo.
echo  Installation laeuft, das dauert einige Minuten. Bitte eine Windows-Rueckfrage bestaetigen.
winget install --id Microsoft.VisualStudio.2022.BuildTools -e --accept-package-agreements --accept-source-agreements --override "--passive --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
echo.
goto find_msbuild_after_install

:find_msbuild_after_install
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" goto find_msbuild
echo  Installation nicht gefunden. Bitte PC neu starten und dieses Skript erneut ausfuehren.
goto fail

:no_winget
echo.
echo  Bitte von Hand installieren:
echo    https://visualstudio.microsoft.com/de/visual-cpp-build-tools/
echo    Im Installer "Desktopentwicklung mit C++" anhaken, installieren,
echo    danach dieses Skript erneut starten.
goto fail

rem ---------------------------------------------------------------------------
rem 3. Bauen
rem ---------------------------------------------------------------------------
:build
echo  [3/4] Baue StripBay.dll und StripBay Remote (Release, x86) ...
echo.
"%MSBUILD%" "%~dp0StripBay.sln" /m /nologo /v:minimal /p:Configuration=Release /p:Platform=x86 /p:PlatformToolset=%TOOLSET%
if errorlevel 1 goto build_fail
if not exist "%~dp0bin\Release\StripBay.dll" goto build_fail
copy /y "%~dp0bin\Release\StripBay.dll" "%~dp0StripBay.dll" >nul
rem StripBay Remote: fertig zum Kopieren auf das andere Geraet
if not exist "%~dp0Remote" mkdir "%~dp0Remote"
if exist "%~dp0bin\Release\StripBayRemote.exe" copy /y "%~dp0bin\Release\StripBayRemote.exe" "%~dp0Remote\StripBayRemote.exe" >nul

echo.
echo  [4/4] Installiere ...
set "TARGET=%APPDATA%\EuroScope\Plugins\StripBay"
if not exist "%TARGET%" mkdir "%TARGET%"
copy /y "%~dp0bin\Release\StripBay.dll" "%TARGET%\StripBay.dll" >nul
if errorlevel 1 goto install_locked
copy /y "%~dp0README.md" "%TARGET%\README.md" >nul

echo.
echo  ===========================================================
echo   StripBay ist installiert:
echo   %TARGET%\StripBay.dll
echo.
echo   Einmalig in EuroScope laden:
echo     OTHER SET - Plug-ins... - Load - die Datei oben waehlen
echo   Beim Beenden die Einstellungen speichern lassen, dann
echo   laedt EuroScope StripBay kuenftig automatisch.
echo.
echo   Berlin: .strips piste beide / nord / sued
echo.
echo   StripBay Remote (weiteres Windows-Geraet im selben Netz):
echo     1. In EuroScope: .strips remote on  (zeigt den Code)
echo     2. Ordner "Remote" auf das andere Geraet kopieren und
echo        StripBayRemote.exe starten, "Search", Code eingeben.
echo     Verbindet es nicht: REMOTE-FREIGABE.bat auf DIESEM PC
echo     einmal ausfuehren (Windows-Firewall).
echo  ===========================================================
echo.
explorer /select,"%TARGET%\StripBay.dll"
pause
exit /b 0

:install_locked
echo.
echo  StripBay.dll konnte nicht ersetzt werden, vermutlich ist sie in
echo  EuroScope gerade geladen. EuroScope schliessen und den Installer
echo  erneut starten. Die neue DLL liegt solange hier:
echo  %~dp0StripBay.dll
goto fail

:build_fail
echo.
echo  Der Build ist fehlgeschlagen. Die Meldungen oben zeigen den Grund.
echo  Bitte das Fenster abfotografieren oder den Text kopieren und schicken.
goto fail

rem ---------------------------------------------------------------------------
:try_es
rem %1 = Kandidat (EuroScope-Ordner oder direkt PlugInEnvironment)
set "ESFOUND="
if exist "%~1\PlugInEnvironment\EuroScopePlugIn.h" if exist "%~1\PlugInEnvironment\EuroScopePlugInDll.lib" set "ESFOUND=%~1\PlugInEnvironment"
if defined ESFOUND exit /b 0
if exist "%~1\EuroScopePlugIn.h" if exist "%~1\EuroScopePlugInDll.lib" set "ESFOUND=%~1"
exit /b 0

:try_es_exe
rem %1 = Pfad zu EuroScope.exe
set "ESFOUND="
call :try_es "%~dp1"
exit /b 0

:search_root
rem %1 = Ordner, rekursiv nach EuroScopePlugIn.h mit EuroScopePlugInDll.lib daneben suchen
set "ESFOUND="
if not exist "%~1" exit /b 0
for /f "delims=" %%f in ('where /r "%~1" EuroScopePlugIn.h 2^>nul') do if not defined ESFOUND if exist "%%~dpfEuroScopePlugInDll.lib" set "ESFOUND=%%~dpf"
if defined ESFOUND set "ESFOUND=%ESFOUND:~0,-1%"
exit /b 0

:fail
echo.
pause
exit /b 1
