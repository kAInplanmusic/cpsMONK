#!/usr/bin/env python3
"""cpsMONK - serieller Helfer fuer die XIAO-OLED-Firmware.

Fragt den Geraetestatus ueber das Konsolen-Kommando '?' ab, startet oder
verwirft Messungen und kann den Status fortlaufend anzeigen.

Wichtig: Dieses Skript toggelt DTR/RTS NICHT. Der XIAO ESP32-S3 nutzt den
eingebauten USB-Serial-JTAG (USB-ID 303a:1001). Ein manuell erzeugter
Reset-Impuls ueber die DTR/RTS-Leitungen kann den Port verklemmmen: das Board
sendet dann nichts mehr und esptool findet kein Ziel mehr. Zum Zuruecksetzen
des Boards einfach neu flashen - der Upload endet mit einem sauberen
Hardware-Reset.

Voraussetzung: pyserial. Aus der PlatformIO-Umgebung heraus verfuegbar unter
~/.local/share/pipx/venvs/platformio/bin/python.

Beispiele:
    python3 tools/cpsmonk_serial.py status
    python3 tools/cpsmonk_serial.py status --json
    python3 tools/cpsmonk_serial.py watch --interval 2
    python3 tools/cpsmonk_serial.py start
    python3 tools/cpsmonk_serial.py reset
"""

from __future__ import annotations

import argparse
import glob
import json
import re
import sys
import time

try:
    import serial
except ImportError:  # pragma: no cover
    sys.exit(
        "pyserial fehlt. Entweder 'pip install pyserial' oder den "
        "PlatformIO-Interpreter nutzen:\n"
        "  ~/.local/share/pipx/venvs/platformio/bin/python tools/cpsmonk_serial.py status"
    )

DEFAULT_BAUD = 115200

# Reihenfolge wie in main.cpp (loop, Kommando '?'). 'dc' steht vor 'err',
# weil 'err' Freitext ist und dahinter nichts mehr geparst werden kann.
_FIELDS = ("state", "impacts", "seen", "cps", "form", "level", "amp", "dB", "noise", "thr", "win", "gate", "dc", "err")

# Feldreihenfolge wie in main.cpp (Kommando '?'). Alle Felder ausser dem
# letzten sind nicht-gierig, damit 'err' als Freitext Leerzeichen enthalten
# darf ("Impulsrate unter 50 CPS: Messung abgebrochen").
_STATUS_RE = re.compile(r"\s+".join(f"{name}=(?P<{name}>.*?)" for name in _FIELDS[:-1]) + r"\s+err=(?P<err>.*)")

_INT_FIELDS = {"impacts", "seen", "gate"}
_FLOAT_FIELDS = {"cps", "form", "amp", "dB", "noise", "thr", "win", "dc"}

# Werte stammen aus levelName()/stateName() in firmware/xiao_oled/main.cpp.
# Sie werden hier uebersetzt, aber immer zusammen mit dem Rohwert ausgegeben,
# damit eine unbekannte oder neue Stufe nicht stillschweigend verschwindet.
_LEVEL_TEXT = {
    "silent": "kein Signal",
    "quiet": "zu leise",
    "good": "Abstand ok",
    "high": "zu laut",
    "loud": "zu laut",
}

_STATE_TEXT = {
    "idle": "Leerlauf",
    "calibrating": "Kalibrierung",
    "waiting": "wartet auf Motor",
    "warmup": "Vorlauf",
    "recording": "Messung laeuft",
    "complete": "fertig",
    "failed": "abgebrochen",
}


def translate(mapping: dict, raw: object) -> str:
    """Uebersetzung plus Rohwert, z. B. 'zu laut (high)'."""
    text = mapping.get(str(raw))
    return f"{text} ({raw})" if text else str(raw)

_LABELS = {
    "state": "Zustand",
    "impacts": "Impulse",
    "seen": "gesehen",
    "cps": "CPS",
    "form": "Formaehnlichkeit %",
    "level": "Pegel",
    "amp": "Amplitude",
    "dB": "Pegel dB",
    "noise": "Rauschen",
    "thr": "Schwelle",
    "win": "Fenster s",
    "gate": "Auto-Start frei",
    "dc": "DC-Offset roh",
    "err": "Fehler",
}

# Ab diesem Betrag des rohen Mittelwerts gilt die I2S-Leitung als auffaellig.
# Ein INMP441 liegt symmetrisch um 0; ein so grosser Offset deutet auf ein
# Signal, das gegen eine Rail klemmt, oder auf eine unterbrochene Leitung.
DC_SUSPECT = 0.5


