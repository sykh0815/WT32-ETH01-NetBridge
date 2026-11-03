#!/usr/bin/env python3
"""Serieller Dauer-Logger fuer NetBridge.
Start (Mac):  ~/.platformio/penv/bin/python tools/serial_log.py [port] [poll_sek]
Schreibt logs/serial_<zeit>.log, markiert Neustarts/Abstuerze und fragt periodisch 'status'/'tasks' ab.
Beenden mit Strg+C."""
import sys, glob, time, os, re, datetime
import serial
port = sys.argv[1] if len(sys.argv) > 1 else (glob.glob('/dev/cu.usbserial*') + glob.glob('/dev/cu.wchusbserial*') + glob.glob('/dev/cu.SLAB*') + [None])[0]
poll = int(sys.argv[2]) if len(sys.argv) > 2 else 60
if not port: sys.exit('Kein Port gefunden - Adapter anstecken oder Port als Argument angeben')
os.makedirs('logs', exist_ok=True)
name = datetime.datetime.now().strftime('logs/serial_%Y%m%d_%H%M%S.log')
KEY = re.compile(r'Guru Meditation|abort\(\)|Backtrace|assert failed|Brownout|rst:0x|\bWDT\b|panic|LoadProhibited|StoreProhibited|heap|Stack', re.I)
s = serial.Serial(port, 115200, timeout=0.2)
f = open(name, 'a', buffering=1)
print(f'Logge {port} -> {name}  (poll alle {poll}s)')
last = time.time(); buf = b''; n = 0
while True:
    try:
        buf += s.read(512)
        while b'\n' in buf:
            line, buf = buf.split(b'\n', 1)
            t = line.decode('utf-8', 'replace').rstrip('\r')
            mark = '!! ' if KEY.search(t) else ''
            f.write(f'{datetime.datetime.now():%H:%M:%S.%f}'[:-3] + f' {mark}{t}\n')
            if mark: print(mark + t)
        if time.time() - last > poll:
            s.write((b'status\n' if n % 2 == 0 else b'tasks\n')); n += 1; last = time.time()
    except (serial.SerialException, OSError) as e:
        f.write(f'### Verbindung verloren: {e}\n'); print('Verbindung verloren, warte...')
        s.close(); time.sleep(2)
        while True:
            try: s.open(); break
            except Exception: time.sleep(1)
    except KeyboardInterrupt:
        break
