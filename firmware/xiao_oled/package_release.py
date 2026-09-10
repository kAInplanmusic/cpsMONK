#!/usr/bin/env python3
"""Build, run synthetic tests, package verified images. Does not flash or push."""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'artifacts' / 'xiao-oled'
OUT.mkdir(parents=True, exist_ok=True)
log = []
def run(args, env=None):
    result = subprocess.run([str(a) for a in args], cwd=ROOT, env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    print(result.stdout, end='')
    log.append('$ ' + ' '.join(map(str, args)) + '\n' + result.stdout)
    (OUT / 'VERIFICATION.txt').write_text('\n'.join(log), encoding='utf8')
    if result.returncode:
        raise SystemExit(result.returncode)

run([sys.executable, 'firmware/xiao_oled/web/generate_header.py'])
run(['g++','-std=c++11','-Wall','-Wextra','-Werror','-O2',
     'firmware/xiao_oled/tests/test_analyzer.cpp','-o',OUT/'test-analyzer'])
run([OUT/'test-analyzer'])
run(['g++','-std=c++11','-O1','-g','-fsanitize=address,undefined',
     'firmware/xiao_oled/tests/test_analyzer.cpp','-o',OUT/'test-analyzer-asan'])
run([OUT/'test-analyzer-asan'])
run(['node','firmware/xiao_oled/tests/test_ui.cjs'])
run(['pio','run','-c','platformio-oled.ini','-e','xiao-oled'])
# Resolve the interpreter actually used by pipx PlatformIO (not system Python).
pio_path = shutil.which('pio')
if pio_path is None:
    raise SystemExit('PlatformIO executable not found')
pio = Path(pio_path).resolve()
python = pio.read_text().splitlines()[0].removeprefix('#!')
home = Path.home()
esptool = home/'.platformio/packages/tool-esptoolpy/esptool.py'
framework = home/'.platformio/packages/framework-arduinoespressif32'
build = ROOT/'.pio/build/xiao-oled'
for name in ['bootloader.bin','partitions.bin','firmware.bin']:
    shutil.copy2(build/name, OUT/name)
shutil.copy2(framework/'tools/partitions/boot_app0.bin',OUT/'boot_app0.bin')
run([python,esptool,'--chip','esp32s3','merge_bin','-o',OUT/'cpsmonk-xiao-oled-merged.bin',
     '0x0',OUT/'bootloader.bin','0x8000',OUT/'partitions.bin',
     '0xe000',OUT/'boot_app0.bin','0x10000',OUT/'firmware.bin'])
run([python,esptool,'--chip','esp32s3','image_info',OUT/'firmware.bin'])
merged=(OUT/'cpsmonk-xiao-oled-merged.bin').read_bytes()
for offset,name in [(0,'bootloader.bin'),(0x8000,'partitions.bin'),(0xe000,'boot_app0.bin'),(0x10000,'firmware.bin')]:
    part=(OUT/name).read_bytes()
    assert merged[offset:offset+len(part)] == part, f'Merged image mismatch: {name}'
log.append('PASS merged image: all four images match byte-for-byte at verified offsets.\nNO HARDWARE FLASH OR PHYSICAL VALIDATION PERFORMED.')
(OUT/'VERIFICATION.txt').write_text('\n'.join(log),encoding='utf8')
shutil.copy2(ROOT/'firmware/xiao_oled/README-DE.md',OUT/'README-DE.md')
(OUT/'FLASH.txt').write_text('''cpsMONK / NORMAL XIAO ESP32-S3 / SSD1306 128x32 / INMP441
Preferred: use PlatformIO from the source project (see README-DE.md).
Alternative with esptool 4.x installed, actual board port in place of /dev/ttyACM0:
python -m esptool --chip esp32s3 --port /dev/ttyACM0 --baud 460800 write_flash 0x0 cpsmonk-xiao-oled-merged.bin
Merged image includes bootloader, partition table, boot_app0 and application.
DO NOT flash firmware.bin alone at 0x0. That application belongs at 0x10000.
Flashing replaces the board firmware. Disconnect machine electronics; USB powers the measurement board.
No flash has been performed by this package script. No erase_flash step required.
''',encoding='utf8')
files=sorted(p for p in OUT.iterdir() if p.suffix=='.bin' or p.name in ['FLASH.txt','README-DE.md','VERIFICATION.txt'])
(OUT/'SHA256SUMS').write_text(''.join(f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n' for p in files))
archive=ROOT/'artifacts/cpsmonk-xiao-oled-deploy.zip'
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
    for p in files+[OUT/'SHA256SUMS']:z.write(p, 'xiao-oled/'+p.name)
    z.write(ROOT/'platformio-oled.ini','source/platformio-oled.ini')
    for p in (ROOT/'firmware/xiao_oled').rglob('*'):
        if p.is_file() and '__pycache__' not in p.parts:z.write(p, 'source/'+str(p.relative_to(ROOT)))
with zipfile.ZipFile(archive) as z:assert z.testzip() is None
print('VERIFIED PACKAGE:',archive)
