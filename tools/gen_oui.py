#!/usr/bin/env python3
"""Erzeugt src/oui_table.h (Hersteller-Erkennung anhand der MAC-Adresse).

Quelle ist die offizielle IEEE-Liste der MAC-Praefixe (MA-L), wahlweise als
  oui.txt  https://standards-oui.ieee.org/oui/oui.txt
  oui.csv  https://standards-oui.ieee.org/oui/oui.csv

Damit die Firmware klein bleibt, landen nur Hersteller in der Tabelle, die im
Heimnetz typisch sind (Liste VENDORS unten). Pro Praefix werden 4 Bytes Flash
belegt (24 Bit Praefix + 8 Bit Herstellernummer).

Aufruf:  python3 tools/gen_oui.py pfad/zu/oui.txt
"""
import csv
import datetime
import os
import re
import sys

# (Anzeigename, regulaerer Ausdruck auf den IEEE-Firmennamen)
VENDORS = [
    ("Apple", r"^Apple,? Inc"),
    ("Samsung", r"^Samsung Electronics|^SAMSUNG ELECTRO"),
    ("Google", r"^Google,? Inc|^Google LLC|^Nest Labs"),
    ("Amazon", r"^Amazon Technologies|^Amazon\.com"),
    ("Espressif", r"^Espressif"),
    ("Raspberry Pi", r"^Raspberry Pi"),
    ("AVM", r"^AVM"),
    ("Intel", r"^Intel Corporat"),
    ("Realtek", r"^Realtek"),
    ("Xiaomi", r"^Xiaomi|^Beijing Xiaomi|^Yeelink|^Lumi United"),
    ("Huawei", r"^Huawei|^HUAWEI"),
    ("Honor", r"^Honor Device"),
    ("Sony", r"^Sony "),
    ("LG", r"^LG Electronics|^LG Innotek"),
    ("TP-Link", r"^TP-LINK|^TP-Link"),
    ("Microsoft", r"^Microsoft"),
    ("Dell", r"^Dell "),
    ("HP", r"^Hewlett Packard|^HP Inc|^Hewlett-Packard"),
    ("Lenovo", r"^Lenovo|^LCFC"),
    ("ASUS", r"^ASUSTek"),
    ("Nintendo", r"^Nintendo"),
    ("Sonos", r"^Sonos"),
    ("Philips Hue", r"^Signify|^Philips Lighting"),
    ("Shelly", r"^Allterco|^Shelly"),
    ("Tuya", r"^Tuya|^Hangzhou Tuya"),
    ("OnePlus", r"^OnePlus"),
    ("OPPO", r"^GUANGDONG OPPO|^OPPO"),
    ("vivo", r"^vivo Mobile"),
    ("Motorola", r"^Motorola Mobility"),
    ("Nokia", r"^Nokia Corporation|^HMD Global"),
    ("Fairphone", r"^Fairphone"),
    ("Murata", r"^Murata"),
    ("Lite-On", r"^Liteon|^LITE-ON"),
    ("AzureWave", r"^AzureWave"),
    ("Foxconn", r"^Hon Hai|^HON HAI"),
    ("Wistron", r"^Wistron"),
    ("Compal", r"^Compal"),
    ("Quanta", r"^Quanta"),
    ("Pegatron", r"^PEGATRON|^Pegatron"),
    ("USI", r"^Universal Global Scientific"),
    ("Quectel", r"^Quectel"),
    ("Ubiquiti", r"^Ubiquiti"),
    ("Netgear", r"^NETGEAR|^Netgear"),
    ("Zyxel", r"^Zyxel|^ZyXEL"),
    ("D-Link", r"^D-Link|^D-LINK"),
    ("devolo", r"^devolo"),
    ("Sagemcom", r"^Sagemcom"),
    ("Technicolor", r"^Technicolor"),
    ("Vodafone", r"^Vodafone"),
    ("Telekom", r"^Deutsche Telekom"),
    ("Gigaset", r"^Gigaset"),
    ("Synology", r"^Synology"),
    ("QNAP", r"^QNAP"),
    ("Brother", r"^Brother Industries"),
    ("Canon", r"^Canon "),
    ("Epson", r"^Seiko Epson"),
    ("Bosch", r"^Robert Bosch|^BSH Hausger"),
    ("Siemens", r"^Siemens"),
    ("Miele", r"^Miele"),
    ("Panasonic", r"^Panasonic"),
    ("Sharp", r"^Sharp Corp"),
    ("Hisense", r"^Hisense"),
    ("TCL", r"^TCL "),
    ("Vestel", r"^Vestel"),
    ("Grundig", r"^Grundig"),
    ("Loewe", r"^Loewe"),
    ("Roku", r"^Roku"),
    ("Meta", r"^Meta Platforms|^Facebook Tech|^Oculus"),
    ("Valve", r"^Valve"),
    ("Garmin", r"^Garmin"),
    ("Fitbit", r"^Fitbit"),
    ("Withings", r"^Withings"),
    ("Ring", r"^Ring LLC"),
    ("Bose", r"^Bose"),
    ("Denon/Marantz", r"^D&M Holdings"),
    ("Yamaha", r"^Yamaha"),
    ("Dyson", r"^Dyson"),
    ("iRobot", r"^iRobot"),
    ("Roborock", r"^Beijing Roborock|^Roborock"),
    ("Ecovacs", r"^Ecovacs"),
    ("Netatmo", r"^Netatmo"),
    ("tado", r"^tado"),
    ("eQ-3", r"^eQ-3"),
    ("Hikvision", r"^Hangzhou Hikvision"),
    ("Dahua", r"^Zhejiang Dahua"),
    ("Reolink", r"^Shenzhen Reolink"),
    ("Wyze", r"^Wyze"),
    ("Gree", r"^GREE ELECTRIC"),
    ("Texas Instruments", r"^Texas Instruments"),
    ("Silicon Labs", r"^Silicon Lab"),
    ("Nordic", r"^Nordic Semi"),
    ("Microchip", r"^Microchip"),
    ("Arduino", r"^Arduino"),
    ("MediaTek", r"^MediaTek"),
    ("Qualcomm", r"^Qualcomm"),
    ("Broadcom", r"^Broadcom"),
    ("Marvell", r"^Marvell"),
    ("Nvidia", r"^NVIDIA"),
    ("Gigabyte", r"^GIGA-BYTE"),
    ("MSI", r"^Micro-Star"),
    ("ASRock", r"^ASRock"),
    ("Logitech", r"^Logitech"),
    ("Corsair", r"^Elgato|^Corsair"),
    ("Tesla", r"^Tesla"),
    ("BMW", r"^BMW"),
    ("Volkswagen", r"^Volkswagen"),
]


