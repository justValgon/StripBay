StripBay Remote - the strips on a second Windows device
=======================================================

(Deutsch: README.de.txt)

What it is
  The same strip window as in EuroScope, on a second Windows PC, laptop or
  Windows tablet in the same network (Wi-Fi/LAN). All actions go through
  StripBay in EuroScope, so they stay in sync with all other controllers
  as usual.

Setup
  1. On the EuroScope PC: run INSTALL.bat (it also builds StripBayRemote.exe
     and places it in this folder). Or take StripBayRemote.exe from the
     release ZIP.
  2. In EuroScope: .strips remote on
     The chat shows address and code (also in the StripBay settings under
     "Remote pairing").
  3. If Windows asks about the firewall: allow EuroScope for "Private
     networks". Safer: run REMOTE-FIREWALL.bat once on the EuroScope PC
     (it sits next to INSTALL.bat).
  4. Copy this folder to the other device (USB stick, network), start
     StripBayRemote.exe, press "Search" (or enter the name/IP of the
     EuroScope PC), enter the code, press "Connect".
  From then on StripBay Remote connects by itself at startup. If the
  EuroScope PC gets a new IP address, StripBay Remote finds it again by
  its name.

Cannot connect?
  The connect window (or the "NOT CONNECTED" card) names the reason:
  - "... answers, but StripBay Remote is not on there"
      -> in EuroScope: .strips remote on
  - "No answer from ..."
      -> almost always the Windows firewall on the EuroScope PC:
         run REMOTE-FIREWALL.bat there once. Also check: both devices in
         the same Wi-Fi/LAN (not a guest network)?
  - "... not in the network of this device"
      -> the address belongs to a different network (e.g. VPN, guest Wi-Fi).
  Self test on the EuroScope PC: start StripBayRemote.exe there and enter
  "localhost" as the PC. If that works, the problem is the network or the
  firewall. Different PC or new code: tap the "NOT CONNECTED" card.

Notes
  - View and area (Delivery, Ground, Tower, North/South) apply to the
    whole session, so they change on the PC as well. Zoom, layout,
    light/dark, fullscreen and window position are per device.
  - Sounds and StripBay messages also play on the device.
  - Connection lost: the window goes dark ("NOT CONNECTED") and accepts
    nothing until it is connected again (automatic retry every 3 s).
  - New code: .strips remote code new (all devices then have to enter it
    again). Different port: .strips remote port 47820, enter PC:47820 on
    the device.
  - Device settings: %APPDATA%\StripBay\StripBayRemote.ini
  - All controllers and the Remote device need the same major version
    (first number).
