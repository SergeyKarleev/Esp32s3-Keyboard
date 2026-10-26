# /// script
# requires-python = ">=3.11"
# dependencies = [
#     "pyserial",
#     "pillow",
# ]
# ///
"""Снимок экрана эмулятора по serial (команда S) в PNG.

  uv run tools/screenshot.py /dev/cu.usbmodemXXXX out.png
"""
import sys
import time

import serial
from PIL import Image


def main():
    port, out = sys.argv[1], sys.argv[2]
    s = serial.Serial(port, 115200, timeout=5)
    s.reset_input_buffer()
    s.write(b"S")
    deadline = time.time() + 10
    while True:
        line = s.readline()
        if line and not line.startswith(b"SNAP "):
            print(line.decode(errors="replace").rstrip())
        if line.startswith(b"SNAP "):
            break
        if time.time() > deadline:
            raise SystemExit("нет ответа SNAP")
    w, h = map(int, line.split()[1:3])
    data = s.read(w * h * 2)
    if len(data) != w * h * 2:
        # лог прошивки может вклиниться в поток: дополняем, картинка все равно читаема
        print(f"warning: получено {len(data)} из {w * h * 2} байт")
        data = data.ljust(w * h * 2, b"\0")
    img = Image.frombytes("RGB", (w, h), bytes(rgb for px in range(w * h) for rgb in rgb565(data, px)))
    img.save(out)
    print(f"{out}: {w}x{h}")


def rgb565(data, i):
    v = data[2 * i] | data[2 * i + 1] << 8
    return ((v >> 11) & 0x1F) * 255 // 31, ((v >> 5) & 0x3F) * 255 // 63, (v & 0x1F) * 255 // 31


if __name__ == "__main__":
    main()
