#pragma once
// Настройки во flash (NVS) и состояние языка.

#include <stdint.h>

namespace settings {

enum Lang : uint8_t { EN = 0, RU = 1 };

struct Values {
    // комбинация переключения раскладки на устройстве-получателе, индекс в hotkeyOptions()
    uint8_t hotkey = 0;
    // скорость курсора 1..10
    uint8_t speed = 5;
    // прокрутка «как на телефоне»: содержимое едет за пальцем
    bool naturalScroll = true;
    Lang lang = EN;
    bool bluetooth = true;
    // система получателя: 0 — определять самой, 1 — Mac (Cmd, русская раскладка macOS), 2 — Windows/Android
    uint8_t hostOs = 0;
    // исправлять опечатки в словах, набранных по буквам
    bool autocorrect = true;
    // свайп ищет слова сразу в английском и русском, раскладка переключается сама
    bool bilingual = true;
    // куда отправлять нажатия: hid::TargetKind и адрес Bluetooth-хоста
    uint8_t target = 0;
    char targetAddr[18] = "";
};

void load();
void save();
Values& get();

// получатель — Mac (по настройке или определен автоматически)
bool isMac();
const char* hostOsOptions();

// варианты для lv_dropdown, через \n
const char* hotkeyOptions();
// отправить выбранную комбинацию переключения языка
void sendHotkey();

}  // namespace settings
