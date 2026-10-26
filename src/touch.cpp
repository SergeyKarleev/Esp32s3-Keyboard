#include "touch.h"

#include <Arduino.h>
#include <Wire.h>

#include "board.h"

namespace touch {

namespace {

constexpr uint8_t ADDR = 0x5D;
constexpr uint16_t REG_CONFIG_X_MAX = 0x8048;
constexpr uint16_t REG_STATUS = 0x814E;
// точки по 8 байт: id, x(2), y(2), размер(2), резерв
constexpr uint16_t REG_POINTS = 0x814F;
constexpr uint8_t MAX_POINTS = 2;
constexpr uint32_t POLL_MS = 5;

uint16_t maxX = LCD_W;
uint16_t maxY = LCD_H;

// GT911 не ответил или перестал отвечать: переинициализация не чаще раза в RETRY_MS
constexpr uint32_t RETRY_MS = 2000;
constexpr int MAX_ERRORS = 10;
bool found = false;
int errors = 0;
uint32_t lastTry = 0;
uint32_t lastPoll = 0;

uint8_t points = 0;
Point pts[MAX_POINTS];

bool readRegs(uint16_t reg, uint8_t* buf, size_t len) {
    Wire.beginTransmission(ADDR);
    Wire.write(reg >> 8);
    Wire.write(reg & 0xFF);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(ADDR, (uint8_t)len) != len) return false;
    for (size_t i = 0; i < len; i++) buf[i] = Wire.read();
    return true;
}

void writeReg(uint16_t reg, uint8_t v) {
    Wire.beginTransmission(ADDR);
    Wire.write(reg >> 8);
    Wire.write(reg & 0xFF);
    Wire.write(v);
    Wire.endTransmission();
}

// Ведомый, сброшенный посреди чтения (перепрошивка, reset без снятия питания), держит SDA в нуле
// и ждет тактов: 9 импульсов SCL и STOP отпускают шину.
void recoverBus() {
    pinMode(TOUCH_SDA, INPUT_PULLUP);
    pinMode(TOUCH_SCL, INPUT_PULLUP);
    delayMicroseconds(10);
    if (digitalRead(TOUCH_SDA)) return;
    Serial.println("[touch] SDA stuck low, recovering bus");
    pinMode(TOUCH_SCL, OUTPUT_OPEN_DRAIN);
    for (int i = 0; i < 9 && !digitalRead(TOUCH_SDA); i++) {
        digitalWrite(TOUCH_SCL, LOW);
        delayMicroseconds(5);
        digitalWrite(TOUCH_SCL, HIGH);
        delayMicroseconds(5);
    }
    pinMode(TOUCH_SDA, OUTPUT_OPEN_DRAIN);
    digitalWrite(TOUCH_SDA, LOW);
    delayMicroseconds(5);
    digitalWrite(TOUCH_SDA, HIGH);
    delayMicroseconds(5);
    pinMode(TOUCH_SDA, INPUT_PULLUP);
    pinMode(TOUCH_SCL, INPUT_PULLUP);
}

// Аппаратный сброс GT911; INT в нуле при выходе из сброса выбирает адрес 0x5D.
// После сброса чип отдает координаты в ориентации дисплея.
void resetChip() {
    pinMode(TOUCH_INT, OUTPUT);
    pinMode(TOUCH_RST, OUTPUT);
    digitalWrite(TOUCH_INT, LOW);
    digitalWrite(TOUCH_RST, LOW);
    delay(11);
    digitalWrite(TOUCH_RST, HIGH);
    delay(6);
    pinMode(TOUCH_INT, INPUT);
    delay(50);
}

}  // namespace

bool begin() {
    lastTry = millis();
    points = 0;
    Wire.end();
    recoverBus();
    resetChip();
    Wire.begin(TOUCH_SDA, TOUCH_SCL, 400000);
    uint8_t cfg[4];
    if (!readRegs(REG_CONFIG_X_MAX, cfg, sizeof cfg)) {
        Serial.println("[touch] GT911 not found, retrying");
        found = false;
        return false;
    }
    found = true;
    errors = 0;
    maxX = cfg[0] | cfg[1] << 8;
    maxY = cfg[2] | cfg[3] << 8;
    if (maxX == 0 || maxY == 0) {
        maxX = LCD_W;
        maxY = LCD_H;
    }
    Serial.printf("[touch] GT911 resolution %ux%u\n", maxX, maxY);
    return true;
}

void poll() {
    uint32_t now = millis();
    if (now - lastPoll < POLL_MS) return;
    lastPoll = now;
    if (!found) {
        if (now - lastTry >= RETRY_MS) begin();
        return;
    }
    uint8_t status;
    if (!readRegs(REG_STATUS, &status, 1)) {
        if (++errors >= MAX_ERRORS) {
            Serial.println("[touch] GT911 stopped responding");
            found = false;
            points = 0;
        }
        return;
    }
    errors = 0;
    // нового кадра нет — остается предыдущий
    if (!(status & 0x80)) return;
    uint8_t n = status & 0x0F;
    if (n > MAX_POINTS) n = MAX_POINTS;
    uint8_t buf[8 * MAX_POINTS];
    if (n > 0 && readRegs(REG_POINTS, buf, 8 * n)) {
        for (uint8_t i = 0; i < n; i++) {
            const uint8_t* p = buf + 8 * i;
            uint16_t rx = p[1] | p[2] << 8;
            uint16_t ry = p[3] | p[4] << 8;
            pts[i].x = int16_t(uint32_t(rx) * LCD_W / maxX);
            pts[i].y = int16_t(uint32_t(ry) * LCD_H / maxY);
        }
        points = n;
    } else {
        points = 0;
    }
    writeReg(REG_STATUS, 0);
}

uint8_t count() { return points; }

void dumpState() {
    uint8_t status = 0;
    bool ok = found && readRegs(REG_STATUS, &status, 1);
    Serial.printf("[touch] found %d, errors %d, max %ux%u, status %s 0x%02X, points %u (%d,%d)\n", found, errors, maxX, maxY,
                  ok ? "ok" : "fail", status, points, pts[0].x, pts[0].y);
}

const Point& point(uint8_t i) { return pts[i < MAX_POINTS ? i : 0]; }

}  // namespace touch
