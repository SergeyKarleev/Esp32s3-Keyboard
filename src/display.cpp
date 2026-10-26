#include "display.h"

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>

#include <algorithm>

#include "board.h"
#include "touch.h"

namespace display {

namespace {

constexpr int DRAW_LINES = 40;

Arduino_DataBus* bus = new Arduino_ESP32QSPI(LCD_CS, LCD_SCK, LCD_D0, LCD_D1, LCD_D2, LCD_D3);
Arduino_GFX* gfx = new Arduino_NV3041A(bus, GFX_NOT_DEFINED, 0, true);

lv_disp_draw_buf_t drawBuf;
lv_disp_drv_t dispDrv;
lv_indev_drv_t indevDrv;

// копия экрана в PSRAM для снимков (то, что ушло на дисплей)
uint16_t* shadow = nullptr;

void flush(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color) {
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;
    auto* px = reinterpret_cast<uint16_t*>(&color->full);
    gfx->draw16bitRGBBitmap(area->x1, area->y1, px, w, h);
    if (shadow) {
        for (uint32_t row = 0; row < h; row++)
            memcpy(shadow + (area->y1 + row) * LCD_W + area->x1, px + row * w, w * 2);
    }
    lv_disp_flush_ready(drv);
}

int16_t fakeX = 0, fakeY = 0;
uint32_t fakeUntil = 0;
bool fakeHeld = false;
uint32_t fakeHeldAt = 0;
// удержание без D/M дольше этого — значит, U потерялся
constexpr uint32_t FAKE_HOLD_TIMEOUT_MS = 3000;

void readTouch(lv_indev_drv_t*, lv_indev_data_t* data) {
    // настоящий тач всегда важнее виртуального
    if (touch::count() > 0) {
        fakeHeld = false;
        fakeUntil = 0;
        data->point.x = touch::point(0).x;
        data->point.y = touch::point(0).y;
        data->state = LV_INDEV_STATE_PR;
        return;
    }
    int16_t x, y;
    if (fakeTouch(x, y)) {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PR;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

}  // namespace

bool fakeTouch(int16_t& x, int16_t& y) {
    if (fakeHeld && millis() - fakeHeldAt > FAKE_HOLD_TIMEOUT_MS) fakeHeld = false;
    if (!fakeHeld && millis() >= fakeUntil) return false;
    x = fakeX;
    y = fakeY;
    return true;
}

void tap(int16_t x, int16_t y) {
    fakeX = x;
    fakeY = y;
    fakeUntil = millis() + 120;
}

void touchDown(int16_t x, int16_t y) {
    fakeX = x;
    fakeY = y;
    fakeHeld = true;
    fakeHeldAt = millis();
    fakeUntil = millis() + 80;
}

void touchMove(int16_t x, int16_t y) {
    fakeX = x;
    fakeY = y;
    fakeHeldAt = millis();
}

void touchUp() { fakeHeld = false; }

void dumpScreenshot() {
    if (!shadow) return;
    // снимок большой: на время передачи ждем хост, иначе CDC выбросит данные
    Serial.setTxTimeoutMs(2000);
    Serial.printf("SNAP %u %u\n", LCD_W, LCD_H);
    const uint8_t* data = reinterpret_cast<const uint8_t*>(shadow);
    size_t size = size_t(LCD_W) * LCD_H * 2, sent = 0;
    uint32_t start = millis();
    while (sent < size && millis() - start < 10000) {
        size_t n = Serial.write(data + sent, std::min<size_t>(512, size - sent));
        if (n == 0) delay(1);
        sent += n;
    }
    Serial.print("\nEND\n");
    Serial.flush();
    Serial.setTxTimeoutMs(0);
}

void begin() {
    pinMode(LCD_BL, OUTPUT);
    digitalWrite(LCD_BL, HIGH);
    if (!gfx->begin()) Serial.println("[lcd] init failed");
    gfx->fillScreen(RGB565_BLACK);
    touch::begin();

    shadow = static_cast<uint16_t*>(heap_caps_calloc(LCD_W * LCD_H, 2, MALLOC_CAP_SPIRAM));

    lv_init();
    size_t bufPixels = LCD_W * DRAW_LINES;
    auto* buf1 = static_cast<lv_color_t*>(heap_caps_malloc(bufPixels * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
    auto* buf2 = static_cast<lv_color_t*>(heap_caps_malloc(bufPixels * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
    lv_disp_draw_buf_init(&drawBuf, buf1, buf2, bufPixels);

    lv_disp_drv_init(&dispDrv);
    dispDrv.hor_res = LCD_W;
    dispDrv.ver_res = LCD_H;
    dispDrv.flush_cb = flush;
    dispDrv.draw_buf = &drawBuf;
    lv_disp_drv_register(&dispDrv);

    lv_indev_drv_init(&indevDrv);
    indevDrv.type = LV_INDEV_TYPE_POINTER;
    indevDrv.read_cb = readTouch;
    lv_indev_drv_register(&indevDrv);
}

void loop() { lv_timer_handler(); }

}  // namespace display