def find_port() -> str | None:
    """Ersten plausiblen ESP32-Port liefern."""
    candidates: list[str] = []
    for pattern in ("/dev/ttyACM*", "/dev/ttyUSB*"):
        candidates.extend(sorted(glob.glob(pattern)))
    if not candidates:
        return None
    if len(candidates) > 1:
        print(f"Mehrere Ports gefunden: {', '.join(candidates)}", file=sys.stderr)
        print("Bitte mit --port den richtigen auswaehlen.", file=sys.stderr)
        return None
    return candidates[0]


def open_port(port: str, baud: int) -> serial.Serial:
    try:
        return serial.Serial(port, baud, timeout=0.25)
    except serial.SerialException as exc:
        sys.exit(
            f"Port {port} laesst sich nicht oeffnen: {exc}\n"
            "Moegliche Ursachen: Kabel kein Datenkabel, Board nicht verbunden,\n"
            "oder ein anderes Programm haelt den Port (Serial-Monitor schliessen)."
        )


def parse_status(line: str) -> dict | None:
    match = _STATUS_RE.search(line)
    if not match:
        return None
    raw = match.groupdict()
    out: dict = {}
    for key, value in raw.items():
        value = (value or "").strip()
        # Die Firmware haengt an 'win' eine Einheit an ("0.0s").
        if key == "win":
            value = value.rstrip("sS")
        try:
            if key in _INT_FIELDS:
                out[key] = int(value)
            elif key in _FLOAT_FIELDS:
                out[key] = float(value)
            else:
                out[key] = value
        except ValueError:
            out[key] = value
    return out


def iter_lines(sp: serial.Serial, deadline: float):
    """Vollstaendige Zeilen vom Port liefern.

    Der USB-Serial-JTAG uebertraegt lange Zeilen gelegentlich in mehreren
    Stuecken und die Statuszeile ist ~150 Zeichen lang. Ohne Zusammenfuegen
    bricht sie mitten in einem Feld ab (beobachtet: 'amp=0.' | '000000 dB=...').
    Eine unvollstaendige Statuszeile laeuft daher weiter in den Puffer.
    """
    buffer = ""
    while time.monotonic() < deadline:
        chunk = sp.readline().decode("utf-8", "replace")
        if chunk:
            buffer += chunk
            complete = buffer.endswith("\n")
        else:
            # Timeout ohne neue Daten: nur dann abschliessen, wenn keine
            # unvollstaendige Statuszeile im Puffer liegt.
            complete = not (buffer.startswith("state=") and _STATUS_RE.search(buffer) is None)
        if not complete:
            continue
        line = buffer.strip()
        buffer = ""
        if line:
            yield line


def read_status(sp: serial.Serial, wait: float, retries: int) -> dict | None:
    """'?' senden und auf eine vollstaendige Statuszeile warten.

    Die erste Antwort geht gelegentlich verloren, solange der CDC-Puffer noch
    leerlaeuft - deshalb wird das Kommando mehrfach gesendet.
    """
    deadline = time.monotonic() + wait
    attempt = 0
    while time.monotonic() < deadline and attempt < retries:
        attempt += 1
        sp.reset_input_buffer()
        sp.write(b"?")
        sp.flush()
        attempt_end = time.monotonic() + max(1.5, wait / retries)
        for line in iter_lines(sp, attempt_end):
            parsed = parse_status(line)
            if parsed is not None:
                return parsed
            print(f"  · {line}", file=sys.stderr)
    return None


NO_ANSWER_HINT = """Keine Antwort von der Konsole.

Das Geraet laeuft trotzdem: leuchtet der WLAN-AP cpsMONK-XXXX weiter, ist nur
der USB-Serial-JTAG-Kanal verklemmt. Das passiert beim haeufigen Oeffnen und
Schliessen des Ports.

Abhilfe: Firmware neu flashen (der Upload endet mit einem Hardware-Reset):
  pio run -c platformio-oled.ini -e xiao-oled -t upload --upload-port /dev/ttyACM0

Danach diesen Befehl erneut ausfuehren. Nicht per DTR/RTS zuruecksetzen."""


