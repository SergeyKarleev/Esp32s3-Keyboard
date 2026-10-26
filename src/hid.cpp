#include "hid.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <USB.h>
#include <USBHIDConsumerControl.h>
#include <USBHIDKeyboard.h>
#include <USBHIDMouse.h>

#include <algorithm>

namespace hid {

namespace {

// Отчеты одинаковы для USB и BLE: клавиатура 8 байт, мышь 5 байт (кнопки, x, y, колесо, пан),
// consumer — 16-битный код.
struct MouseReport {
    uint8_t buttons;
    int8_t x, y, wheel, pan;
} __attribute__((packed));

// ---------- USB ----------

// объекты регистрируют свои дескрипторы в составном HID-интерфейсе TinyUSB
USBHIDKeyboard usbKeyboard;
USBHIDMouse usbMouse;
USBHIDConsumerControl usbConsumer;
USBHID usbHid;
volatile bool usbMounted = false;
// меняется при подключении, отключении, получении имени и смене цели
volatile uint32_t version = 0;

constexpr uint32_t USB_TIMEOUT_MS = 20;

void onUsbEvent(void*, esp_event_base_t base, int32_t id, void*) {
    if (base != ARDUINO_USB_EVENTS) return;
    bool was = usbMounted;
    if (id == ARDUINO_USB_STARTED_EVENT || id == ARDUINO_USB_RESUME_EVENT) usbMounted = true;
    if (id == ARDUINO_USB_STOPPED_EVENT || id == ARDUINO_USB_SUSPEND_EVENT) usbMounted = false;
    if (was != usbMounted) version++;
}

TargetKind targetKind_ = TargetKind::All;
char targetAddr_[18] = "";

void usbSend(uint8_t id, const void* data, size_t len) {
    if (targetKind_ == TargetKind::Ble || !usbMounted || !usbHid.ready()) return;
    usbHid.SendReport(id, data, len, USB_TIMEOUT_MS);
}

// ---------- BLE ----------

constexpr uint8_t BLE_ID_KEYBOARD = 1;
constexpr uint8_t BLE_ID_MOUSE = 2;
constexpr uint8_t BLE_ID_CONSUMER = 3;

const uint8_t reportMap[] = {
    // клавиатура
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, BLE_ID_KEYBOARD,
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
    0x95, 0x05, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02,
    0x95, 0x01, 0x75, 0x03, 0x91, 0x01,
    0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65, 0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00,
    0xC0,
    // мышь
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, BLE_ID_MOUSE, 0x09, 0x01, 0xA1, 0x00,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x05, 0x15, 0x00, 0x25, 0x01, 0x95, 0x05, 0x75, 0x01, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x03, 0x81, 0x01,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x03, 0x81, 0x06,
    0x05, 0x0C, 0x0A, 0x38, 0x02, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x01, 0x81, 0x06,
    0xC0, 0xC0,
    // мультимедиа
    0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, BLE_ID_CONSUMER,
    0x15, 0x00, 0x26, 0xFF, 0x03, 0x19, 0x00, 0x2A, 0xFF, 0x03, 0x75, 0x10, 0x95, 0x01, 0x81, 0x00,
    0xC0,
};

NimBLEServer* server = nullptr;
NimBLECharacteristic* bleKeyboard = nullptr;
NimBLECharacteristic* bleMouse = nullptr;
NimBLECharacteristic* bleConsumer = nullptr;
volatile bool bleEnabled = true;

// подключенные хосты; меняются в задаче NimBLE, читаются в loop()
constexpr size_t MAX_PEERS = CONFIG_BT_NIMBLE_MAX_CONNECTIONS;
struct Peer {
    bool used = false;
    uint16_t handle = 0;
    char addr[18] = "";
    char name[32] = "";
    bool nameAsked = false;
    bool encrypted = false;
};
Peer peers[MAX_PEERS];
portMUX_TYPE peersLock = portMUX_INITIALIZER_UNLOCKED;
// имя хоста читается из его GATT (Generic Access, Device Name) после шифрования связи
constexpr uint32_t NAME_TASK_STACK = 4096;

size_t connectedCount() {
    size_t n = 0;
    for (const Peer& p : peers) n += p.used;
    return n;
}

void advertiseIfFree() {
    if (bleEnabled && connectedCount() < MAX_PEERS) NimBLEDevice::startAdvertising();
}

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* s, NimBLEConnInfo& info) override {
        taskENTER_CRITICAL(&peersLock);
        for (Peer& p : peers) {
            if (p.used) continue;
            p = Peer{};
            p.used = true;
            p.handle = info.getConnHandle();
            strlcpy(p.addr, info.getIdAddress().toString().c_str(), sizeof p.addr);
            break;
        }
        taskEXIT_CRITICAL(&peersLock);
        version++;
        Serial.printf("[ble] connected %s\n", info.getAddress().toString().c_str());
        // короткий интервал связи: курсор без рывков
        s->updateConnParams(info.getConnHandle(), 6, 12, 0, 300);
        // остальные хосты тоже могут подключиться
        advertiseIfFree();
    }
    void onDisconnect(NimBLEServer*, NimBLEConnInfo& info, int reason) override {
        taskENTER_CRITICAL(&peersLock);
        for (Peer& p : peers)
            if (p.used && p.handle == info.getConnHandle()) p.used = false;
        taskEXIT_CRITICAL(&peersLock);
        version++;
        Serial.printf("[ble] disconnected, reason 0x%X\n", reason);
        advertiseIfFree();
    }
    void onAuthenticationComplete(NimBLEConnInfo& info) override {
        Serial.printf("[ble] auth %s, bonded %d\n", info.isEncrypted() ? "encrypted" : "FAILED", info.isBonded());
        // после сопряжения известен постоянный адрес хоста (вместо случайного)
        taskENTER_CRITICAL(&peersLock);
        for (Peer& p : peers) {
            if (!p.used || p.handle != info.getConnHandle()) continue;
            strlcpy(p.addr, info.getIdAddress().toString().c_str(), sizeof p.addr);
            p.encrypted = info.isEncrypted();
        }
        taskEXIT_CRITICAL(&peersLock);
        version++;
    }
};