def read_entries(path):
    """Liefert (praefix, firmenname) fuer jeden MA-L-Eintrag."""
    if path.lower().endswith(".csv"):
        with open(path, newline="", encoding="utf-8", errors="replace") as handle:
            for row in csv.DictReader(handle):
                if row.get("Registry", "MA-L") == "MA-L":
                    yield int(row["Assignment"], 16), row["Organization Name"].strip()
        return
    pattern = re.compile(r"^([0-9A-Fa-f]{6})\s+\(base 16\)\s+(.*)$")
    with open(path, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            match = pattern.match(line.strip())
            if match:
                yield int(match.group(1), 16), match.group(2).strip()


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    rules = [(name, re.compile(rx, re.I)) for name, rx in VENDORS]
    names = [name for name, _ in VENDORS]
    if len(names) > 255:
        sys.exit("Hoechstens 255 Hersteller moeglich")
    table = {}
    for prefix, organisation in read_entries(sys.argv[1]):
        for index, (_, rx) in enumerate(rules):
            if rx.search(organisation):
                table[prefix] = index
                break
    if not table:
        sys.exit("Keine Eintraege gefunden - richtige Datei?")

    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "oui_table.h")
    with open(out, "w", encoding="ascii") as handle:
        handle.write("// Automatisch erzeugt von tools/gen_oui.py aus der IEEE-OUI-Liste (%s). Nicht von Hand aendern.\n"
                     % datetime.date.today().isoformat())
        handle.write("// %d MAC-Praefixe von %d Herstellern. Eintrag = (Praefix << 8) | Herstellernummer, aufsteigend sortiert.\n"
                     % (len(table), len(names)))
        handle.write("#pragma once\n#include <stdint.h>\n\n")
        handle.write("static const char *const OUI_VENDORS[] = {\n")
        for i in range(0, len(names), 6):
            handle.write("  " + " ".join('"%s",' % n for n in names[i:i + 6]) + "\n")
        handle.write("};\n\nstatic const uint32_t OUI_TABLE[] = {\n")
        items = sorted((prefix << 8) | index for prefix, index in table.items())
        for i in range(0, len(items), 8):
            handle.write("  " + " ".join("0x%08X," % v for v in items[i:i + 8]) + "\n")
        handle.write("};\n\nstatic const uint32_t OUI_TABLE_SIZE = sizeof(OUI_TABLE) / sizeof(OUI_TABLE[0]);\n")
    print("%s: %d Praefixe, %d Hersteller, %.1f KB" % (os.path.normpath(out), len(table), len(names), len(table) * 4 / 1024))


if __name__ == "__main__":
    main()
