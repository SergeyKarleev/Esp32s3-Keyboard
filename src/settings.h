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
    // получатель — Mac: копировать/вставить через Cmd, русская раскладка macOS
    bool macHost = false;
    // куда отправлять нажатия: hid::TargetKind и адрес Bluetooth-хоста
    uint8_t target = 0;
    char targetAddr[18] = "";
};

void load();
void save();
Values& get();

// варианты для lv_dropdown, через \n
const char* hotkeyOptions();
// отправить выбранную комбинацию переключения языка
void sendHotkey();

}  // namespace settings
