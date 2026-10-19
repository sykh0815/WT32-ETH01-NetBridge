# Changelog

🇬🇧 **English** | 🇩🇪 [Deutsch](CHANGELOG.de.md)

All notable changes to this project are documented in this file.

Version numbers follow the scheme **MAJOR.MINOR.PATCH**:

- **MAJOR**: incompatible changes (e.g. settings have to be entered again)
- **MINOR**: new features
- **PATCH**: bug fixes only (written as e.g. `2.2.1`; a missing patch number means `.0`)

The current version is defined in `src/main.cpp` (`FIRMWARE_VERSION`) and shown in the web
interface and the serial output.

## [3.14] – 2026-10-19

### Added
- Display page **WiFi QR code** (optional in the rotation or permanently): phones join the WiFi of the
  bridge by scanning it (access point modes: own WiFi, client modes: setup WiFi). Own small QR
  generator (byte mode, version 1–6, ported from Project Nayuki's library, MIT), checked against the
  reference implementation and a QR reader.
- Display page **Today**: data volume down/up, peak rates and maximum number of WiFi devices since
  midnight (or since start until the time is known).
- **Clock** via the internet (NTP) with selectable time zone, shown in the display header.
- **Night mode** (can be switched off): display off or dimmed between two times.
- **Optional button on IO36** (to GND, 10 kΩ pull-up to 3V3): short press shows the next page for
  30 s and lifts night mode briefly, long press (1 s) switches the display on/off.
- Web interface "Display": new groups "Time & night mode" and "Button", page options "Today only" and
  "WiFi QR code only", switch "WiFi QR code in rotation".

### Changed
- **Larger app partition** (`min_spiffs.csv`: 2 × 1.9 MB instead of 2 × 1.25 MB) – the firmware had
  grown beyond 1.25 MB. The settings (NVS) stay at the same place and are kept. Flash once via USB
  (`pio run -t upload` or the full image from the release), because the partition table changes.

## [3.13] – 2026-10-19

### Added
- **Live preview of the display** in the web interface: the current display content (OLED or TFT, in
  colour) is shown in a small "display bezel" and updated every 2 s (only while the section is open).
- **Event notices on the display** (4 s, can be switched off): new WiFi device (with name), device left,
  cable plugged/unplugged, router WiFi connected/disconnected.

### Changed
- **Redesigned display section**: status with type and connection at the top, settings in two groups
  ("Hardware" and "Display content"), wiring plan folded away, compact switches instead of large tiles,
  offset X/Y in one row, page and interval side by side.

## [3.12] – 2026-10-19

### Added
- Display page **"System"**: CPU load per core and RAM usage as coloured bars (green, from 60 % orange,
  from 85 % red), free RAM, uptime and – if measured – the supply voltage (on the large TFT layout also
  chip and clock). Part of the automatic rotation, or shown permanently with "System only (CPU, RAM)".

## [3.11.2] – 2026-10-19

### Fixed
- Serial console: typed text was not visible because the monitor's exception decoder filter only shows
  complete lines. The monitor echoes the input again (`monitor_echo`), and the firmware confirms every
  recognized command with a line `> command`.

## [3.11.1] – 2026-10-19

### Fixed
- Serial console: the firmware now echoes every received character itself (instead of the local echo
  of the monitor). This shows whether input really reaches the ESP32. A command is also executed after
  2 s without a line ending (monitors that send no Enter).

## [3.11] – 2026-10-19

### Added
- Display setting **"Show"**: rotate pages automatically or show one page permanently (web interface
  address, data rate or WiFi). With a fixed page a notice (e.g. no cable) still appears in alternation;
  the page dots in the header are hidden.
- Display setting **"Change every"**: 3, 5, 10, 20 or 30 seconds per page.

## [3.10.1] – 2026-10-19

### Fixed
- Serial console: typed commands were not visible and were interrupted by the status line.
  `platformio.ini` now enables `monitor_echo` (and LF line endings), the status line pauses while a
  command is being typed, and backspace works.

### Added
- `diag off|on|<seconds>`: switch the status line off or change its interval (saved).
- Hint at start: "'help' + Enter shows the commands".

## [3.10] – 2026-10-19

### Added
- **Brightness slider** for the display in the web interface (10–100 %). Changes are shown live while
  sliding and saved when the slider is released; "dim after 1 minute" uses a quarter of the set
  brightness.
  - OLED: via the contrast (pixel current) of the SSD1306/SSD1315
  - TFT: real PWM dimming if the module's BL pin is wired to TXD (GPIO17) and "Backlight on TXD" is
    enabled; otherwise via darker colours

## [3.9] – 2026-10-19

### Added
- **System status in the serial console**: command `status` (or `s`) prints uptime, reason of the last
  start (incl. brownout), chip, flash, firmware size, **CPU load per core** (bar chart, measured via the
  FreeRTOS run-time statistics) with the busiest tasks, RAM, settings memory, supply voltage, LAN/WiFi,
  web interface address and display. Printed automatically once 15 s after start.
- `tasks` (or `t`): all tasks with core, CPU share and free stack.
- **Optional measurement of the 5 V supply** via a voltage divider (2 × 10 kΩ) on IO39: `vmon on|off`,
  calibration with `vmon cal <volts>`; warning below 4.6 V.
- The status line every 10 s now also shows the CPU load per core (and the supply voltage, if measured).
- Warning at start when the last restart was caused by undervoltage (brownout).

## [3.8.1] – 2026-10-19

### Fixed
- TFT stayed **white after "rotate by 180°"** (and other TFT settings) until a power cycle. Rotation,
  offset and colours are now applied immediately without a restart (the display is re-initialized,
  including a hardware reset); only a change of the display type restarts the bridge.
- TFT/OLED could **freeze** on one page after interference on the wires: orientation, colour format
  and addressing are now sent again with every frame, and the TFT gets sleep-out/display-on every
  30 s. A black frame is written before the TFT is switched on.
- TFT SPI clock lowered from 16 to 8 MHz (more reliable with jumper wires).

## [3.8] – 2026-10-19

### Added
- **Colour TFT displays with ST7735** (SPI): 1.8″ 160×128, 1.44″ 128×128 and 0.96″ 160×80.
  Wiring: SCL/SCK = GPIO32, SDA/MOSI = GPIO33, DC = IO4, CS = IO14, RES = GPIO5, BL to 3V3.
  The same pages in colour (header in the colour of the operating mode, download blue, upload orange,
  warnings red); larger layout with bigger values and a full-width chart on 160×128 and 128×128.
  Frame buffer with a 16-colour palette (at most 10 KB RAM, only when a TFT is selected).
- Web interface "Display": **display type selection** (OLED, three TFT sizes, none) with the wiring for
  the selected type; for TFTs offset X/Y, swap red/blue and invert colours, with presets per size.

### Changed
- Display driver moved to `src/display.h` (common drawing code for OLED and TFT).

## [3.7] – 2026-10-19

### Added
- **Optional OLED display** (128×64, SSD1306 or SSD1315, I²C on SDA = GPIO33 "485_EN",
  SCL = GPIO32 "CFG", VCC 3V3). Detected automatically at start; own small driver and font,
  no external library (about 8 KB).
  - pages every 5 s: web interface address, data rate on the LAN port (large, with a chart), WiFi
    (signal in the client modes, number and names of the devices in the access point modes); an
    additional notice page when something is wrong
  - start screen with firmware version; display language follows the web interface
  - burn-in protection: content shifts by a few pixels every minute, dims after 1 minute and gets
    bright again on events (cable, WiFi, new devices) and when the web interface is opened
- Web interface section **"Display"**: detection status with wiring hint, on/off, dimming,
  rotate by 180°.
- `tools/gen_font.py` generates the display font (based on LCD_FONT from luma.core, MIT).

## [3.6.2] – 2026-10-08

### Fixed
- Firewall: when switching the firewall off, the whole section including the blocked packet counter
  and the "Save firewall" button was dimmed (an HTML element was not closed). Now only the options
  are dimmed; counter and button stay normal.

## [3.6.1] – 2026-10-07

### Fixed
- **Empty web interface page** after the update to 3.6: the start page (now over 50 KB) was assembled
  completely in RAM before sending; without a large enough free memory block the result was empty.
  The page is now sent in parts (chunked transfer) and each section is only generated right before
  it is sent.

## [3.6] – 2026-10-07

### Changed
- **New header in the web interface**:
  - illustration of the WT32-ETH01 with live status LEDs (LAN link green, traffic blinking yellow,
    WiFi blue)
  - **prominent operating mode banner** with pictogram and its own colour per mode (NAT blue,
    bridge green, access point NAT purple, access point bridge orange); a click opens the mode selection
  - **three status tiles**: LAN port/uplink, router WiFi or WiFi devices, and the current data rate,
    each with a coloured status dot
- **Collapsible settings**: router WiFi, operating mode, firewall, setup WiFi and web interface
  password are sections that can be opened and closed, each with a pictogram and a status badge
  (e.g. firewall "on", password "set"). The browser remembers which sections are open; links such as
  "Set a password" open the matching section.
- Access point modes: the warning links for an open WiFi now jump to the operating mode, where name and
  password are set.

## [3.5] – 2026-10-07

### Changed
- **Redesigned firewall section**: every option is a tile with a pictogram and a toggle switch;
  "Firewall enabled" is highlighted in green, and the other options are dimmed while the firewall is off.
- **Up to 32 custom rules** (previously 16). Only the existing rules are shown; further rules are
  added with "+ Add rule". Each rule can be moved up/down (the order matters) and deleted.
  "Block" is shown in red and "Allow" in green; disabled rules are dimmed.
- "Everything else" and the blocked packet counter are shown as separate tiles.
- Allowed WiFi devices: the MAC list has its own label with more spacing; the connected devices
  can be added with a tap.

## [3.4] – 2026-10-06

### Added
- **Devices in the home network** (access point bridge mode): a table with IP address, device name,
  manufacturer, MAC address and "last seen". The bridge detects the devices passively from their
  broadcasts on the LAN port (ARP, DHCP, multicast); devices appear as soon as they send something,
  names with their next address assignment. Up to 32 devices; the router is marked as such.

### Fixed
- Access point bridge: DHCP requests of devices in the home network no longer end up in the info
  table of the WiFi devices (could push out names and lease times of the WiFi devices).

## [3.3] – 2026-10-06

### Added
- **Live data rate on the LAN port** in the LAN card: current download and upload, a chart of the last
  2 minutes with a scaled axis (Mbit/s or kbit/s), peak values and data volume since start. Works in all four operating modes
  (download = towards the LAN device in the client modes, from the router in the access point modes).
  The firmware only adds a byte counter per frame; the rate is calculated once per second.

## [3.2] – 2026-10-06

### Added
- **Optional password protection for the web interface** (section "Password for this web interface").
  When a password is set, every page asks for it on a login page; the browser then stays logged in
  with a random session cookie (24 hours without use, "Log out" button). Only a salted SHA-256 hash
  of the password is stored. After 5 wrong attempts, login is blocked for one minute.
- In the access point modes a notice at the top of the page points out a missing password, because
  the web interface is also reachable from the home network there.

### Changed
- The **emergency reset** (power off/on 3 times) now also removes the web interface password.

## [3.1] – 2026-10-06

### Added
- **More information about connected devices** (WiFi devices in the access point modes, device on
  the LAN port in NAT and bridge mode):
  - **Device name** (e.g. "Thorstens-iPhone"), read from the device's DHCP request (option 12 or 81)
  - **Manufacturer** from the MAC prefix (built-in table of about 8,700 prefixes of common home
    network manufacturers); randomized addresses are shown as **"Private MAC"**
  - **Connected for** (WiFi devices), **signal bars** and **WiFi standard** (802.11b/g/n)
  - **Remaining DHCP lease time**: in NAT and AP-NAT from the bridge's own DHCP server, in the
    bridge modes from the router's DHCP acknowledgement
  - AP bridge: the IP address assigned by the router is now shown as well
- The firewall MAC list picker shows device names.
- `tools/gen_oui.py` regenerates the manufacturer table from the official IEEE list.

### Changed
- **Project name is now WT32-ETH01 NetBridge** (instead of "WT32 NetBridge"), so the project is
  easier to find when searching for the board. Web interface title, serial output, documentation,
  banner and preview image use the new name.
- **Setup WiFi is now called `WT32-ETH01-NetBridge-Setup`** and the hostname in the router is now
  `wt32-eth01-netbridge` (e.g. `http://wt32-eth01-netbridge.fritz.box`). If you never changed the
  WiFi name, the setup WiFi appears under the new name after the update; a name you set yourself is
  kept.
- Combined firmware file in releases: `wt32-eth01-netbridge-vX.Y-full.bin`.

### Fixed
- No more `nvs_get_blob ... NOT_FOUND` error messages at startup when no firewall rules are saved.

## [3.0] – 2026-10-06

### Changed
- **New project name: WT32 NetBridge** (formerly "WT32-ETH01 Ethernet-WLAN-Bridge"), because the
  firmware now works in both directions (WiFi client and access point). GitHub repository:
  `WT32-ETH01-NetBridge`.
- Web interface title, serial output and documentation use the new name.
- **Default name of the setup WiFi is now `WT32-NetBridge-Setup`** and the hostname in the router
  is now `wt32-netbridge` (e.g. `http://wt32-netbridge.fritz.box`). If you never changed the WiFi
  name, the setup WiFi appears under the new name after the update; a name you set yourself is kept.
- Combined firmware file in releases: `wt32-netbridge-vX.Y-full.bin`.

## [2.9.1] – 2026-10-06

### Changed
- In the access point modes the separate "WiFi (access point)" section at the bottom is gone; name
  and password are only shown (and changed) with the operating mode under "Access point WiFi". The
  warning links jump there. In the client modes the "Setup WiFi" section stays.

## [2.9] – 2026-10-06

### Added
- **Captive portal** for the setup WiFi: after connecting, phones and laptops open the setup page
  automatically (iOS sign-in window, Android "Sign in to network", Windows/macOS notification). The
  bridge answers all DNS queries in the setup WiFi with 192.168.4.1 and redirects the connectivity
  checks of the operating systems to the setup page.
- Only in the client modes (NAT, bridge); in the access point modes the WiFi is a normal network and
  DNS works as usual.
- Diagnostic line in the serial output every 10 s (mode, router connection and reconnect attempts,
  connected WiFi devices, channel, free memory).

## [2.8] – 2026-10-06

### Added
- Option **"Open WiFi without password"** as a separate checkbox, both in the WiFi settings and
  directly with the access point modes. When ticked, the password fields are hidden and a red
  warning explains the risks (anyone can connect, unencrypted, home network access in bridge mode)
  with a tip to enable the firewall.
- While the WiFi is open on purpose, a permanent (non-pulsing) red notice stays at the top of the
  web interface.

### Changed
- Access point modes can now also run with an open WiFi, but only if it was chosen explicitly.
  Setting a password later turns the open WiFi off again.

## [2.7] – 2026-10-06

### Added
- WiFi name and password of the access point can be entered directly when selecting an access
  point mode (fields appear below the cards); no separate step needed anymore.
- **Web interface in access point (bridge) mode**: the bridge now gets its own address from the
  router via DHCP and stays reachable at that address – from the home network and from its WiFi
  (e.g. `http://wt32-bridge.fritz.box`). Frames for the bridge itself are passed to its own TCP/IP
  stack, all other frames are still forwarded.
- Hostname `wt32-bridge` in the router (client modes and access point modes).

### Changed
- The warning for access point (bridge) mode now explains where to find the web interface instead
  of saying it is no longer reachable.

## [2.6.1] – 2026-10-06

### Fixed
- The setup WiFi was almost unreachable (packet loss, web interface did not load) while the router
  WiFi could not be found: the bridge retried the connection non-stop, and every attempt scans all
  WiFi channels with the same radio. Connection attempts are now spaced out (10 s, 20 s, 40 s … up to
  120 s) and, once the router is known, only search its channel; every third attempt does a full scan
  in case the router changed its channel.

### Added
- The web interface shows why the router connection failed (not found / wrong password) and when
  the next attempt follows.

## [2.6] – 2026-10-05

### Added
- **Simple firewall** (section "Firewall" in the web interface, applies immediately):
  - Switches: "Internet only, no home network", "Block the bridge web interface", "Isolate WiFi
    devices" (access point modes).
  - Up to 16 custom rules: block/allow, protocol, target IP/network, port/port range, hit counter.
  - Default action for everything else: allow or block.
  - MAC allow list for the access point modes; prevents you from locking yourself out.
  - IPv6 is blocked while the firewall is enabled; counter of blocked packets.
- Router and DNS are learned from DHCP in the bridge modes so they stay reachable with "Internet
  only".

## [2.5] – 2026-10-05

### Added
- **Access point – own WiFi network (NAT)**: LAN port as uplink to the router (DHCP client), own
  WiFi with DHCP and NAT for up to approx. 8 devices, the router's DNS server is passed on. The web
  interface stays reachable at `192.168.4.1` and via the bridge IP in the home network.
- **Access point – WiFi straight into your home network (bridge)**: layer-2 forwarding between
  Ethernet and the access point (like Espressif's `eth2ap`); WiFi devices get their addresses from
  the router. Prominent warning that the web interface will no longer be reachable, switching
  requires an explicit confirmation.
- **Emergency reset**: switching the power off and on 3 times in a row (each within 10 s) resets the
  operating mode to NAT.
- The name of the bridge's WiFi can be changed in the web interface ("WiFi name").
- Web interface in the access point modes: uplink info (bridge IP in the home network, gateway) and a
  list of connected WiFi devices (IP, MAC, signal).

### Changed
- Access point modes can only be selected once a WiFi password is set.
- When changing the setup WiFi settings, the password can be left empty to keep it.

## [2.4] – 2026-09-30

### Added
- Complete web interface in **English** in addition to German, including all result and error
  pages and the texts generated by JavaScript.
- Language switch with small flag buttons (DE / EN) at the top right. The choice is stored in the
  browser for one year (cookie); without a choice the browser language is used.
- `tools/release.sh`: builds the firmware and creates a GitHub release with the firmware files
  (`bootloader.bin`, `partitions.bin`, `boot_app0.bin`, `firmware.bin`, a combined `-full.bin` if
  esptool is available, and `SHA256SUMS.txt`) and bilingual release notes from the changelogs.

### Changed
- `/status` additionally reports whether the setup WiFi is open (`apOpen`).

## [2.3] – 2026-09-30

### Changed
- The setup WiFi now starts **without a password** (open) until a password is set in the web
  interface. `SETUP_AP_PASSWORD` in `src/main.cpp` is empty by default; builders can still enter a
  fixed start password there.
- The button in the "Einrichtungs-WLAN" section reads "Passwort festlegen" (set password) while no
  password is set.

### Added
- Prominent, pulsing red warning at the top of the web interface while the setup WiFi is open,
  with a link to the password form. The form is highlighted in red as well.
- Warning in the serial output when the setup WiFi is open.

## [2.2] – 2026-09-30

### Added
- Setup WiFi password can be changed in the web interface (section "Einrichtungs-WLAN"). It is
  stored in flash and survives firmware updates.
- Warning in the web interface and in the serial output while the default setup WiFi password is
  still active.
- Password rules are checked: 8 to 63 printable ASCII characters, entered twice.

### Changed
- `SETUP_AP_PASSWORD` in `src/main.cpp` is now only the default, used until a custom password is
  set or after erasing the flash.

## [2.1] – 2026-09-30

### Added
- **Bridge** operating mode: the LAN device gets its IP address directly from the router (MAC
  translation, one device, IPv4 only). Selectable in the web interface, in addition to NAT.
- Operating modes shown as cards with pictograms and data rate.
- Info about the device on the LAN port: IP, MAC, link speed/duplex, connection time, packet
  counters in bridge mode.
- Version number in the web interface, the serial output and `/status`.
- Project description and tutorial in English and German, MIT license.

### Fixed
- WiFi scan now works while there is no router connection yet (connection attempts are paused
  during the scan; the scan runs asynchronously).
- DHCP server on the Ethernet port did not start (missing `ESP_NETIF_FLAG_AUTOUP`).
- DNS server was not passed to DHCP clients correctly.
- LAN8720 oscillator (GPIO16) is enabled before the EMAC starts.

## [2.0] and earlier

NAT mode only, not documented in detail. The last NAT-only version is kept in
`backup/main_nat_only.cpp`.
