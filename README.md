# WT32-ETH01 NetBridge – Ethernet ⇄ WiFi Bridge & Access Point (ESP32 + LAN8720)

🇬🇧 **English** | 🇩🇪 [Deutsch](README.de.md)

[![Version](https://img.shields.io/badge/Version-3.14-1263a6)](CHANGELOG.md)
[![Platform](https://img.shields.io/badge/ESP32-WT32--ETH01-green)](#hardware)
[![PlatformIO](https://img.shields.io/badge/PlatformIO-Arduino%20Core%203.x-orange?logo=platformio)](#build-and-flash)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow)](LICENSE)
[![Buy me a coffee](https://img.shields.io/badge/Buy%20me%20a%20coffee-FFDD00?logo=buymeacoffee&logoColor=black)](https://buymeacoffee.com/sykh)

**Version 3.14** – see the [changelog](CHANGELOG.md)

**WT32-ETH01 NetBridge** is a firmware for the **WT32-ETH01 v1.4** (ESP32 + LAN8720) that connects
Ethernet and WiFi in both directions:

- **WiFi client:** brings a device with an Ethernet port into your WiFi – a **WiFi adapter for
  devices without WiFi** such as printers, smart TVs, game consoles, NAS, PLCs or measuring
  instruments.
- **Access point:** connected to your router by cable, it creates its **own WiFi** – as a small
  access point, guest WiFi or WiFi extension, optionally with a firewall.
- **Optional display:** OLED or colour TFT showing address, data rate, devices, CPU/RAM, a WiFi QR
  code for guests and the time.

<p align="center">
  <img src="docs/wt32-eth01-netbridge-banner.png" alt="WT32-ETH01 NetBridge – Ethernet ⇄ WiFi for WT32-ETH01: WiFi client or access point, NAT or bridge, firewall" width="760">
</p>

```
WiFi client:   [ LAN device ] ──cable── [ WT32-ETH01 NetBridge ] ))) WiFi ))) [ Router ] ── Internet
Access point:  [ phone, laptop ] ))) WiFi ))) [ WT32-ETH01 NetBridge ] ──cable── [ Router ] ── Internet
```

## Web interface

<p align="center">
  <img src="docs/screenshots/en/07-status-access-point.png" alt="Web interface in access point bridge mode: board with status LEDs, operating mode, status tiles, data rate chart, WiFi devices and devices in the home network" width="460">
</p>

The web interface (version 3.14, access point bridge mode): board illustration with live status LEDs,
the current operating mode, status tiles, data rate with a 2-minute chart, the connected WiFi devices
with name, manufacturer, signal and lease, and the devices in the home network. The settings below
are collapsible sections. The interface is available in English and German and can be switched with
the flag buttons at the top right.

## Screenshots

All screenshots with descriptions: **[screenshot album](docs/SCREENSHOTS.md)**.

<table>
<tr><td align='center' width='33%'><a href='docs/SCREENSHOTS.md'><img src='docs/screenshots/en/01-status-nat.png' width='240' alt='Status in NAT mode'></a><br><sub>Status in NAT mode</sub></td><td align='center' width='33%'><a href='docs/SCREENSHOTS.md'><img src='docs/screenshots/en/04-operating-modes.png' width='240' alt='Operating modes'></a><br><sub>Operating modes</sub></td><td align='center' width='33%'><a href='docs/SCREENSHOTS.md'><img src='docs/screenshots/en/06-firewall.png' width='240' alt='Firewall'></a><br><sub>Firewall</sub></td></tr>
<tr><td align='center' width='33%'><a href='docs/SCREENSHOTS.md'><img src='docs/screenshots/en/05-access-point-setup.png' width='240' alt='Access point setup'></a><br><sub>Access point setup</sub></td><td align='center' width='33%'><a href='docs/SCREENSHOTS.md'><img src='docs/screenshots/en/07-status-access-point.png' width='240' alt='Status in access point mode'></a><br><sub>Status in access point mode</sub></td><td align='center' width='33%'><a href='docs/SCREENSHOTS.md'><img src='docs/screenshots/en/08-open-wifi.png' width='240' alt='Open WiFi (on purpose)'></a><br><sub>Open WiFi (on purpose)</sub></td></tr>
<tr><td align='center' width='33%'><a href='docs/SCREENSHOTS.md'><img src='docs/screenshots/en/12-display.png' width='240' alt='Display settings'></a><br><sub>Display settings with live preview</sub></td><td align='center' colspan='2'><a href='docs/SCREENSHOTS.md'><img src='docs/screenshots/en/13-display-pages.png' width='480' alt='Display pages'></a><br><sub>Display pages (TFT and OLED)</sub></td></tr>
</table>

## Features

**Operating modes** (selectable in the web interface)

- **NAT – own network** (default): separate subnet `192.168.50.0/24` on the LAN port with a DHCP
  server, internet access through NAPT. Several devices possible (e.g. via a switch). Up to approx. 10 Mbit/s.
- **Bridge – straight into your home network** (experimental): the LAN device gets its IP address
  **directly from your router**. One device only, IPv4 only. Above 30 Mbit/s.
- **Access point – own WiFi network (NAT)**: the LAN port goes to your router, the bridge creates its own
  WiFi (`192.168.4.x`) for up to approx. 8 devices. The web interface stays reachable.
- **Access point – WiFi straight into your home network (bridge)**: WiFi devices get their IP directly
  from your router; the web interface is reachable via the bridge IP (e.g. `http://wt32-eth01-netbridge.fritz.box`).

**Web interface** (English and German, switchable via flag buttons)

- Header with board illustration and live status LEDs, a coloured **operating mode banner** and status
  tiles (LAN, WiFi, data rate); settings as collapsible sections with status badges
- **Live data rate** on the LAN port with a 2-minute chart, peak values and data volume
- **Device overview**: WiFi devices and the LAN device with name (from DHCP), manufacturer (or "private
  MAC"), IP, signal, WiFi standard, connection time and remaining DHCP lease time
- **Home network devices** (access point bridge): IP, name, manufacturer, MAC and "last seen" of the
  devices in the home network, detected passively from their broadcasts
- WiFi scan, router credentials, setup WiFi password, captive portal in the setup WiFi

**Security**

- **Firewall** for the connected devices: "Internet only, no home network", block the web interface,
  isolate WiFi devices, up to **32 custom rules** (add, move, delete) with hit counters, MAC allow list
- **Password protection for the web interface** (optional, salted hash, session cookie, lockout after
  failed attempts)
- **Emergency reset**: power off/on 3 times in a row returns to NAT mode and removes the web password

**Optional display** (OLED SSD1306/SSD1315 via I²C or colour TFT ST7735 via SPI)

- Pages: web interface address, data rate with chart, WiFi, **system** (CPU, RAM), **today** (data
  volume, peaks) and a **WiFi QR code** for guests; rotating or one fixed page
- **Event notices** (new device, cable, router WiFi), **clock** via NTP, **night mode**, brightness
  slider, burn-in protection, optional **button** on IO36
- **Live preview** of the display in the web interface

**Diagnostics**

- **Serial console** with commands: system status with **CPU load per core**, RAM, firmware size, reason
  of the last start (incl. brownout), optional **5 V supply measurement**, task list
- Settings stored permanently in flash (NVS); no external libraries required

## Hardware

- WT32-ETH01 v1.4 by Wireless-Tag: ESP32 module, LAN8720 Ethernet PHY, RJ45 jack (10/100 Mbit/s),
  powered by 5 V **or** 3.3 V
- USB-to-serial adapter with **3.3 V** logic for flashing (e.g. CP2102 or CH340)

| Adapter | WT32-ETH01 |
|---------|------------|
| TX      | RX0 (GPIO3) |
| RX      | TX0 (GPIO1) |
| GND     | GND |
| 5V      | 5V (or 3.3 V to 3V3, never both) |

To flash, **connect IO0 to GND** and then power up the board. Remove the jumper afterwards and
restart.

### Header pinout

According to the [Wireless-Tag wiki](https://wiki.wireless-tag.com/docs/en/WT32-ETH01/board_features.html):

| Header 1 | Function | Header 2 | Function |
|----------|----------|----------|----------|
| EN | Enable (active high) | GND | Ground |
| CFG | IO32 (factory reset) | IO39 | input only |
| 485_EN | IO33 (RS485 enable) | IO36 | input only |
| RXD | IO5 (UART2 RX) | IO15 | GPIO |
| TXD | IO17 (UART2 TX) | IO14 | GPIO |
| GND | Ground | IO12 | GPIO |
| 3V3 | 3.3 V in/out | IO35 | input only |
| GND | Ground | IO4 | GPIO |
| 5V | 5 V in/out | IO2 | GPIO |
| LINK | Link LED | GND | Ground |

Flashing uses the programming pins **TXD0 (IO1), RXD0 (IO3), GND, 3V3, EN and IO0**.

Pins used internally: GPIO16 (LAN8720 oscillator enable), GPIO23 (MDC), GPIO18 (MDIO),
GPIO0 (50 MHz RMII clock input), PHY address 1.

### Optional: display

The display type is selected in the web interface under "Display" (the bridge restarts after a change).

**OLED 128×64, SSD1306 or SSD1315** (4-pin I²C module, default). It is detected automatically at start;
without a display nothing changes.

| Display | WT32-ETH01 |
|---------|------------|
| GND | GND |
| VCC | 3V3 |
| SCL | CFG (GPIO32) |
| SDA | 485_EN (GPIO33) |

The display alternates (every 3–30 seconds, adjustable) between the web interface address, the data rate on the LAN
port (with a 1-minute chart) the WiFi status (signal or connected devices) and the
system (CPU load per core, RAM, uptime, supply voltage if measured); if something is wrong
(no cable, router WiFi down, no IP) a notice page is added. Alternatively one page can be shown
permanently (setting "Page"); a notice then still appears in alternation. Events (new WiFi device,
cable plugged/unplugged, router WiFi) are shown for 4 seconds, and the web interface has a **live preview**
of the display.

More display features (web interface → Display):

- **WiFi QR code**: guests scan it with the phone camera and are connected (optional page; anyone who
  can see the display can join).
- **Today**: data volume down/up, peak rates and max. WiFi devices since midnight (or since start).
- **Clock** in the header (time via the internet/NTP, selectable time zone).
- **Night mode** (optional): display off or dimmed between two times, e.g. 22:00–07:00.
- **Button** (optional): between **IO36** and **GND** plus 10 kΩ from IO36 to 3V3. Short press: next page
  for 30 s and lift night mode briefly; long press (1 s): display on/off. Burn-in protection: the content shifts by
a few pixels every minute and the display dims after 1 minute (it gets bright again on events and when
the web interface is opened). On, dimming and 180° rotation can be set in the web interface under
"Display". Do not use GPIO2, 12 or 15 for the display – they are boot strapping pins.

**Colour TFT with ST7735** – 1.8″ 160×128, 1.44″ 128×128 or 0.96″ 160×80. The ST7735 has **no I²C**:
the pins labelled SCL/SDA on these modules are SPI clock and data.

| TFT module | WT32-ETH01 |
|------------|------------|
| GND / VCC | GND / 3V3 |
| BL (LED) | leave open (backlight on), or TXD (GPIO17) for real dimming |
| SCL (SCK) | CFG (GPIO32) |
| SDA (MOSI) | 485_EN (GPIO33) |
| DC (A0) | IO4 |
| CS | IO14 |
| RES (RST) | RXD (GPIO5) |

The TFT shows the same pages in colour (header in the colour of the operating mode, download blue,
upload orange, warnings red); 160×128 and 128×128 use a larger layout. If the picture is shifted or the
colours are wrong, adjust offset X/Y, "swap red and blue" or "invert colours" in the web interface
(presets per module size are filled in automatically). The brightness slider dims via darker colours while
BL is open (most modules switch the backlight on by themselves); with BL on TXD (GPIO17) and "Backlight on TXD" enabled it controls the
backlight itself (PWM).

## Build and flash

A detailed step-by-step guide (including a way without the command line) is in the
**[tutorial](docs/TUTORIAL.en.md)**.

Short version with [PlatformIO](https://platformio.org/):

```bash
pio run -t upload        # build and flash
pio device monitor       # serial output (115200 baud)
```

The firmware requires **Arduino Core 3.x (ESP-IDF 5)**. `platformio.ini` uses the
[pioarduino](https://github.com/pioarduino/platform-espressif32) platform for this. With the stock
`platform = espressif32` package (Arduino Core 2.x) the Ethernet part does not compile.

## Setup

1. Connect to the WiFi **`WT32-ETH01-NetBridge-Setup`**. On first start it is **open (no password)**.
2. The setup page opens automatically (captive portal, like in hotel WiFis). If not, open
   `http://192.168.4.1`.
3. Select your router's WiFi, enter the password, save.
4. Set a password for the setup WiFi (the web interface shows a red warning until you do).
5. Connect the device to the LAN port.

## WiFi passwords

The bridge uses two passwords:

| Password | Where is it stored? | How to change it? |
|----------|---------------------|-------------------|
| **Setup WiFi** `WT32-ETH01-NetBridge-Setup` (initially **open**, no password) | in the ESP32's flash (NVS); an optional default can be set in `SETUP_AP_PASSWORD` in `src/main.cpp` | in the web interface under "Setup WiFi" (8–63 characters), the bridge restarts |
| **Router WiFi** | in the ESP32's flash (NVS), not in the code | enter it again in the web interface under "Router WiFi" and click "Save and connect" |

> **Open WiFi on purpose:** in the WiFi settings (and with the access point modes) you can tick
> "Open WiFi without password", e.g. for a guest WiFi. A red warning explains the risks and stays
> visible at the top of the web interface; combine it with the firewall ("Internet only", "Block
> the web interface").

> **Important:** on first start the setup WiFi is open, so anyone in range could change the
> settings. The web interface shows a prominent red warning until you set a password; do this
> right after setup. Details: [tutorial, WiFi passwords](docs/TUTORIAL.en.md#7-find-and-change-the-wifi-passwords).

## Operating modes in detail

### NAT – own network

The bridge creates its own network on the LAN port and assigns the addresses itself. Ideal if you
want to connect several devices through a switch. Data rates up to approx. 10 Mbit/s.

- Bridge address on the LAN: `192.168.50.1` (gateway)
- DHCP range: `192.168.50.x`, DNS: `1.1.1.1`
- Internet traffic is routed over WiFi using NAPT.

### Bridge – straight into your home network (experimental)

The connected device gets its IP address directly from your router and is reachable in your home
network like any other device. Data rates above 30 Mbit/s.

A WiFi client may only send frames with its own MAC address. The bridge therefore rewrites the MAC
addresses in the frames, including DHCP and ARP packets. Espressif's
[`sta2eth`](https://github.com/espressif/esp-idf/tree/master/examples/network/sta2eth) example uses
the same technique.

Limitations:

- **one** device on the LAN port, **IPv4** only
- the router sees the device with the **WiFi MAC address of the bridge** (shown in the web
  interface). Static IP reservations in the router must use this MAC.
- the bridge has no IP address of its own in the router's network; the web interface is only
  reachable through the setup WiFi
- uses internal ESP-IDF WiFi functions (`esp_wifi_internal_*`), which may change with core updates
- after switching the operating mode, unplug the cable of the LAN device briefly so it requests a
  new address

### Access point – own WiFi network (NAT)

Connect the LAN port to your router. The bridge gets an address from the router via DHCP and creates
its own WiFi (name and password from the "Setup WiFi" section). WiFi devices get addresses in the
`192.168.4.x` network; DNS is the router's DNS server. The web interface stays reachable at
`192.168.4.1` and via the bridge's IP in the home network.

- up to approx. 8 WiFi devices, 2.4 GHz only, the data rate is shared by all devices
- a WiFi password must be set before this mode can be selected

### Access point – WiFi straight into your home network (bridge)

Connect the LAN port to your router. Frames are passed between Ethernet and the WiFi access point
on layer 2 (like Espressif's
[`eth2ap`](https://github.com/espressif/esp-idf/tree/master/examples/network/eth2ap) example). WiFi
devices get their addresses directly from your router and are visible in the home network
(AirPlay, Chromecast, printers).

> **Note:** in this mode the web interface is **no longer at 192.168.4.1**. The bridge gets its own
> address from your router via DHCP (hostname `wt32-eth01-netbridge`) and the web interface is reachable at
> that address – from the home network and from the bridge's WiFi, e.g. `http://wt32-eth01-netbridge.fritz.box`
> or the IP shown in your router's device list. The web interface asks for a confirmation before
> switching.
>
> **Back to setup (emergency reset):** switch the power off and on again **3 times in a row**, each
> time within 10 seconds. The bridge then starts in NAT mode with the setup WiFi and the web interface
> password is removed; all other settings are kept.

## Firewall

Section "Firewall" in the web interface. It checks the traffic of the **connected devices** (device
on the LAN port, or the WiFi devices in the access point modes). Changes apply immediately, no restart
needed.

| Option | Effect |
|--------|--------|
| Internet only, no home network | blocks private addresses (10.x, 172.16–31.x, 192.168.x, multicast); DHCP, DNS and ping to the router stay allowed. In the bridge modes it also blocks connections from the home network to the device. |
| Block the bridge web interface | connected devices cannot open the settings page |
| Isolate WiFi devices (AP modes) | WiFi devices of the bridge cannot reach each other |
| Custom rules (up to 32) | block/allow, protocol (all/TCP/UDP/ICMP), target IP or network, port or port range; first match wins; hit counter per rule |
| Everything else | allow (default) or block |
| MAC filter (AP modes) | only listed WiFi devices may connect; protects you from locking yourself out |

Limitations: stateless (no connection tracking; NAT already blocks unsolicited inbound traffic in
the NAT modes), IPv4 only (IPv6 is blocked while the firewall is enabled), IP addresses only, no
domain names.

## Project structure

```
platformio.ini            build configuration
src/main.cpp              firmware
src/oui_table.h           manufacturer table (MAC prefixes), generated by tools/gen_oui.py
src/display.h             drivers for the optional OLED and TFT displays
src/qr.h                  small QR code generator (WiFi QR code on the display)
src/oled_font.h           display font, generated by tools/gen_font.py
docs/TUTORIAL.en.md       tutorial: build and flash (English)
docs/TUTORIAL.de.md       Tutorial: Kompilieren und Flashen (Deutsch)
docs/webinterface-v2.4.png screenshot of the web interface (real device)
docs/SCREENSHOTS.md       screenshot album (EN), docs/screenshots/en|de/
docs/wt32-eth01-netbridge-banner.png banner on the start page
tools/make_preview.py     regenerates banner and social preview
docs/wt32-eth01.svg       board illustration
docs/social-preview.png   GitHub preview image
tools/release.sh          builds the firmware and creates a GitHub release
tools/gen_oui.py          regenerates src/oui_table.h from the IEEE OUI list
tools/gen_font.py         regenerates src/oled_font.h (display font)
backup/main_nat_only.cpp  older version with NAT mode only
CHANGELOG.md              changelog (English)
CHANGELOG.de.md           Änderungsprotokoll (Deutsch)
LICENSE                   MIT license
```

## Versions

Current version: **3.14** – display: WiFi QR code, "today" statistics, clock, night mode, button.
Ready-made firmware files are attached to each [release](../../releases).
All changes are listed in the **[changelog](CHANGELOG.md)**.

## Support

If this project helps you, I'd be happy about a coffee:

<a href="https://buymeacoffee.com/sykh"><img src="https://img.shields.io/badge/Buy%20me%20a%20coffee-FFDD00?style=for-the-badge&logo=buymeacoffee&logoColor=black" alt="Buy me a coffee"></a>

## Serial console and system status

In the serial monitor (`pio device monitor`, 115200 baud) the bridge prints a status line every
10 seconds (mode, WiFi, **CPU load per core**, free RAM) and, 15 seconds after start, a complete
system status. Commands (type and press Enter). The firmware confirms every recognized command with a line `> command`; if
that line does not appear, the input does not reach the ESP32 (check the wire adapter TX → RX0/IO3).
In the Arduino IDE use the input field at the top:

| Command | Function |
|---------|----------|
| `status` or `s` | system status: uptime, reason of the last start (incl. brownout), chip, flash, firmware size, CPU load per core with the busiest tasks, RAM, settings memory, supply voltage, LAN/WiFi, web interface address, display |
| `tasks` or `t` | all tasks with core, CPU share and free stack |
| `vmon on` / `vmon off` | measure the 5 V supply (needs a voltage divider, see below) |
| `vmon cal 5.02` | calibrate with the voltage measured by a multimeter |
| `diag off` / `diag on` / `diag 60` | status line off, on (every 10 s) or every 60 s |
| `reboot` | restart |

**Supply voltage:** the ESP32 cannot measure its own supply voltage. For a measurement connect a
voltage divider of two equal resistors (e.g. 2 × 10 kΩ) from **5V to GND** and its middle to **IO39**
(input only, free). Then enter `vmon on` once. Below 4.6 V the status shows a warning. The reason of the
last start shows whether the bridge was restarted by undervoltage (brownout).

```
═══ WT32-ETH01 NetBridge 3.9 · Systemstatus ═══
 CPU
   Kern 0 ........... [██░░░░░░░░░░░░░░░░░░]  10 %
   Kern 1 ........... [█░░░░░░░░░░░░░░░░░░░]   4 %
   Top-Tasks ........ wifi 3.0 %, eth2ap 2.2 %, loopTask 1.2 %, tiT 0.8 %
 Versorgung
   5V-Eingang ....... 5.02 V   ok
   Brownout ......... nein
```

## Troubleshooting

The serial output (115200 baud) shows the current state, for example:

```
WT32-ETH01 NetBridge, Firmware 3.1
Betriebsart: NAT
DHCP server started on interface ETH_LAN with IP: 192.168.50.1
Ethernet-LAN: 192.168.50.1, DHCP-Server laeuft
Ethernet-Kabel verbunden (100 Mbit/s, Vollduplex)
LAN-Geraet AA:BB:CC:DD:EE:FF hat 192.168.50.2 bekommen
```

The log messages are in German. More hints are in the
[tutorial](docs/TUTORIAL.en.md#8-troubleshooting).

## License

Released under the [MIT license](LICENSE): you may use, modify and distribute the code freely,
including commercially, as long as the copyright notice is kept.
