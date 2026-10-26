#include "trackpad.h"

#include <Arduino.h>

#include <algorithm>
#include <cmath>

#include "display.h"
#include "hid.h"
#include "settings.h"
#include "theme.h"
#include "touch.h"

namespace trackpad {

namespace {

constexpr uint32_t TAP_MS = 220;
// после тапа ждем столько: если палец вернулся — это двойной тап или перетаскивание
constexpr uint32_t TAP_DRAG_MS = 220;
constexpr int TAP_MOVE_PX = 10;
constexpr int SCROLL_STEP_PX = 14;
// сдвиг больше этого за кадр — сменился палец, а не движение
constexpr int SECOND_JUMP_PX = 40;

lv_obj_t* pad = nullptr;
lv_obj_t* strip = nullptr;
// кнопки мыши читаются прямо с тача: одним пальцем ведем курсор, другим держим кнопку
lv_obj_t* btnLeft = nullptr;
lv_obj_t* btnRight = nullptr;
// кнопки, зажатые пальцами на экранных ЛКМ/ПКМ (без перетаскивания жестом)
uint8_t heldButtons = 0;

enum class Zone : uint8_t { None, Pad, Strip };

struct Gesture {
    Zone zone = Zone::None;
    uint32_t downAt = 0;
    float lastX = 0, lastY = 0;
    uint8_t fingers = 0;
    uint8_t maxFingers = 0;
    int moved = 0;
    // второе касание вскоре после тапа: станет перетаскиванием или двойным кликом
    bool afterTap = false;
    bool dragging = false;
    // курсор ведется, и второй палец коснулся: тап — левый клик, удержание — кнопка зажата
    bool second = false;
    bool secondHold = false;
    uint32_t secondAt = 0;
    float remX = 0, remY = 0;
    float scrollX = 0, scrollY = 0;
};
Gesture g;
uint32_t pendingClickAt = 0;

bool inside(lv_obj_t* obj, int16_t x, int16_t y) {
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    return x >= a.x1 && x <= a.x2 && y >= a.y1 && y <= a.y2;
}

// все касания кадра (тач или отладочное с serial)
uint8_t readTouches(touch::Point* out) {
    uint8_t n = touch::count();
    for (uint8_t i = 0; i < n && i < 2; i++) out[i] = touch::point(i);
    if (n == 0 && display::fakeTouch(out[0].x, out[0].y)) n = 1;
    return std::min<uint8_t>(n, 2);
}

// касания в зоне: число пальцев и опорная точка (середина между двумя пальцами)
uint8_t fingersIn(lv_obj_t* zone, const touch::Point* pts, uint8_t n, float& x, float& y, touch::Point* inZone) {
    uint8_t k = 0;
    float sx = 0, sy = 0;
    for (uint8_t i = 0; i < n; i++) {
        if (!inside(zone, pts[i].x, pts[i].y)) continue;
        inZone[k] = pts[i];
        sx += pts[i].x;
        sy += pts[i].y;
        k++;
    }
    if (k) {
        x = sx / k;
        y = sy / k;
    }
    return k;
}

void setHeld(uint8_t b) {
    if (b == heldButtons) return;
    uint8_t pressed = b & ~heldButtons, released = heldButtons & ~b;
    heldButtons = b;
    for (lv_obj_t* o : {btnLeft, btnRight}) {
        uint8_t bit = o == btnLeft ? hid::BTN_LEFT : hid::BTN_RIGHT;
        if (b & bit)
            lv_obj_add_state(o, LV_STATE_PRESSED);
        else
            lv_obj_clear_state(o, LV_STATE_PRESSED);
    }
    hid::mouseButtons((hid::mouseButtonsState() | pressed) & ~released);
}

void moveCursor(float dx, float dy) {
    float base = 0.4f + settings::get().speed * 0.25f;
    float v = std::min(std::sqrt(dx * dx + dy * dy), 40.0f);
    float gain = base * (1.0f + v / 6.0f);
    g.remX += dx * gain;
    g.remY += dy * gain;
    int ix = int(g.remX), iy = int(g.remY);
    g.remX -= ix;
    g.remY -= iy;
    if (ix || iy) hid::mouseMove(ix, iy);
}

void scroll(float dx, float dy) {
    // «натуральная» прокрутка: палец вверх — страница вверх (колесо вниз)
    float sign = settings::get().naturalScroll ? 1.0f : -1.0f;
    g.scrollY += dy * sign;
    g.scrollX -= dx * sign;
    int wheel = int(g.scrollY / SCROLL_STEP_PX);
    int pan = int(g.scrollX / SCROLL_STEP_PX);
    g.scrollY -= wheel * SCROLL_STEP_PX;
    g.scrollX -= pan * SCROLL_STEP_PX;
    if (wheel || pan) hid::mouseScroll(wheel, pan);
}

void start(Zone zone, uint8_t n, float x, float y, uint32_t now) {
    g = Gesture{};
    g.zone = zone;
    g.downAt = now;
    g.lastX = x;
    g.lastY = y;
    g.fingers = g.maxFingers = n;
    if (zone == Zone::Pad && pendingClickAt && now - pendingClickAt < TAP_DRAG_MS) {
        g.afterTap = true;
        pendingClickAt = 0;
    }
}

void finish(uint32_t now) {
    bool tap = g.zone == Zone::Pad && now - g.downAt < TAP_MS && g.moved < TAP_MOVE_PX;
    if (g.secondHold) {
        hid::mouseButtons(hid::mouseButtonsState() & ~hid::BTN_LEFT);
    } else if (g.second) {
        // оба пальца поднялись разом — тоже клик
        hid::click(hid::BTN_LEFT);
    } else if (g.dragging) {
        hid::mouseButtons(hid::mouseButtonsState() & ~hid::BTN_LEFT);
    } else if (g.afterTap) {
        // тап, тап — двойной клик; тап и удержание без движения — одиночный клик
        hid::click(hid::BTN_LEFT);
        if (tap) hid::click(hid::BTN_LEFT);
    } else if (tap) {
        if (g.maxFingers >= 2)
            hid::click(hid::BTN_RIGHT);
        else
            pendingClickAt = now;
    }
    g.zone = Zone::None;
}

// курсор ведет палец, коснувшийся первым; второй — кнопка
void trackSecond(uint8_t n, const touch::Point* zp, uint32_t now) {
    // основной палец — ближайший к своей прошлой позиции (порядок точек у GT911 может меняться)
    const touch::Point* p = &zp[0];
    auto far = [](const touch::Point& q) { return std::fabs(q.x - g.lastX) + std::fabs(q.y - g.lastY); };
    if (n == 2 && far(zp[1]) < far(zp[0])) p = &zp[1];
    float dx = p->x - g.lastX, dy = p->y - g.lastY;
    g.lastX = p->x;
    g.lastY = p->y;
    if (n == 2) {
        if (!g.secondHold && now - g.secondAt >= TAP_MS) {
            g.secondHold = true;
            hid::mouseButtons(hid::mouseButtonsState() | hid::BTN_LEFT);
        }
        moveCursor(dx, dy);
        return;
    }
    // второй палец поднят
    if (g.secondHold)
        hid::mouseButtons(hid::mouseButtonsState() & ~hid::BTN_LEFT);
    else
        hid::click(hid::BTN_LEFT);
    g.second = g.secondHold = false;
    g.fingers = 1;
    // остался второй палец, а первый ушел — курсор не прыгает
    if (std::fabs(dx) + std::fabs(dy) < SECOND_JUMP_PX) moveCursor(dx, dy);
}

void track(uint8_t n, float x, float y, const touch::Point* zp, uint32_t now) {
    if (g.second) {
        trackSecond(n, zp, now);
        return;
    }
    // палец уже ведет курсор, второй коснулся — это кнопка, а не прокрутка
    bool cursorActive = g.moved >= TAP_MOVE_PX || now - g.downAt >= TAP_MS;
    if (g.zone == Zone::Pad && n == 2 && g.fingers == 1 && g.maxFingers == 1 && !g.afterTap && cursorActive) {
        g.second = true;
        g.secondAt = now;
        return;
    }
    // число пальцев изменилось — опорная точка прыгает, начинаем отсчет заново
    if (n != g.fingers) {
        g.fingers = n;
        g.maxFingers = std::max(g.maxFingers, n);
        g.lastX = x;
        g.lastY = y;
        return;
    }
    float dx = x - g.lastX, dy = y - g.lastY;
    g.lastX = x;
    g.lastY = y;
    g.moved += int(std::fabs(dx) + std::fabs(dy));
    if (g.zone == Zone::Strip) {
        scroll(0, dy);
        return;
    }
    if (n >= 2) {
        scroll(dx, dy);
        return;
    }
    // до первого сдвига двухпальцевого жеста курсор не трогаем
    if (g.maxFingers >= 2) return;
    if (g.afterTap && !g.dragging && g.moved >= TAP_MOVE_PX) {
        g.dragging = true;
        hid::mouseButtons(hid::mouseButtonsState() | hid::BTN_LEFT);
    }
    moveCursor(dx, dy);
}

lv_obj_t* makePanel(lv_obj_t* parent) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(o, theme::panel(), 0);
    lv_obj_set_style_border_color(o, lv_color_hex(0x30343C), 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_radius(o, 10, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    return o;
}

lv_obj_t* makeHint(lv_obj_t* parent, const char* text) {
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, theme::small(), 0);
    lv_obj_set_style_text_color(l, theme::muted(), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    return l;
}

lv_obj_t* makeButton(lv_obj_t* parent, const char* text) {
    lv_obj_t* o = makePanel(parent);
    lv_obj_set_style_bg_color(o, theme::key(), 0);
    lv_obj_set_style_bg_color(o, theme::keyPressed(), LV_STATE_PRESSED);
    lv_obj_set_style_radius(o, 6, 0);
    lv_obj_t* l = lv_label_create(o);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, theme::medium(), 0);
    lv_obj_set_style_text_color(l, theme::text(), 0);
    lv_obj_center(l);
    return o;
}

}  // namespace

void create(lv_obj_t* parent) {
    constexpr int STRIP_W = 56, BTN_H = 50, GAP = 4;
    lv_coord_t w = lv_obj_get_content_width(parent);
    lv_coord_t h = lv_obj_get_content_height(parent);

    pad = makePanel(parent);
    lv_obj_set_pos(pad, GAP, GAP);
    lv_obj_set_size(pad, w - STRIP_W - 3 * GAP, h - BTN_H - 3 * GAP);
    lv_obj_center(makeHint(pad,
                           "1 палец — курсор,  тап — клик\n"
                           "ведешь курсор + тап вторым — клик,\n"
                           "  держишь второй — перетаскивание\n"
                           "2 пальца — прокрутка,  тап двумя — правый клик"));

    strip = makePanel(parent);
    lv_obj_set_pos(strip, w - STRIP_W - GAP, GAP);
    lv_obj_set_size(strip, STRIP_W, h - BTN_H - 3 * GAP);
    lv_obj_t* up = makeHint(strip, LV_SYMBOL_UP);
    lv_obj_align(up, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_t* down = makeHint(strip, LV_SYMBOL_DOWN);
    lv_obj_align(down, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_center(makeHint(strip, "Про-\nкрут-\nка"));

    // ЛКМ шире: по ней чаще попадают вслепую
    lv_coord_t leftW = (w - 3 * GAP) * 2 / 3;
    btnLeft = makeButton(parent, "ЛКМ");
    lv_obj_set_pos(btnLeft, GAP, h - BTN_H - GAP);
    lv_obj_set_size(btnLeft, leftW, BTN_H);
    btnRight = makeButton(parent, "ПКМ");
    lv_obj_set_pos(btnRight, 2 * GAP + leftW, h - BTN_H - GAP);
    lv_obj_set_size(btnRight, w - 3 * GAP - leftW, BTN_H);
}

void loop() {
    uint32_t now = millis();
    touch::Point pts[2];
    uint8_t total = readTouches(pts);

    uint8_t held = 0;
    for (uint8_t i = 0; i < total; i++) {
        if (inside(btnLeft, pts[i].x, pts[i].y)) held |= hid::BTN_LEFT;
        if (inside(btnRight, pts[i].x, pts[i].y)) held |= hid::BTN_RIGHT;
    }
    setHeld(held);

    float x = 0, y = 0;
    uint8_t n = 0;
    touch::Point zp[2];
    if (g.zone == Zone::None) {
        if ((n = fingersIn(pad, pts, total, x, y, zp)))
            start(Zone::Pad, n, x, y, now);
        else if ((n = fingersIn(strip, pts, total, x, y, zp)))
            start(Zone::Strip, n, x, y, now);
    } else if (!(n = fingersIn(g.zone == Zone::Pad ? pad : strip, pts, total, x, y, zp))) {
        finish(now);
    } else {
        track(n, x, y, zp, now);
    }

    // тап без продолжения — обычный клик
    if (pendingClickAt && g.zone == Zone::None && now - pendingClickAt >= TAP_DRAG_MS) {
        pendingClickAt = 0;
        hid::click(hid::BTN_LEFT);
    }
}

void reset() {
    if (g.dragging || g.secondHold || heldButtons) hid::mouseButtons(0);
    heldButtons = 0;
    g = Gesture{};
    pendingClickAt = 0;
}

}  // namespace trackpad