void readNameTask(void* arg) {
    uint16_t handle = uint16_t(uintptr_t(arg));
    std::string name;
    NimBLEClient* client = server ? server->getClient(handle) : nullptr;
    if (client) {
        NimBLERemoteService* svc = client->getService(NimBLEUUID(uint16_t(0x1800)));
        NimBLERemoteCharacteristic* chr = svc ? svc->getCharacteristic(NimBLEUUID(uint16_t(0x2A00))) : nullptr;
        if (chr && chr->canRead()) name = chr->readValue();
    }
    Serial.printf("[ble] peer %u name '%s'\n", handle, name.c_str());
    taskENTER_CRITICAL(&peersLock);
    for (Peer& p : peers)
        if (p.used && p.handle == handle && !name.empty()) strlcpy(p.name, name.c_str(), sizeof p.name);
    taskEXIT_CRITICAL(&peersLock);
    version++;
    vTaskDelete(nullptr);
}

// хост выбран целью и подключен: его conn handle
bool targetHandle(uint16_t& handle) {
    bool found = false;
    taskENTER_CRITICAL(&peersLock);
    for (const Peer& p : peers)
        if (p.used && !strcmp(p.addr, targetAddr_)) {
            handle = p.handle;
            found = true;
        }
    taskEXIT_CRITICAL(&peersLock);
    return found;
}
ServerCallbacks serverCallbacks;

// буферы стека заняты (много отчетов подряд) — notify отказывает; ждем и повторяем,
// иначе теряется нажатие или отпускание клавиши
constexpr int NOTIFY_RETRIES = 20;

bool bleActive() {
    uint16_t h;
    if (targetKind_ == TargetKind::Ble) return targetHandle(h);
    return targetKind_ == TargetKind::All && connectedCount() > 0;
}

