#pragma once
// Экран и тач платы для LVGL, плюс отладка по serial: снимок экрана и виртуальные касания
// (tools/screenshot.py, tools/tap.py).

#include <stdint.h>

namespace display {

// дисплей, тач, LVGL; после этого можно строить интерфейс на lv_scr_act()
void begin();
// вызывать из loop(): крутит lv_timer_handler
void loop();

// снимок экрана в Serial: "SNAP <w> <h>\n" + RGB565 LE + "\nEND\n"
void dumpScreenshot();

// виртуальное касание: тап на 120 мс или удержание (D/M/U)
void tap(int16_t x, int16_t y);
void touchDown(int16_t x, int16_t y);
void touchMove(int16_t x, int16_t y);
void touchUp();
// есть ли сейчас виртуальное касание (для трекпада)
bool fakeTouch(int16_t& x, int16_t& y);

}  // namespace display
