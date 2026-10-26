# /// script
# requires-python = ">=3.11"
# dependencies = [
#     "pyserial",
# ]
# ///
"""Виртуальное касание экрана эмулятора: uv run tools/tap.py /dev/cu.usbmodemXXXX X Y"""
import sys

import serial

s = serial.Serial(sys.argv[1], 115200, timeout=1)
s.write(f"T {int(sys.argv[2])} {int(sys.argv[3])}\n".encode())
s.flush()
