# Changelog

🇬🇧 **English** | 🇩🇪 [Deutsch](CHANGELOG.de.md)

All notable changes to this project are documented in this file.

Version numbers follow the scheme **MAJOR.MINOR.PATCH**:

- **MAJOR**: incompatible changes (e.g. settings have to be entered again)
- **MINOR**: new features
- **PATCH**: bug fixes only (written as e.g. `2.2.1`; a missing patch number means `.0`)

The current version is defined in `src/main.cpp` (`FIRMWARE_VERSION`) and shown in the web
interface and the serial output.

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
