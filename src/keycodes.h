#pragma once
// Коды клавиш HID (Keyboard/Keypad Page 0x07), только нужные не-буквенные.

#include <stdint.h>

constexpr uint8_t HK_ENTER = 0x28;
constexpr uint8_t HK_ESC = 0x29;
constexpr uint8_t HK_BACKSPACE = 0x2A;
constexpr uint8_t HK_TAB = 0x2B;
constexpr uint8_t HK_SPACE = 0x2C;
constexpr uint8_t HK_CAPS_LOCK = 0x39;
constexpr uint8_t HK_F1 = 0x3A;
constexpr uint8_t HK_INSERT = 0x49;
constexpr uint8_t HK_HOME = 0x4A;
constexpr uint8_t HK_PAGE_UP = 0x4B;
constexpr uint8_t HK_DELETE = 0x4C;
constexpr uint8_t HK_END = 0x4D;
constexpr uint8_t HK_PAGE_DOWN = 0x4E;
constexpr uint8_t HK_RIGHT = 0x4F;
constexpr uint8_t HK_LEFT = 0x50;
constexpr uint8_t HK_DOWN = 0x51;
constexpr uint8_t HK_UP = 0x52;