void bleSend(NimBLECharacteristic* chr, const void* data, size_t len) {
    if (!chr || targetKind_ == TargetKind::Usb || !connectedCount()) return;
    // всем подписанным хостам или только выбранному
    uint16_t handle = BLE_HS_CONN_HANDLE_NONE;
    if (targetKind_ == TargetKind::Ble && !targetHandle(handle)) return;
    for (int i = 0; i < NOTIFY_RETRIES; i++) {
        if (chr->notify(static_cast<const uint8_t*>(data), len, handle)) return;
        delay(2);
    }
    Serial.println("[ble] notify failed");
}

// Android склеивает или теряет отчеты клавиатуры, пришедшие в одном интервале связи
constexpr uint32_t BLE_KEY_GAP_MS = 8;

void bleBegin(const char* name) {
    NimBLEDevice::init(name);
    NimBLEDevice::setPower(9);
    // сопряжение без ввода кода (Just Works) с сохранением ключей
    NimBLEDevice::setSecurityAuth(true, false, true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

    server = NimBLEDevice::createServer();
    server->setCallbacks(&serverCallbacks, false);
    server->advertiseOnDisconnect(false);

    auto* dev = new NimBLEHIDDevice(server);
    dev->setManufacturer("EspKeyboard");
    dev->setPnp(0x02, 0x303A, 0x4002, 0x0100);
    dev->setHidInfo(0x00, 0x01);
    dev->setReportMap(const_cast<uint8_t*>(reportMap), sizeof reportMap);
    bleKeyboard = dev->getInputReport(BLE_ID_KEYBOARD);
    dev->getOutputReport(BLE_ID_KEYBOARD);
    bleMouse = dev->getInputReport(BLE_ID_MOUSE);
    bleConsumer = dev->getInputReport(BLE_ID_CONSUMER);
    dev->setBatteryLevel(100);
    server->start();

    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->setAppearance(0x03C1);  // HID keyboard
    adv->addServiceUUID(dev->getHidService()->getUUID());
    adv->setName(name);
    adv->enableScanResponse(true);
    if (bleEnabled) adv->start();
}

// ---------- общий вывод ----------

uint8_t buttons = 0;
int accX = 0, accY = 0, accWheel = 0, accPan = 0;
uint32_t lastMouse = 0;
constexpr uint32_t MOUSE_PERIOD_MS = 12;

int8_t takeChunk(int& acc) {
    int v = std::clamp(acc, -127, 127);
    acc -= v;
    return int8_t(v);
}

void sendMouse() {
    lastMouse = millis();
    do {
        MouseReport r{buttons, takeChunk(accX), takeChunk(accY), takeChunk(accWheel), takeChunk(accPan)};
        usbSend(HID_REPORT_ID_MOUSE, &r, sizeof r);
        bleSend(bleMouse, &r, sizeof r);
    } while (accX || accY || accWheel || accPan);
}

}  // namespace

void begin(const char* name, bool bluetooth) {
    bleEnabled = bluetooth;
    USB.onEvent(onUsbEvent);
    usbKeyboard.begin();
    usbMouse.begin();
    usbConsumer.begin();
    USB.begin();
    bleBegin(name);
}

void poll() {
    // имя нового хоста: чтение GATT блокирует, поэтому в отдельной задаче
    for (Peer& p : peers) {
        if (!p.used || !p.encrypted || p.nameAsked) continue;
        p.nameAsked = true;
        xTaskCreate(readNameTask, "blename", NAME_TASK_STACK, reinterpret_cast<void*>(uintptr_t(p.handle)), 1, nullptr);
    }
    if ((accX || accY || accWheel || accPan) && millis() - lastMouse >= MOUSE_PERIOD_MS) sendMouse();
}

void keyState(uint8_t mods, uint8_t code) {
    uint8_t r[8] = {mods, 0, code, 0, 0, 0, 0, 0};
    usbSend(HID_REPORT_ID_KEYBOARD, r, sizeof r);
    bleSend(bleKeyboard, r, sizeof r);
    if (bleActive()) delay(BLE_KEY_GAP_MS);
}

void tapKey(uint8_t mods, uint8_t code) {
    keyState(mods, code);
    keyState(mods, 0);
    if (mods) keyState(0, 0);
}

