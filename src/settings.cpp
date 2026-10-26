#include "settings.h"

#include <Preferences.h>

#include "hid.h"
#include "keycodes.h"

namespace settings {

namespace {

Values values;
Preferences prefs;

struct Hotkey {
    uint8_t mods;
    uint8_t code;
};

// порядок совпадает с hotkeyOptions()
const Hotkey HOTKEYS[] = {
    {hid::MOD_CTRL, HK_SPACE},   // Android / Android TV, macOS
    {hid::MOD_ALT, 0},            // Alt+Shift: Windows, Linux
    {hid::MOD_CTRL, 0},           // Ctrl+Shift
    {hid::MOD_GUI, HK_SPACE},    // Win+Space
    {0, HK_CAPS_LOCK},           // Caps Lock
    {0, 0},                       // не отправлять
};
constexpr uint8_t HOTKEY_COUNT = sizeof HOTKEYS / sizeof HOTKEYS[0];

}  // namespace

void load() {
    prefs.begin("kbd", true);
    values.hotkey = prefs.getUChar("hotkey", values.hotkey);
    values.speed = prefs.getUChar("speed", values.speed);
    values.naturalScroll = prefs.getBool("natural", values.naturalScroll);
    values.lang = Lang(prefs.getUChar("lang", values.lang));
    values.bluetooth = prefs.getBool("bt", values.bluetooth);
    values.macHost = prefs.getBool("cmd", values.macHost);
    values.target = prefs.getUChar("target", values.target);
    prefs.getString("taddr", values.targetAddr, sizeof values.targetAddr);
    prefs.end();
    if (values.target > 2) values.target = 0;
    if (values.hotkey >= HOTKEY_COUNT) values.hotkey = 0;
    if (values.speed < 1 || values.speed > 10) values.speed = 5;
    if (values.lang > RU) values.lang = EN;
}

void save() {
    prefs.begin("kbd", false);
    prefs.putUChar("hotkey", values.hotkey);
    prefs.putUChar("speed", values.speed);
    prefs.putBool("natural", values.naturalScroll);
    prefs.putUChar("lang", values.lang);
    prefs.putBool("bt", values.bluetooth);
    prefs.putBool("cmd", values.macHost);
    prefs.putUChar("target", values.target);
    prefs.putString("taddr", values.targetAddr);
    prefs.end();
}

Values& get() { return values; }

const char* hotkeyOptions() { return "Ctrl+Space (Android, TV)\nAlt+Shift (Windows)\nCtrl+Shift\nWin+Space\nCaps Lock\nНе отправлять"; }

void sendHotkey() {
    const Hotkey& h = HOTKEYS[values.hotkey];
    // Alt+Shift и Ctrl+Shift: второй модификатор — Shift
    if (values.hotkey == 1 || values.hotkey == 2)
        hid::chord(h.mods | hid::MOD_SHIFT, 0);
    else if (h.mods || h.code)
        hid::chord(h.mods, h.code);
}

}  // namespace settings
