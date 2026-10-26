#include <Arduino.h>

#include "display.h"
#include "hid.h"
#include "settings.h"
#include "swipe.h"
#include "touch.h"
#include "ui.h"

// отладочные команды по USB-serial: S — снимок экрана, T x y — касание,
// D x y / M x y / U — палец вниз/движение/вверх
static void handleSerial() {
    static char line[32];
    static size_t len = 0;
    while (Serial.available()) {
        char c = Serial.read();
        if (len == 0 && c == 'S') {
            display::dumpScreenshot();
            continue;
        }
        if (c == '\n' || c == '\r') {
            line[len] = 0;
            int x, y;
            if (sscanf(line, "T %d %d", &x, &y) == 2)
                display::tap(x, y);
            else if (sscanf(line, "D %d %d", &x, &y) == 2)
                display::touchDown(x, y);
            else if (sscanf(line, "M %d %d", &x, &y) == 2)
                display::touchMove(x, y);
            else if (line[0] == 'U')
                display::touchUp();
            else if (line[0] == 'I')
                touch::dumpState();
            else if (line[0] == 'O')
                hid::dumpUsbRequests();
            len = 0;
        } else if (len < sizeof line - 1) {
            line[len++] = c;
        }
    }
}

void setup() {
    Serial.begin(115200);
    // без открытого монитора (подключение к ТВ) CDC не должен блокировать loop
    Serial.setTxTimeoutMs(0);
    settings::load();
    hid::begin("ESP32 Keyboard", settings::get().bluetooth);
    hid::setTarget(hid::TargetKind(settings::get().target), settings::get().targetAddr);
    swipe::begin();
    display::begin();
    ui::create();
    Serial.println("[main] ready");
}

void loop() {
    handleSerial();
    touch::poll();
    ui::loop();
    hid::poll();
    swipe::poll();
    display::loop();
    delay(1);
}
