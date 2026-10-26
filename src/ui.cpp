#include "ui.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <lvgl.h>

#include "board.h"
#include "hid.h"
#include "keyboard.h"
#include "settings.h"
#include "swipe.h"
#include "theme.h"
#include "trackpad.h"

namespace ui {

namespace {

constexpr int BAR_H = 32;

enum class Mode : uint8_t { Keyboard, Trackpad };
Mode mode = Mode::Keyboard;

lv_obj_t* tabKeyboard = nullptr;
lv_obj_t* tabTrackpad = nullptr;
lv_obj_t* pageKeyboard = nullptr;
lv_obj_t* pageTrackpad = nullptr;
lv_obj_t* usbLabel = nullptr;
lv_obj_t* bleLabel = nullptr;
lv_obj_t* settingsPanel = nullptr;
// середина панели: быстрые действия или варианты слова после свайпа
lv_obj_t* actionsBox = nullptr;
lv_obj_t* suggestBox = nullptr;
// подсказка режима тренировки вместо быстрых действий; тап — выход из тренировки
lv_obj_t* promptBox = nullptr;
lv_obj_t* promptLabel = nullptr;
lv_obj_t* btSwitch = nullptr;
lv_obj_t* deviceList = nullptr;

void setMode(Mode m) {
    mode = m;
    bool kb = m == Mode::Keyboard;
    if (kb) {
        lv_obj_clear_flag(pageKeyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(pageTrackpad, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_state(tabKeyboard, LV_STATE_CHECKED);
        lv_obj_clear_state(tabTrackpad, LV_STATE_CHECKED);
        trackpad::reset();
    } else {
        lv_obj_add_flag(pageKeyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(pageTrackpad, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_state(tabTrackpad, LV_STATE_CHECKED);
        lv_obj_clear_state(tabKeyboard, LV_STATE_CHECKED);
    }
}

lv_obj_t* makeBarButton(lv_obj_t* parent, const char* text, lv_event_cb_t cb, void* user = nullptr) {
    lv_obj_t* b = lv_btn_create(parent);
    lv_obj_set_height(b, BAR_H - 6);
    lv_obj_set_style_pad_hor(b, 10, 0);
    lv_obj_set_style_pad_ver(b, 0, 0);
    lv_obj_set_style_radius(b, 6, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, theme::key(), 0);
    lv_obj_set_style_bg_color(b, theme::accent(), LV_STATE_CHECKED);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, theme::small(), 0);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    return b;
}

// значок тачпада: в шрифте символов такого нет — рамка поля и две кнопки снизу
void addTouchpadIcon(lv_obj_t* btn) {
    lv_obj_t* l = lv_obj_get_child(btn, 0);
    lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t* frame = lv_obj_create(btn);
    lv_obj_remove_style_all(frame);
    lv_obj_set_size(frame, 20, 16);
    lv_obj_center(frame);
    lv_obj_clear_flag(frame, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(frame, 2, 0);
    lv_obj_set_style_border_color(frame, theme::text(), 0);
    lv_obj_set_style_radius(frame, 3, 0);
    for (int i = 0; i < 2; i++) {
        lv_obj_t* key = lv_obj_create(frame);
        lv_obj_remove_style_all(key);
        lv_obj_clear_flag(key, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(key, 7, 3);
        lv_obj_set_style_bg_color(key, theme::text(), 0);
        lv_obj_set_style_bg_opa(key, LV_OPA_COVER, 0);
        lv_obj_align(key, LV_ALIGN_BOTTOM_LEFT, 2 + i * 8, -2);
    }
}

lv_obj_t* makeBarLabel(lv_obj_t* parent, const char* text) {
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, theme::small(), 0);
    lv_obj_set_style_text_color(l, theme::muted(), 0);
    return l;
}

// ---------- выбор устройства ----------

constexpr size_t MAX_DEVICES = 6;
hid::Device listed[MAX_DEVICES];
size_t listedCount = 0;
uint32_t listedVersion = UINT32_MAX;

bool isTarget(const hid::Device& d) {
    hid::TargetKind k = hid::targetKind();
    return d.kind == k && (k != hid::TargetKind::Ble || !strcmp(d.addr, hid::targetAddr()));
}

void onDevice(lv_event_t* e) {
    auto i = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
    // 0 — «Везде», дальше устройства из списка
    hid::TargetKind kind = i == 0 ? hid::TargetKind::All : listed[i - 1].kind;
    const char* addr = i == 0 ? "" : listed[i - 1].addr;
    hid::setTarget(kind, addr);
    settings::get().target = uint8_t(kind);
    strlcpy(settings::get().targetAddr, hid::targetAddr(), sizeof settings::get().targetAddr);
    settings::save();
}

void addDeviceButton(const char* text, bool checked, uintptr_t index) {
    lv_obj_t* b = makeBarButton(deviceList, text, onDevice, reinterpret_cast<void*>(index));
    lv_obj_set_height(b, 34);
    lv_obj_t* l = lv_obj_get_child(b, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_style_max_width(l, 200, 0);
    if (checked) lv_obj_add_state(b, LV_STATE_CHECKED);
}

// список пересобирается, когда устройства подключаются, отключаются или выбор меняется
void rebuildDevices() {
    if (!deviceList || listedVersion == hid::devicesVersion()) return;
    listedVersion = hid::devicesVersion();
    listedCount = hid::devices(listed, MAX_DEVICES);
    lv_obj_clean(deviceList);
    addDeviceButton("Везде", hid::targetKind() == hid::TargetKind::All, 0);
    for (size_t i = 0; i < listedCount; i++) {
        const hid::Device& d = listed[i];
        char text[64];
        snprintf(text, sizeof text, "%s %s%s", d.kind == hid::TargetKind::Usb ? LV_SYMBOL_USB : LV_SYMBOL_BLUETOOTH, d.name,
                 d.connected ? "" : " (нет связи)");
        addDeviceButton(text, isTarget(d), i + 1);
    }
}

void updateStatus(lv_timer_t* = nullptr) {
    static uint32_t blink = 0;
    blink++;
    rebuildDevices();
    // канал, в который нажатия не идут, не подсвечиваем
    hid::TargetKind target = hid::targetKind();
    lv_obj_set_style_text_color(usbLabel,
                                hid::usbConnected() && target != hid::TargetKind::Ble ? theme::ok() : theme::muted(), 0);
    if (target == hid::TargetKind::Usb) {
        lv_obj_set_style_text_color(bleLabel, theme::muted(), 0);
        return;
    }
    switch (hid::bleState()) {
        case hid::BleState::Connected: lv_obj_set_style_text_color(bleLabel, theme::ok(), 0); break;
        // ожидание подключения: мигает
        case hid::BleState::Advertising:
            lv_obj_set_style_text_color(bleLabel, blink & 1 ? theme::accent() : theme::muted(), 0);
            break;
        default: lv_obj_set_style_text_color(bleLabel, theme::muted(), 0); break;
    }
}

// ---------- быстрые действия и подсказки ----------

struct QuickAction {
    const char* label;
    uint8_t code;  // клавиша вместе с Ctrl (или Cmd на Mac)
};
const QuickAction QUICK[] = {
    {"Всё", 0x04},             // выделить все
    {LV_SYMBOL_CUT, 0x1B},     // вырезать
    {LV_SYMBOL_COPY, 0x06},    // копировать
    {LV_SYMBOL_PASTE, 0x19},   // вставить
    {LV_SYMBOL_LOOP, 0x1D},    // отменить
};

void onQuick(lv_event_t* e) {
    auto i = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
    hid::chord(settings::isMac() ? hid::MOD_GUI : hid::MOD_CTRL, QUICK[i].code);
}

void onSuggestion(lv_event_t* e) { keyboard::pickSuggestion(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e))); }

lv_obj_t* makeBox(lv_obj_t* parent) {
    lv_obj_t* box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_height(box, BAR_H);
    lv_obj_set_flex_grow(box, 1);
    lv_obj_set_style_pad_column(box, 4, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    return box;
}

void showSuggestions(const char* const* words, size_t n) {
    lv_obj_clean(suggestBox);
    // words[0] уже набрано — показываем остальные варианты
    for (size_t i = 1; i < n; i++) {
        lv_obj_t* b = makeBarButton(suggestBox, words[i], onSuggestion, reinterpret_cast<void*>(uintptr_t(i)));
        lv_obj_set_style_pad_hor(b, 6, 0);
    }
    bool show = n > 1;
    if (show) {
        lv_obj_add_flag(actionsBox, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(suggestBox, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(actionsBox, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(suggestBox, LV_OBJ_FLAG_HIDDEN);
    }
}

void showPrompt(const char* text) {
    if (text) {
        lv_label_set_text(promptLabel, text);
        lv_obj_clear_flag(promptBox, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(actionsBox, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(suggestBox, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_add_flag(promptBox, LV_OBJ_FLAG_HIDDEN);
    showSuggestions(nullptr, 0);
}

void setBluetooth(bool on) {
    settings::get().bluetooth = on;
    settings::save();
    hid::setBluetooth(on);
    if (on)
        lv_obj_add_state(btSwitch, LV_STATE_CHECKED);
    else
        lv_obj_clear_state(btSwitch, LV_STATE_CHECKED);
    updateStatus();
}

// ---------- настройки ----------

lv_obj_t* settingsRow(lv_obj_t* parent, const char* title) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t* l = lv_label_create(row);
    lv_label_set_text(l, title);
    lv_obj_set_style_text_font(l, theme::medium(), 0);
    return row;
}

void closeSettings(lv_event_t*) {
    settings::save();
    lv_obj_add_flag(settingsPanel, LV_OBJ_FLAG_HIDDEN);
}

void openSettings(lv_event_t*) { lv_obj_clear_flag(settingsPanel, LV_OBJ_FLAG_HIDDEN); }

void createSettings() {
    settingsPanel = lv_obj_create(lv_layer_top());
    lv_obj_set_size(settingsPanel, LCD_W, LCD_H);
    lv_obj_set_style_bg_color(settingsPanel, theme::bg(), 0);
    lv_obj_set_style_border_width(settingsPanel, 0, 0);
    lv_obj_set_style_radius(settingsPanel, 0, 0);
    lv_obj_set_style_pad_all(settingsPanel, 10, 0);
    lv_obj_set_style_pad_row(settingsPanel, 6, 0);
    lv_obj_set_flex_flow(settingsPanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(settingsPanel, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* head = settingsRow(settingsPanel, "Настройки");
    lv_obj_set_style_text_font(lv_obj_get_child(head, 0), theme::large(), 0);
    lv_obj_t* close = makeBarButton(head, LV_SYMBOL_OK " Готово", closeSettings);
    lv_obj_set_height(close, 34);

    lv_obj_t* row = settingsRow(settingsPanel, "Смена языка на ТВ/ПК");
    lv_obj_t* dd = lv_dropdown_create(row);
    lv_dropdown_set_options_static(dd, settings::hotkeyOptions());
    lv_dropdown_set_selected(dd, settings::get().hotkey);
    lv_obj_set_width(dd, 230);
    lv_obj_set_style_text_font(dd, theme::small(), 0);
    lv_obj_set_style_text_font(lv_dropdown_get_list(dd), theme::medium(), 0);
    lv_obj_add_event_cb(
        dd, [](lv_event_t* e) { settings::get().hotkey = lv_dropdown_get_selected(lv_event_get_target(e)); },
        LV_EVENT_VALUE_CHANGED, nullptr);

    lv_obj_t* title = lv_label_create(settingsPanel);
    lv_label_set_text(title, "Куда печатать");
    lv_obj_set_style_text_font(title, theme::medium(), 0);
    deviceList = lv_obj_create(settingsPanel);
    lv_obj_remove_style_all(deviceList);
    lv_obj_set_size(deviceList, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(deviceList, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(deviceList, 6, 0);
    lv_obj_set_style_pad_row(deviceList, 6, 0);
    rebuildDevices();

    row = settingsRow(settingsPanel, "Скорость курсора");
    lv_obj_t* slider = lv_slider_create(row);
    lv_slider_set_range(slider, 1, 10);
    lv_slider_set_value(slider, settings::get().speed, LV_ANIM_OFF);
    lv_obj_set_width(slider, 210);
    lv_obj_set_style_pad_right(row, 12, 0);
    lv_obj_add_event_cb(
        slider, [](lv_event_t* e) { settings::get().speed = lv_slider_get_value(lv_event_get_target(e)); },
        LV_EVENT_VALUE_CHANGED, nullptr);

    row = settingsRow(settingsPanel, "Натуральная прокрутка");
    lv_obj_t* sw = lv_switch_create(row);
    if (settings::get().naturalScroll) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(
        sw,
        [](lv_event_t* e) {
            settings::get().naturalScroll = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
        },
        LV_EVENT_VALUE_CHANGED, nullptr);

    row = settingsRow(settingsPanel, "Свайп сразу на EN и RU");
    sw = lv_switch_create(row);
    if (settings::get().bilingual) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(
        sw,
        [](lv_event_t* e) {
            settings::get().bilingual = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
            keyboard::refresh();
        },
        LV_EVENT_VALUE_CHANGED, nullptr);

    row = settingsRow(settingsPanel, "Автоисправление");
    sw = lv_switch_create(row);
    if (settings::get().autocorrect) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(
        sw, [](lv_event_t* e) { settings::get().autocorrect = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED); },
        LV_EVENT_VALUE_CHANGED, nullptr);

    // Mac: копировать/вставить через Cmd, знаки по русской раскладке macOS
    row = settingsRow(settingsPanel, "Система получателя");
    dd = lv_dropdown_create(row);
    lv_dropdown_set_options_static(dd, settings::hostOsOptions());
    lv_dropdown_set_selected(dd, settings::get().hostOs);
    lv_obj_set_width(dd, 200);
    lv_obj_set_style_text_font(dd, theme::small(), 0);
    lv_obj_set_style_text_font(lv_dropdown_get_list(dd), theme::medium(), 0);
    lv_obj_add_event_cb(
        dd, [](lv_event_t* e) { settings::get().hostOs = lv_dropdown_get_selected(lv_event_get_target(e)); },
        LV_EVENT_VALUE_CHANGED, nullptr);

    row = settingsRow(settingsPanel, "Bluetooth");
    btSwitch = lv_switch_create(row);
    if (settings::get().bluetooth) lv_obj_add_state(btSwitch, LV_STATE_CHECKED);
    lv_obj_add_event_cb(
        btSwitch, [](lv_event_t* e) { setBluetooth(lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED)); },
        LV_EVENT_VALUE_CHANGED, nullptr);

    row = settingsRow(settingsPanel, "Сопряженные устройства");
    lv_obj_t* forget = makeBarButton(row, LV_SYMBOL_TRASH " Забыть устройства", [](lv_event_t*) { hid::forgetBonds(); });
    lv_obj_set_height(forget, 34);

    row = settingsRow(settingsPanel, "Свайп под тебя");
    lv_obj_t* box = lv_obj_create(row);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(box, 6, 0);
    lv_obj_t* train = makeBarButton(box, LV_SYMBOL_EDIT " Тренировка", [](lv_event_t*) {
        closeSettings(nullptr);
        setMode(Mode::Keyboard);
        keyboard::startTraining();
    });
    lv_obj_set_height(train, 34);
    // забыть выученные слова и калибровку
    lv_obj_t* reset = makeBarButton(box, LV_SYMBOL_TRASH " Сброс", [](lv_event_t*) {
        swipe::forget();
        swipe::resetCalibration();
    });
    lv_obj_set_height(reset, 34);

    lv_obj_t* info = lv_label_create(settingsPanel);
    lv_label_set_text_fmt(info,
                          "Имя: ESP32 Keyboard,  MAC %s\n"
                          "Долгое нажатие EN/RU — сменить подписи без отправки комбинации.",
                          NimBLEDevice::getAddress().toString().c_str());
    lv_obj_set_style_text_font(info, theme::small(), 0);
    lv_obj_set_style_text_color(info, theme::muted(), 0);
}

}  // namespace

void create() {
    theme::initFonts();
    lv_obj_t* scr = lv_scr_act();
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, theme::bg(), 0);
    lv_obj_set_style_text_color(scr, theme::text(), 0);
    lv_obj_set_style_text_font(scr, theme::medium(), 0);

    lv_obj_t* bar = lv_obj_create(scr);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, LCD_W, BAR_H);
    lv_obj_set_style_bg_color(bar, theme::panel(), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(bar, 4, 0);
    lv_obj_set_style_pad_column(bar, 6, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    tabKeyboard = makeBarButton(bar, LV_SYMBOL_KEYBOARD, [](lv_event_t*) { setMode(Mode::Keyboard); });
    tabTrackpad = makeBarButton(bar, LV_SYMBOL_KEYBOARD, [](lv_event_t*) { setMode(Mode::Trackpad); });
    addTouchpadIcon(tabTrackpad);

    actionsBox = makeBox(bar);
    for (size_t i = 0; i < sizeof QUICK / sizeof QUICK[0]; i++) {
        lv_obj_t* b = makeBarButton(actionsBox, QUICK[i].label, onQuick, reinterpret_cast<void*>(uintptr_t(i)));
        lv_obj_set_style_pad_hor(b, 8, 0);
        lv_obj_set_style_bg_color(b, theme::keySpecial(), 0);
    }
    suggestBox = makeBox(bar);
    lv_obj_add_flag(suggestBox, LV_OBJ_FLAG_HIDDEN);
    keyboard::onSuggestions(showSuggestions);
    promptBox = makeBox(bar);
    lv_obj_add_flag(promptBox, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(promptBox, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        promptBox, [](lv_event_t*) { keyboard::stopTraining(); }, LV_EVENT_CLICKED, nullptr);
    promptLabel = lv_label_create(promptBox);
    lv_obj_set_style_text_font(promptLabel, theme::medium(), 0);
    lv_obj_set_style_text_color(promptLabel, theme::accent(), 0);
    keyboard::onPrompt(showPrompt);

    usbLabel = makeBarLabel(bar, LV_SYMBOL_USB);
    bleLabel = makeBarLabel(bar, LV_SYMBOL_BLUETOOTH);
    // тап по значку BT — включить/выключить Bluetooth
    lv_obj_add_flag(bleLabel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(bleLabel, 8);
    lv_obj_add_event_cb(
        bleLabel, [](lv_event_t*) { setBluetooth(!settings::get().bluetooth); }, LV_EVENT_CLICKED, nullptr);
    makeBarButton(bar, LV_SYMBOL_SETTINGS, openSettings);

    pageKeyboard = lv_obj_create(scr);
    lv_obj_remove_style_all(pageKeyboard);
    lv_obj_clear_flag(pageKeyboard, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(pageKeyboard, 0, BAR_H);
    lv_obj_set_size(pageKeyboard, LCD_W, LCD_H - BAR_H);
    keyboard::create(pageKeyboard);

    pageTrackpad = lv_obj_create(scr);
    lv_obj_remove_style_all(pageTrackpad);
    lv_obj_clear_flag(pageTrackpad, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(pageTrackpad, 0, BAR_H);
    lv_obj_set_size(pageTrackpad, LCD_W, LCD_H - BAR_H);
    lv_obj_update_layout(pageTrackpad);
    trackpad::create(pageTrackpad);

    createSettings();
    setMode(Mode::Keyboard);
    updateStatus();
    lv_timer_create(updateStatus, 500, nullptr);
}

void loop() {
    if (mode == Mode::Trackpad && lv_obj_has_flag(settingsPanel, LV_OBJ_FLAG_HIDDEN)) trackpad::loop();
}

}  // namespace ui