def print_human(status: dict) -> None:
    level_text = translate(_LEVEL_TEXT, status.get("level", ""))
    state_raw = str(status.get("state", ""))

    def row(key: str, text: str) -> None:
        print(f"  {_LABELS.get(key, key):<22} {text}")

    row("state", translate(_STATE_TEXT, status.get("state", "?")))
    row("impacts", f"{status.get('impacts', 0)} (gesehen: {status.get('seen', 0)})")
    row("cps", f"{status.get('cps', 0.0):.3f}")
    row("form", f"{status.get('form', 0.0):.2f} %")
    row("level", f"{level_text}  (amp {status.get('amp', 0.0):.6f}, {status.get('dB', 0.0):.1f} dB)")
    row("noise", f"{status.get('noise', 0.0):.7f}   (Schwelle {status.get('thr', 0.0):.6f})")
    row("win", f"{status.get('win', 0.0):.1f}")
    row("gate", "ja" if status.get("gate") else "nein")
    dc = float(status.get("dc") or 0.0)
    amp = float(status.get("amp") or 0.0)
    row("dc", f"{dc:+.5f}" + ("   <-- auffaellig" if abs(dc) >= DC_SUSPECT else ""))
    error = status.get("err") or "-"
    row("err", str(error))

    # Der Pegelindikator hat einen eigenen DC-Blocker. Ein auf die Rail
    # geklemmter Eingang sieht damit aus wie "leise". Nur der rohe Mittelwert
    # unterscheidet ihn von einem wirklich ruhigen Raum.
    if abs(dc) >= DC_SUSPECT and amp < 0.05:
        print(
            f"\n  HINWEIS: DC-Offset {dc:+.5f} bei kleiner Amplitude ({amp:.6f}).\n"
            "  Der Blockkondensator der Anzeige macht daraus 'leise' - das ist NICHT\n"
            "  dasselbe wie ein ruhiger Raum. I2S-Datenleitung/INMP441 pruefen."
        )
    # Nur in den Zustaenden auswerten, in denen monitor() ueberhaupt laeuft.
    # In calibrating/waiting/warmup/recording stehen diese Felder per Design auf 0.
    elif state_raw in ("idle", "failed") and amp == 0.0 and dc == 0.0:
        print(
            "\n  HINWEIS: Alle Samples sind null - die I2S-Uhr laeuft (die Kalibrierung\n"
            "  zaehlt Samples), aber die Datenleitung fuehrt nichts. Ein Software-Reset\n"
            "  hilft dabei oft NICHT: das Mikrofon bleibt bestromt und kann nach einem\n"
            "  Taktstopp stumm bleiben. Board richtig vom Strom trennen (USB abziehen)."
        )


def summary_line(status: dict) -> str:
    level = translate(_LEVEL_TEXT, status.get("level", ""))
    state = translate(_STATE_TEXT, status.get("state", "?"))
    error = status.get("err") or "-"
    return (
        f"{time.strftime('%H:%M:%S')}  {state:<22} "
        f"imp={status.get('impacts', 0):<5} cps={status.get('cps', 0.0):>7.3f} "
        f"form={status.get('form', 0.0):>5.2f}%  pegel={level:<20} err={error}"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=("status", "watch", "start", "reset"), help="Aktion")
    parser.add_argument("--port", help="serieller Port (Standard: automatisch)")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD, help=f"Baudrate (Standard {DEFAULT_BAUD})")
    parser.add_argument("--json", action="store_true", help="Status als JSON ausgeben")
    parser.add_argument("--interval", type=float, default=2.0, help="Sekunden zwischen Abfragen bei 'watch'")
    parser.add_argument("--wait", type=float, default=6.0, help="Wartezeit auf eine Antwort in Sekunden")
    parser.add_argument("--count", type=int, default=0, help="bei 'watch': nach N Zeilen beenden (0 = endlos)")
    args = parser.parse_args()

    port = args.port or find_port()
    if not port:
        print("Keinen seriellen Port gefunden. Ist das Board angeschlossen?", file=sys.stderr)
        return 1

    sp = open_port(port, args.baud)
    try:
        if args.command == "status":
            status = read_status(sp, args.wait, retries=3)
            if status is None:
                print(NO_ANSWER_HINT, file=sys.stderr)
                return 1
            if args.json:
                print(json.dumps(status, indent=2, ensure_ascii=False))
            else:
                print_human(status)
            return 0

        if args.command == "watch":
            shown = 0
            while True:
                status = read_status(sp, args.wait, retries=2)
                if status is None:
                    print(f"{time.strftime('%H:%M:%S')}  keine Antwort", file=sys.stderr)
                elif args.json:
                    print(json.dumps(status, ensure_ascii=False))
                else:
                    print(summary_line(status))
                shown += 1
                if args.count and shown >= args.count:
                    return 0
                time.sleep(max(0.0, args.interval))

        if args.command in ("start", "reset"):
            key = b"s" if args.command == "start" else b"r"
            sp.reset_input_buffer()
            sp.write(key)
            sp.flush()
            print(f"'{key.decode()}' gesendet.")
            time.sleep(1.0)
            status = read_status(sp, args.wait, retries=2)
            if status is None:
                print(NO_ANSWER_HINT, file=sys.stderr)
                return 1
            if args.json:
                print(json.dumps(status, indent=2, ensure_ascii=False))
            else:
                print_human(status)
            return 0
    finally:
        sp.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
