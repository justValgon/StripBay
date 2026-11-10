@echo off
rem ===========================================================================
rem  StripBay - SDK-Paket fuer Claude
rem
rem  Packt die Teile von Visual Studio (Build Tools) und Windows SDK, die zum
rem  Bauen von StripBay.dll und StripBayRemote.exe (32 Bit) noetig sind, in
rem  die Datei StripBay-SDK.zip neben diesem Skript.
rem
rem  Es wird nichts installiert und nichts veraendert. Die ZIP enthaelt nur
rem  Microsoft-Header und -Bibliotheken, keine persoenlichen Daten.
rem ===========================================================================
setlocal EnableExtensions
title StripBay - SDK-Paket
cd /d "%~dp0"

set "PF86=%ProgramFiles(x86)%"
if not defined PF86 set "PF86=%ProgramFiles%"
set "VSWHERE=%PF86%\Microsoft Visual Studio\Installer\vswhere.exe"
set "KITS=%PF86%\Windows Kits\10"
set "WORK=%TEMP%\StripBaySDK"
set "OUT=%~dp0StripBay-SDK.zip"
set "RC=/NFL /NDL /NJH /NJS /NP /R:1 /W:1"

echo.
echo  StripBay - SDK-Paket fuer Claude
echo  --------------------------------
echo.

rem --- Visual Studio / Build Tools mit C++ ----------------------------------
if not exist "%VSWHERE%" goto no_vs
set "VS="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do if not defined VS set "VS=%%i"
if not defined VS goto no_vs

set "VCVER="
if exist "%VS%\VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt" set /p VCVER=<"%VS%\VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt"
if defined VCVER for /f "tokens=1" %%v in ("%VCVER%") do set "VCVER=%%v"
if defined VCVER if not exist "%VS%\VC\Tools\MSVC\%VCVER%\lib\x86\libcpmt.lib" set "VCVER="
if not defined VCVER for /f "delims=" %%d in ('dir /b /ad /o-n "%VS%\VC\Tools\MSVC" 2^>nul') do if not defined VCVER if exist "%VS%\VC\Tools\MSVC\%%d\lib\x86\libcpmt.lib" set "VCVER=%%d"
if not defined VCVER goto no_vs
set "VC=%VS%\VC\Tools\MSVC\%VCVER%"

rem --- Windows SDK (neueste Version mit 32-Bit-Bibliotheken) ----------------
set "SDKVER="
for /f "delims=" %%d in ('dir /b /ad /o-n "%KITS%\Include" 2^>nul') do if not defined SDKVER if exist "%KITS%\Include\%%d\um\windows.h" if exist "%KITS%\Lib\%%d\um\x86\kernel32.lib" if exist "%KITS%\Lib\%%d\ucrt\x86\libucrt.lib" set "SDKVER=%%d"
if not defined SDKVER goto no_sdk

echo  Visual Studio : %VS%
echo  MSVC          : %VCVER%
echo  Windows SDK   : %SDKVER%
echo.

rem --- Kopieren -------------------------------------------------------------
if exist "%WORK%" rmdir /s /q "%WORK%"
mkdir "%WORK%" || goto fail
echo  Kopiere Dateien, bitte kurz warten ...

robocopy "%VC%\include" "%WORK%\msvc\include" /E %RC% >nul
if errorlevel 8 goto fail
robocopy "%VC%\lib\x86" "%WORK%\msvc\lib\x86" libcmt.lib libcpmt.lib libvcruntime.lib oldnames.lib libconcrt.lib %RC% >nul
if errorlevel 8 goto fail

robocopy "%KITS%\Include\%SDKVER%\ucrt" "%WORK%\sdk\include\ucrt" /E %RC% >nul
if errorlevel 8 goto fail
robocopy "%KITS%\Include\%SDKVER%\um" "%WORK%\sdk\include\um" /E %RC% >nul
if errorlevel 8 goto fail
robocopy "%KITS%\Include\%SDKVER%\shared" "%WORK%\sdk\include\shared" /E %RC% >nul
if errorlevel 8 goto fail

robocopy "%KITS%\Lib\%SDKVER%\ucrt\x86" "%WORK%\sdk\lib\ucrt\x86" libucrt.lib %RC% >nul
if errorlevel 8 goto fail
robocopy "%KITS%\Lib\%SDKVER%\um\x86" "%WORK%\sdk\lib\um\x86" kernel32.lib user32.lib gdi32.lib msimg32.lib winhttp.lib ole32.lib oleaut32.lib ws2_32.lib winmm.lib comctl32.lib iphlpapi.lib uuid.lib advapi32.lib shell32.lib shlwapi.lib comdlg32.lib imm32.lib version.lib uxtheme.lib dwmapi.lib bcrypt.lib ntdll.lib %RC% >nul
if errorlevel 8 goto fail

>"%WORK%\VERSIONEN.txt" echo MSVC %VCVER%
>>"%WORK%\VERSIONEN.txt" echo SDK %SDKVER%

rem --- ZIP packen -----------------------------------------------------------
if exist "%OUT%" del /q "%OUT%"
echo  Packe StripBay-SDK.zip ...
tar -a -c -f "%OUT%" -C "%WORK%" msvc sdk VERSIONEN.txt >nul 2>&1
if not exist "%OUT%" powershell -NoProfile -Command "Compress-Archive -Path '%WORK%\*' -DestinationPath '%OUT%' -CompressionLevel Optimal"
if not exist "%OUT%" goto fail
rmdir /s /q "%WORK%"

set "SIZE=0"
for %%z in ("%OUT%") do set "SIZE=%%~zz"
set /a MB=SIZE/1048576

echo.
echo  Fertig: StripBay-SDK.zip, etwa %MB% MB
echo  Diese Datei bitte im Chat bei Claude hochladen.
echo.
explorer /select,"%OUT%"
pause
exit /b 0

:no_vs
echo  Visual Studio bzw. die Build Tools mit C++ wurden nicht gefunden.
echo  Bitte zuerst INSTALL.bat ausfuehren, der installiert sie bei Bedarf.
goto end

:no_sdk
echo  Das Windows SDK mit 32-Bit-Bibliotheken wurde nicht gefunden
echo  (erwartet unter %KITS%).
echo  Bitte zuerst INSTALL.bat ausfuehren.
goto end

:fail
echo  Beim Kopieren oder Packen ist ein Fehler aufgetreten.
echo  Bitte ein Foto oder den Text dieses Fensters an Claude schicken.

:end
echo.
pause
exit /b 1