void chord(uint8_t mods, uint8_t code) {
    // Alt+Shift и подобные срабатывают на отпускании: модификаторы нажимаем по одному
    uint8_t held = 0;
    for (uint8_t bit = 1; bit; bit <<= 1) {
        if (!(mods & bit)) continue;
        held |= bit;
        keyState(held, 0);
        delay(10);
    }
    if (code) {
        keyState(held, code);
        delay(10);
        keyState(held, 0);
        delay(10);
    }
    keyState(0, 0);
}

void mouseMove(int dx, int dy) {
    accX += dx;
    accY += dy;
}

void mouseScroll(int wheel, int pan) {
    accWheel += wheel;
    accPan += pan;
}

void mouseButtons(uint8_t b) {
    if (b == buttons) return;
    buttons = b;
    sendMouse();
}

uint8_t mouseButtonsState() { return buttons; }

void click(uint8_t button) {
    mouseButtons(buttons | button);
    delay(15);
    mouseButtons(buttons & ~button);
}

void consumer(uint16_t usage) {
    uint16_t zero = 0;
    usbSend(HID_REPORT_ID_CONSUMER_CONTROL, &usage, 2);
    bleSend(bleConsumer, &usage, 2);
    delay(15);
    usbSend(HID_REPORT_ID_CONSUMER_CONTROL, &zero, 2);
    bleSend(bleConsumer, &zero, 2);
}

void setTarget(TargetKind kind, const char* addr) {
    // отпустить все на устройстве, которое перестает получать отчеты
    uint8_t zero[8] = {};
    MouseReport idle{};
    buttons = 0;
    usbSend(HID_REPORT_ID_KEYBOARD, zero, sizeof zero);
    bleSend(bleKeyboard, zero, sizeof zero);
    usbSend(HID_REPORT_ID_MOUSE, &idle, sizeof idle);
    bleSend(bleMouse, &idle, sizeof idle);
    targetKind_ = kind;
    strlcpy(targetAddr_, kind == TargetKind::Ble ? addr : "", sizeof targetAddr_);
    version++;
}

TargetKind targetKind() { return targetKind_; }

const char* targetAddr() { return targetAddr_; }

size_t devices(Device* out, size_t maxOut) {
    size_t n = 0;
    if (usbMounted && n < maxOut) out[n++] = {TargetKind::Usb, "", "Компьютер (USB)", true};
    bool targetListed = false;
    taskENTER_CRITICAL(&peersLock);
    for (const Peer& p : peers) {
        if (!p.used || n == maxOut) continue;
        Device& d = out[n++];
        d = {TargetKind::Ble, "", "", true};
        strlcpy(d.addr, p.addr, sizeof d.addr);
        strlcpy(d.name, p.name[0] ? p.name : p.addr, sizeof d.name);
        targetListed |= !strcmp(p.addr, targetAddr_);
    }
    taskEXIT_CRITICAL(&peersLock);
    // выбранное, но отключенное устройство — чтобы было видно, куда сейчас ничего не уходит
    if (targetKind_ == TargetKind::Ble && !targetListed && n < maxOut) {
        out[n] = {TargetKind::Ble, "", "", false};
        strlcpy(out[n].addr, targetAddr_, sizeof out[n].addr);
        strlcpy(out[n].name, targetAddr_, sizeof out[n].name);
        n++;
    }
    if (targetKind_ == TargetKind::Usb && !usbMounted && n < maxOut) out[n++] = {TargetKind::Usb, "", "Компьютер (USB)", false};
    return n;
}

uint32_t devicesVersion() { return version; }

bool usbConnected() { return usbMounted; }

BleState bleState() {
    if (connectedCount()) return BleState::Connected;
    return NimBLEDevice::getAdvertising()->isAdvertising() ? BleState::Advertising : BleState::Off;
}

void setBluetooth(bool on) {
    bleEnabled = on;
    if (on) {
        NimBLEDevice::startAdvertising();
        return;
    }
    NimBLEDevice::stopAdvertising();
    if (server)
        for (uint16_t h : server->getPeerDevices()) server->disconnect(h);
}

void forgetBonds() {
    if (server)
        for (uint16_t h : server->getPeerDevices()) server->disconnect(h);
    NimBLEDevice::deleteAllBonds();
    Serial.println("[ble] bonds deleted");
}

}  // namespace hid
