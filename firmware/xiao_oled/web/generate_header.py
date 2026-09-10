#!/usr/bin/env python3
"""Embed the standalone UTF-8 UI into the ESP32 firmware, with optional JS check."""
from pathlib import Path
import argparse
import re
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check-js', action='store_true', help='Validate inline scripts with node --check before generating')
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    source = root / 'index.html'
    destination = root.parent / 'web_ui.h'
    html = source.read_text(encoding='utf-8')
    delimiter = 'cpsMONK_HTML'
    if f'){delimiter}"' in html:
        raise SystemExit('Raw literal delimiter collision')
    if args.check_js:
        if not shutil.which('node'):
            raise SystemExit('Node.js is required for --check-js')
        scripts = re.findall(r'<script\b[^>]*>(.*?)</script>', html, re.S | re.I)
        if not scripts:
            raise SystemExit('No inline JavaScript found')
        for script in scripts:
            with tempfile.NamedTemporaryFile(mode='w', suffix='.js', encoding='utf-8') as temp:
                temp.write(script)
                temp.flush()
                subprocess.run(['node', '--check', temp.name], check=True)
        print(f'JavaScript syntax OK ({len(scripts)} inline script)')
    header = ('// Generated from web/index.html by web/generate_header.py. Do not edit.\n'
              '#pragma once\n#include <pgmspace.h>\n\n'
              f'static const char WEB_UI[] PROGMEM = R"{delimiter}(' + html + f'){delimiter}";\n')
    destination.write_text(header, encoding='utf-8')
    embedded = header.split(f'R"{delimiter}(', 1)[1].rsplit(f'){delimiter}";', 1)[0]
    assert embedded == html
    print(f'Generated {destination} ({len(header.encode("utf-8"))} bytes), HTML round-trip exact')


if __name__ == '__main__':
    main()
