#!/bin/sh
# Прошивка без кнопок: 1200 бод на CDC перезагружают плату в загрузчик (USB-Serial/JTAG),
# после заливки — сброс сторожевым таймером в приложение.
set -e
cd "$(dirname "$0")/.."
PIO=${PIO:-$HOME/.platformio/penv/bin/pio}
APP=$(ls /dev/cu.usbmodem* 2>/dev/null | head -1)
if [ -n "$APP" ] && ! esptool --port "$APP" --before no-reset --after no-reset read-mac >/dev/null 2>&1; then
    python3 -c "import serial,time; s=serial.Serial('$APP',1200); s.dtr=False; time.sleep(0.2); s.close()" || true
    sleep 3
fi
PORT=""
for i in $(seq 1 20); do
    for p in /dev/cu.usbmodem*; do
        [ -e "$p" ] && esptool --port "$p" --before no-reset --after no-reset read-mac >/dev/null 2>&1 && PORT=$p && break 2
    done
    sleep 1
done
[ -n "$PORT" ] || { echo "Загрузчик не найден: зажмите BOOT, нажмите RST"; exit 1; }
"$PIO" run -t upload --upload-port "$PORT"
esptool --port "$PORT" --after watchdog-reset read-mac >/dev/null 2>&1 || true
echo "Готово"
