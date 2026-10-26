#pragma once
// Цвета и шрифты интерфейса.

#include <lvgl.h>

namespace theme {

inline lv_color_t bg() { return lv_color_hex(0x0E0F12); }
inline lv_color_t panel() { return lv_color_hex(0x1A1C21); }
inline lv_color_t key() { return lv_color_hex(0x2B2F36); }
inline lv_color_t keySpecial() { return lv_color_hex(0x1F2228); }
inline lv_color_t keyPressed() { return lv_color_hex(0x4A5160); }
inline lv_color_t accent() { return lv_color_hex(0x3B82F6); }
inline lv_color_t ok() { return lv_color_hex(0x22C55E); }
inline lv_color_t muted() { return lv_color_hex(0x6B7280); }
inline lv_color_t text() { return lv_color_hex(0xE5E7EB); }

// Roboto с кириллицей + значки LV_SYMBOL_* из Montserrat (запасной шрифт)
void initFonts();
const lv_font_t* small();   // 14
const lv_font_t* medium();  // 18
const lv_font_t* large();   // 22

}  // namespace theme
