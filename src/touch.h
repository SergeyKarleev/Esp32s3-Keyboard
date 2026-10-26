#pragma once
// Драйвер GT911 (I2C 0x5D): до двух точек, координаты в пикселях экрана.
// poll() читает чип, остальные функции отдают последний прочитанный кадр.

#include <stdint.h>

namespace touch {

struct Point {
    int16_t x, y;
};

bool begin();
// опросить чип (не чаще раза в несколько мс); вызывать из loop()
void poll();
// число касаний в последнем кадре (0..2)
uint8_t count();
const Point& point(uint8_t i);
// состояние драйвера в Serial (отладка)
void dumpState();

}  // namespace touch
