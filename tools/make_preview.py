#!/usr/bin/env python3
"""Erzeugt docs/social-preview.png (GitHub-Vorschaubild, 1280x640) und docs/wt32-eth01-netbridge-banner.png.

Benoetigt cairosvg (pip install cairosvg) und die Schrift DejaVu Sans.
Aufruf aus dem Projektordner:  python3 tools/make_preview.py
"""
import os
import shutil

import cairosvg

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
NAME = "WT32-ETH01 NetBridge"


def pill(x, y, text, width, background, color):
    return (f"<rect x='{x}' y='{y}' width='{width}' height='40' rx='20' fill='{background}'/>"
            f"<text x='{x + width / 2}' y='{y + 27}' font-family='DejaVu Sans' font-size='19' font-weight='bold' "
            f"fill='{color}' text-anchor='middle'>{text}</text>")


def main():
    source = open(os.path.join(ROOT, "docs", "wt32-eth01.svg"), encoding="utf-8").read()
    board = source[source.index("<g>"):source.rindex("</g>") + 4]
    svg = f"""<svg xmlns='http://www.w3.org/2000/svg' width='1280' height='640' viewBox='0 0 1280 640'>
<rect width='1280' height='640' fill='#f2f6fa'/><rect width='1280' height='10' fill='#1263a6'/>
<text x='70' y='140' font-family='DejaVu Sans' font-size='64' font-weight='bold' fill='#1263a6'>{NAME}</text>
<text x='70' y='200' font-family='DejaVu Sans' font-size='32' font-weight='bold' fill='#17212b'>Ethernet &#8644; WiFi bridge &amp; access point</text>
<text x='70' y='262' font-family='DejaVu Sans' font-size='24' fill='#4b5865'>Firmware for the WT32-ETH01 (ESP32 + LAN8720):</text>
<text x='70' y='296' font-family='DejaVu Sans' font-size='24' fill='#4b5865'>NAT or bridge, firewall, web interface (EN/DE).</text>
{pill(70, 350, 'WiFi client', 170, '#dbe9f7', '#1263a6')}{pill(254, 350, 'Access point', 190, '#dbe9f7', '#1263a6')}{pill(458, 350, 'Firewall', 140, '#fdf1dc', '#875b00')}
{pill(70, 404, 'NAT + Bridge', 190, '#e3f4ea', '#08783d')}{pill(274, 404, 'Captive portal', 210, '#e4e8ec', '#3b4650')}
<text x='70' y='540' font-family='DejaVu Sans' font-size='20' fill='#4b5865'>[ LAN device ] &#8212; cable &#8212; [ NetBridge ] ))) WiFi ))) [ Router ]</text>
<text x='70' y='572' font-family='DejaVu Sans' font-size='20' fill='#4b5865'>[ phone ] ))) WiFi ))) [ NetBridge ] &#8212; cable &#8212; [ Router ]</text>
<g transform='translate(740,262) scale(0.82)'>{board}</g></svg>"""
    target = os.path.join(ROOT, "docs", "social-preview.png")
    cairosvg.svg2png(bytestring=svg.encode(), write_to=target, output_width=1280, output_height=640)
    shutil.copyfile(target, os.path.join(ROOT, "docs", "wt32-eth01-netbridge-banner.png"))
    print("docs/social-preview.png und docs/wt32-eth01-netbridge-banner.png erzeugt")


if __name__ == "__main__":
    main()
