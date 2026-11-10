# StripBay Remote - Windows-Firewall auf dem EuroScope-PC freigeben.
# Wird von REMOTE-FIREWALL.bat mit Administratorrechten gestartet.
param([int]$Port = 47810)

$Udp = 47811
$virtualRx = 'vEthernet|VirtualBox|VMware|WSL|Hyper-V|Loopback|Bluetooth|VPN|TAP-'

function Ask([string]$q)
{
    $a = Read-Host "$q (J/N)"
    return ($a -match '^\s*(j|ja|y|yes)\s*$')
}

Write-Host ""
Write-Host " StripBay Remote - Windows-Firewall"
Write-Host " ----------------------------------"
Write-Host ""

# --- 1. Freigabe fuer Verbindung (TCP) und Suche (UDP), private Netzwerke ---
try
{
    Get-NetFirewallRule -DisplayName 'StripBay Remote*' -ErrorAction SilentlyContinue | Remove-NetFirewallRule -ErrorAction SilentlyContinue
    New-NetFirewallRule -DisplayName 'StripBay Remote (Verbindung, TCP)' -Direction Inbound -Action Allow -Protocol TCP `
        -LocalPort $Port -Profile Domain, Private -ErrorAction Stop | Out-Null
    New-NetFirewallRule -DisplayName 'StripBay Remote (Suche, UDP)' -Direction Inbound -Action Allow -Protocol UDP `
        -LocalPort $Udp -Profile Domain, Private -ErrorAction Stop | Out-Null
    Write-Host " [OK] Eingehend erlaubt: TCP $Port (Verbindung) und UDP $Udp (Suche), private Netzwerke."
}
catch
{
    Write-Host " [FEHLER] Freigabe konnte nicht angelegt werden: $($_.Exception.Message)"
}

# --- 2. Sperren fuer EuroScope ---------------------------------------------
Write-Host ""
Write-Host " Suche Sperren fuer EuroScope, das dauert einen Moment ..."
$block = @()
try
{
    $block = @(Get-NetFirewallApplicationFilter -ErrorAction SilentlyContinue |
               Where-Object { $_.Program -match 'euroscope' } |
               Get-NetFirewallRule -ErrorAction SilentlyContinue |
               Where-Object { $_.Direction -eq 'Inbound' -and $_.Action -eq 'Block' -and $_.Enabled -eq 'True' })
}
catch { }
if ($block.Count -gt 0)
{
    Write-Host " Die Firewall sperrt EuroScope fuer eingehende Verbindungen:"
    $block | ForEach-Object { Write-Host "   - $($_.DisplayName)  [$($_.Profile)]" }
    Write-Host " (Das passiert, wenn die Firewall-Abfrage einmal mit 'Abbrechen' geschlossen wurde.)"
    if (Ask " Diese Sperren abschalten?")
    {
        try { $block | Disable-NetFirewallRule -ErrorAction Stop; Write-Host " [OK] Sperren abgeschaltet." }
        catch { Write-Host " [FEHLER] $($_.Exception.Message)" }
    }
    else
    {
        Write-Host " Sperren bleiben - dann kann sich StripBay Remote nicht verbinden."
    }
}
else
{
    Write-Host " [OK] Keine Sperre fuer EuroScope gefunden."
}

# --- 3. Netzwerk als "privat" einstufen --------------------------------------
Write-Host ""
$pub = @(Get-NetConnectionProfile -ErrorAction SilentlyContinue |
         Where-Object { $_.NetworkCategory -eq 'Public' -and $_.InterfaceAlias -notmatch $virtualRx })
if ($pub.Count -gt 0)
{
    Write-Host " Diese Verbindung ist als 'oeffentliches Netzwerk' eingestuft, dort gilt die Freigabe nicht:"
    $pub | ForEach-Object { Write-Host "   - $($_.Name)  ($($_.InterfaceAlias))" }
    if (Ask " Ist das dein Heimnetz? Als 'privates Netzwerk' einstufen?")
    {
        foreach ($p in $pub)
        {
            try { Set-NetConnectionProfile -InterfaceIndex $p.InterfaceIndex -NetworkCategory Private -ErrorAction Stop }
            catch { Write-Host " [FEHLER] $($p.Name): $($_.Exception.Message)" }
        }
        Write-Host " [OK] Als privates Netzwerk eingestuft."
    }
    else
    {
        Write-Host " Nicht geaendert - in diesem Netz kann sich StripBay Remote nicht verbinden."
    }
}
else
{
    Write-Host " [OK] Das Netzwerk ist als privat eingestuft."
}

# --- 4. Adresse dieses PCs ---------------------------------------------------
Write-Host ""
Write-Host " Adresse dieses PCs fuer StripBay Remote (falls 'Search' nichts findet):"
try
{
    Get-NetIPConfiguration -ErrorAction Stop |
        Where-Object { $_.IPv4DefaultGateway -and $_.NetAdapter.Status -eq 'Up' } |
        ForEach-Object { Write-Host "   $(@($_.IPv4Address.IPAddress) -join ', ')   ($($_.InterfaceAlias))" }
}
catch { Write-Host "   (unbekannt - in EuroScope: .strips remote status)" }

Write-Host ""
Write-Host " Fertig. Jetzt in EuroScope '.strips remote on' (falls noch nicht an) und auf dem"
Write-Host " anderen Geraet in StripBay Remote 'Search' druecken."
Write-Host " Hast du ein Sicherheitsprogramm mit eigener Firewall (z. B. Norton, Kaspersky,"
Write-Host " Bitdefender, Avast), muss EuroScope dort zusaetzlich erlaubt werden."
