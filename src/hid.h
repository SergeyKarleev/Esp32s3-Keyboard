#pragma once
// Вывод HID в USB (TinyUSB) и Bluetooth LE (HID over GATT, до трех хостов сразу).
// Отчеты уходят во все подключенные устройства или только в выбранное.

#include <stddef.h>
#include <stdint.h>

namespace hid {

// модификаторы клавиатуры (байт 0 отчета)
constexpr uint8_t MOD_CTRL = 0x01;
constexpr uint8_t MOD_SHIFT = 0x02;
constexpr uint8_t MOD_ALT = 0x04;
constexpr uint8_t MOD_GUI = 0x08;

// кнопки мыши
constexpr uint8_t BTN_LEFT = 0x01;
constexpr uint8_t BTN_RIGHT = 0x02;
constexpr uint8_t BTN_MIDDLE = 0x04;

// Consumer Page (0x0C)
constexpr uint16_t CC_PLAY_PAUSE = 0x00CD;
constexpr uint16_t CC_NEXT = 0x00B5;
constexpr uint16_t CC_PREV = 0x00B6;
constexpr uint16_t CC_MUTE = 0x00E2;
constexpr uint16_t CC_VOL_UP = 0x00E9;
constexpr uint16_t CC_VOL_DOWN = 0x00EA;
constexpr uint16_t CC_HOME = 0x0223;
constexpr uint16_t CC_BACK = 0x0224;

enum class BleState { Off, Advertising, Connected };
// куда отправлять отчеты: всем, USB-хосту или одному Bluetooth-хосту (по адресу)
enum class TargetKind : uint8_t { All, Usb, Ble };

// подключенное устройство (или выбранное, но сейчас отключенное)
struct Device {
    TargetKind kind;
    char addr[18];  // адрес Bluetooth, у USB пусто
    char name[32];
    bool connected;
};

// name — имя Bluetooth-устройства; bluetooth — сразу начать рекламу BLE
void begin(const char* name, bool bluetooth);
// вызывать из loop(): отправляет накопленное движение мыши
void poll();

// состояние клавиатуры: модификаторы + одна клавиша (0 — нет)
void keyState(uint8_t mods, uint8_t code);
// нажать и отпустить клавишу
void tapKey(uint8_t mods, uint8_t code);
// комбинация с модификаторами: сначала модификаторы, потом клавиша, отпускание в обратном порядке
void chord(uint8_t mods, uint8_t code);

void mouseMove(int dx, int dy);
void mouseScroll(int wheel, int pan);
void mouseButtons(uint8_t buttons);
uint8_t mouseButtonsState();
void click(uint8_t button);

// нажать и отпустить мультимедийную клавишу
void consumer(uint16_t usage);

void setTarget(TargetKind kind, const char* addr = "");
TargetKind targetKind();
const char* targetAddr();
// устройства для выбора: USB (если подключен), Bluetooth-хосты и выбранное, даже если отключено
size_t devices(Device* out, size_t maxOut);
// меняется при подключении, отключении и получении имени устройства
uint32_t devicesVersion();
// выбранный получатель (или основной при выводе «везде») — Apple
bool hostIsMac();
// отладка: запросы строк USB-дескрипторов при подключении
void dumpUsbRequests();

bool usbConnected();
BleState bleState();
// включить/выключить Bluetooth: реклама и текущее соединение
void setBluetooth(bool on);
// забыть все сопряженные устройства и разорвать связь
void forgetBonds();

}  // namespace hid
