@echo off
rem ===========================================================================
rem  StripBay Remote - Windows-Firewall freigeben (auf dem EuroScope-PC)
rem
rem  Einmal ausfuehren, wenn sich StripBay Remote vom anderen Geraet aus nicht
rem  verbinden kann. Das Skript fragt nach Administratorrechten und
rem    1. erlaubt eingehend TCP 47810 (Verbindung) und UDP 47811 (Suche)
rem       in privaten Netzwerken,
rem    2. zeigt Sperren der Firewall fuer EuroScope und schaltet sie auf
rem       Wunsch ab (entstehen, wenn die Firewall-Abfrage einmal mit
rem       "Abbrechen" geschlossen wurde),
rem    3. bietet an, ein als "oeffentlich" eingestuftes Heimnetz als "privat"
rem       einzustufen (sonst gilt die Freigabe dort nicht).
rem
rem  Anderer Port (.strips remote port N):   REMOTE-FREIGABE.bat N
rem ===========================================================================
setlocal EnableExtensions
title StripBay Remote - Firewall
set "PS1=%~dp0StripBay\tools\RemoteFreigabe.ps1"
set "PORT=%~1"
if not defined PORT set "PORT=47810"
if not exist "%PS1%" goto missing

rem Administratorrechte? Sonst neu starten mit Rueckfrage von Windows
net session >nul 2>&1
if not errorlevel 1 goto admin
echo.
echo  Windows fragt gleich nach Administratorrechten - bitte mit "Ja" bestaetigen.
powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -ArgumentList '%PORT%' -Verb RunAs"
exit /b 0

:admin
powershell -NoProfile -ExecutionPolicy Bypass -File "%PS1%" -Port %PORT%
echo.
pause
exit /b 0

:missing
echo.
echo  Die Datei StripBay\tools\RemoteFreigabe.ps1 fehlt.
echo  Bitte das StripBay-Paket vollstaendig entpacken.
echo.
pause
exit /b 1
