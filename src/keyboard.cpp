#include "keyboard.h"

#include <Arduino.h>

#include <algorithm>
#include <cstring>

#include "hid.h"
#include "keycodes.h"
#include "settings.h"
#include "swipe.h"
#include "theme.h"

namespace keyboard {

namespace {

// Подписи клавиш с символами: EN, EN+Shift, RU, RU+Shift
struct Glyph {
    uint8_t code;
    const char* en;
    const char* enShift;
    const char* ru;
    const char* ruShift;
};

const Glyph GLYPHS[] = {
    {0x14, "q", "Q", "й", "Й"}, {0x1A, "w", "W", "ц", "Ц"}, {0x08, "e", "E", "у", "У"},
    {0x15, "r", "R", "к", "К"}, {0x17, "t", "T", "е", "Е"}, {0x1C, "y", "Y", "н", "Н"},
    {0x18, "u", "U", "г", "Г"}, {0x0C, "i", "I", "ш", "Ш"}, {0x12, "o", "O", "щ", "Щ"},
    {0x13, "p", "P", "з", "З"}, {0x2F, "[", "{", "х", "Х"}, {0x30, "]", "}", "ъ", "Ъ"},
    {0x04, "a", "A", "ф", "Ф"}, {0x16, "s", "S", "ы", "Ы"}, {0x07, "d", "D", "в", "В"},
    {0x09, "f", "F", "а", "А"}, {0x0A, "g", "G", "п", "П"}, {0x0B, "h", "H", "р", "Р"},
    {0x0D, "j", "J", "о", "О"}, {0x0E, "k", "K", "л", "Л"}, {0x0F, "l", "L", "д", "Д"},
    {0x33, ";", ":", "ж", "Ж"}, {0x34, "'", "\"", "э", "Э"},
    {0x1D, "z", "Z", "я", "Я"}, {0x1B, "x", "X", "ч", "Ч"}, {0x06, "c", "C", "с", "С"},
    {0x19, "v", "V", "м", "М"}, {0x05, "b", "B", "и", "И"}, {0x11, "n", "N", "т", "Т"},
    {0x10, "m", "M", "ь", "Ь"}, {0x36, ",", "<", "б", "Б"}, {0x37, ".", ">", "ю", "Ю"},
    {0x38, "/", "?", ".", ","},
    {0x35, "`", "~", "ё", "Ё"}, {0x1E, "1", "!", "1", "!"}, {0x1F, "2", "@", "2", "\""},
    {0x20, "3", "#", "3", "No"}, {0x21, "4", "$", "4", ";"}, {0x22, "5", "%", "5", "%"},
    {0x23, "6", "^", "6", ":"}, {0x24, "7", "&", "7", "?"}, {0x25, "8", "*", "8", "*"},
    {0x26, "9", "(", "9", "("}, {0x27, "0", ")", "0", ")"}, {0x2D, "-", "_", "-", "_"},
    {0x2E, "=", "+", "=", "+"}, {0x31, "\\", "|", "\\", "/"},
};

// русская раскладка macOS отличается от ПК цифровым рядом с Shift; точка и запятая там на Shift+7 и Shift+6
struct MacShift {
    uint8_t code;
    const char* ru;
};
const MacShift MAC_RU_SHIFT[] = {{0x21, "%"}, {0x22, ":"}, {0x23, ","}, {0x24, "."}, {0x25, ";"}};

bool macRu() { return settings::get().macHost && settings::get().lang == settings::RU; }

const Glyph* findGlyph(uint8_t code) {
    for (const Glyph& g : GLYPHS)
        if (g.code == code) return &g;
    return nullptr;
}

// буква в текущем языке: на нее действует Caps Lock
bool isLetter(uint8_t code, settings::Lang lang) {
    if (code >= 0x04 && code <= 0x1D) return true;
    if (lang != settings::RU) return false;
    return code == 0x2F || code == 0x30 || code == 0x33 || code == 0x34 || code == 0x36 || code == 0x37 || code == 0x35;
}

enum class Act : uint8_t { Row, Key, Shift, Ctrl, Alt, Gui, Lang, Sym, Abc, Media, Comma, Period };

struct Btn {
    Act act;
    uint16_t code;
    uint8_t width;
    // подпись для клавиш без символа; nullptr — из GLYPHS
    const char* label;
};

constexpr Btn ROW{Act::Row, 0, 0, nullptr};

constexpr Btn key(uint8_t code, uint8_t w = 2, const char* label = nullptr) { return {Act::Key, code, w, label}; }

// нижний ряд одинаков в обоих слоях, кроме кнопки переключения слоя
#define BOTTOM_ROW(layerBtn)                                                                              \
    layerBtn, {Act::Lang, 0, 3, nullptr}, {Act::Comma, 0, 2, ","}, key(HK_SPACE, 6, " "),                \
        {Act::Period, 0, 2, "."}, key(HK_LEFT, 2, LV_SYMBOL_LEFT),                                        \
        key(HK_UP, 2, LV_SYMBOL_UP), key(HK_DOWN, 2, LV_SYMBOL_DOWN), key(HK_RIGHT, 2, LV_SYMBOL_RIGHT)

const Btn ALPHA[] = {
    key(0x14), key(0x1A), key(0x08), key(0x15), key(0x17), key(0x1C), key(0x18), key(0x0C), key(0x12), key(0x13),
    key(0x2F), key(0x30), ROW,
    key(0x04), key(0x16), key(0x07), key(0x09), key(0x0A), key(0x0B), key(0x0D), key(0x0E), key(0x0F), key(0x33),
    key(0x34), key(HK_BACKSPACE, 3, LV_SYMBOL_BACKSPACE), ROW,
    {Act::Shift, 0, 3, LV_SYMBOL_UPLOAD}, key(0x1D), key(0x1B), key(0x06), key(0x19), key(0x05), key(0x11),
    key(0x10), key(0x36), key(0x37), key(0x38), key(HK_ENTER, 3, LV_SYMBOL_NEW_LINE), ROW,
    BOTTOM_ROW((Btn{Act::Sym, 0, 3, "?123"})),
};

const Btn SYMBOLS[] = {
    key(0x35), key(0x1E), key(0x1F), key(0x20), key(0x21), key(0x22), key(0x23), key(0x24), key(0x25), key(0x26),
    key(0x27), key(0x2D), key(0x2E), ROW,
    key(HK_ESC, 2, "Esc"), key(HK_TAB, 2, "Tab"), key(0x31), key(HK_DELETE, 2, "Del"), key(HK_HOME, 2, "Home"),
    key(HK_END, 2, "End"), key(HK_PAGE_UP, 2, "PgUp"), key(HK_PAGE_DOWN, 2, "PgDn"),
    key(HK_BACKSPACE, 3, LV_SYMBOL_BACKSPACE), ROW,
    {Act::Shift, 0, 3, LV_SYMBOL_UPLOAD}, {Act::Ctrl, 0, 2, "Ctrl"}, {Act::Alt, 0, 2, "Alt"}, {Act::Gui, 0, 2, "Win"},
    {Act::Media, hid::CC_VOL_DOWN, 2, LV_SYMBOL_VOLUME_MID "-"}, {Act::Media, hid::CC_VOL_UP, 2, LV_SYMBOL_VOLUME_MAX "+"},
    {Act::Media, hid::CC_MUTE, 2, LV_SYMBOL_MUTE "x"}, {Act::Media, hid::CC_PLAY_PAUSE, 2, LV_SYMBOL_PLAY},
    key(HK_ENTER, 3, LV_SYMBOL_NEW_LINE), ROW,
    BOTTOM_ROW((Btn{Act::Abc, 0, 3, "ABC"})),
};

struct Layer {
    const Btn* btns;
    size_t count;
};
const Layer LAYERS[] = {{ALPHA, sizeof ALPHA / sizeof ALPHA[0]}, {SYMBOLS, sizeof SYMBOLS / sizeof SYMBOLS[0]}};

constexpr size_t MAX_BTNS = 64;

lv_obj_t* matrix = nullptr;
uint8_t layer = 0;
// карта для LVGL: подписи и "\n" между рядами, "" в конце
const char* map[MAX_BTNS + 8];
lv_btnmatrix_ctrl_t ctrl[MAX_BTNS];
// кнопка LVGL (без учета рядов) -> описание
const Btn* byId[MAX_BTNS];
uint16_t btnCount = 0;

enum class ShiftState : uint8_t { Off, Once, Locked };
ShiftState shift = ShiftState::Off;
uint32_t shiftTapAt = 0;
constexpr uint32_t DOUBLE_TAP_MS = 400;
// залипающие модификаторы: действуют на следующую клавишу
uint8_t sticky = 0;
bool langLongPress = false;

// ---------- свайп ----------
constexpr size_t MAX_PATH = 400;
constexpr size_t MAX_TRAIL = 64;
constexpr size_t MAX_SUGGEST = 4;
swipe::Point path[MAX_PATH];
size_t pathLen = 0;
bool pathFromLetter = false;
bool swiping = false;
// за это нажатие сработал автоповтор: отпускание клавишу не нажимает
bool repeated = false;
lv_obj_t* trail = nullptr;
lv_point_t trailPts[MAX_TRAIL];
uint16_t trailLen = 0;

// последнее набранное свайпом слово (для замены подсказкой) и варианты
char suggest[MAX_SUGGEST][64];
const char* suggestPtrs[MAX_SUGGEST];
settings::Lang suggestLang[MAX_SUGGEST];
// штраф к счету слова на неактивном языке (в ширинах клавиши)
constexpr float OTHER_LANG_PENALTY = 0.12f;
constexpr uint32_t LANG_SWITCH_MS = 60;
size_t suggestCount = 0;
size_t lastTypedChars = 0;  // символов последнего слова вместе с пробелом
bool lastCapital = false;
bool lastAllCaps = false;
SuggestCallback suggestCb = nullptr;
// слово, набираемое по буквам: запоминается после пробела или Enter
char typed[32];
size_t typedLen = 0;

bool shifted(uint8_t code) {
    if (shift == ShiftState::Once) return true;
    return shift == ShiftState::Locked && isLetter(code, settings::get().lang);
}

const char* labelFor(const Btn& b) {
    if (b.act == Act::Lang) return settings::get().lang == settings::RU ? "RU" : "EN";
    if (b.label) return b.label;
    const Glyph* g = findGlyph(b.code);
    if (!g) return "?";
    bool s = shifted(b.code);
    if (settings::get().lang == settings::RU) {
        if (s && macRu())
            for (const MacShift& m : MAC_RU_SHIFT)
                if (m.code == b.code) return m.ru;
        return s ? g->ruShift : g->ru;
    }
    return s ? g->enShift : g->en;
}

bool isChecked(const Btn& b) {
    switch (b.act) {
        case Act::Shift: return shift != ShiftState::Off;
        case Act::Ctrl: return sticky & hid::MOD_CTRL;
        case Act::Alt: return sticky & hid::MOD_ALT;
        case Act::Gui: return sticky & hid::MOD_GUI;
        default: return false;
    }
}

bool isSpecial(const Btn& b) { return b.act != Act::Key || b.label != nullptr; }

void build() {
    const Layer& l = LAYERS[layer];
    size_t m = 0;
    btnCount = 0;
    for (size_t i = 0; i < l.count; i++) {
        const Btn& b = l.btns[i];
        if (b.act == Act::Row) {
            map[m++] = "\n";
            continue;
        }
        map[m++] = labelFor(b);
        // все клавиши срабатывают по отпусканию: иначе при свайпе btnmatrix нажимает каждую
        // клавишу под пальцем. Удержание повторяет только клавиши без символа и мультимедиа.
        lv_btnmatrix_ctrl_t c = b.width | LV_BTNMATRIX_CTRL_CLICK_TRIG;
        bool repeat = (b.act == Act::Key && b.label) || b.act == Act::Media;
        if (!repeat) c |= LV_BTNMATRIX_CTRL_NO_REPEAT;
        if (isChecked(b)) c |= LV_BTNMATRIX_CTRL_CHECKED;
        ctrl[btnCount] = c;
        byId[btnCount++] = &b;
    }
    map[m] = "";
    lv_btnmatrix_set_map(matrix, map);
    lv_btnmatrix_set_ctrl_map(matrix, ctrl);
    lv_obj_set_style_text_font(matrix, layer == 0 ? theme::large() : theme::medium(), LV_PART_ITEMS);
}

void notifySuggestions() {
    if (suggestCb) suggestCb(suggestPtrs, suggestCount);
}

void clearSuggestions() {
    lastTypedChars = 0;
    if (!suggestCount) return;
    suggestCount = 0;
    notifySuggestions();
}

void toggleSticky(uint8_t mod) { sticky ^= mod; }

// буква текущего языка в нижнем регистре (UTF-8) или nullptr
const char* letterText(uint8_t code) {
    settings::Lang lang = settings::get().lang;
    const Glyph* g = findGlyph(code);
    if (!g || !isLetter(code, lang)) return nullptr;
    return lang == settings::RU ? g->ru : g->en;
}

void trackTyped(uint8_t code, uint8_t mods) {
    const char* letter = (mods & ~hid::MOD_SHIFT) ? nullptr : letterText(code);
    if (letter && typedLen + strlen(letter) < sizeof typed) {
        memcpy(typed + typedLen, letter, strlen(letter));
        typedLen += strlen(letter);
        return;
    }
    if (code == HK_BACKSPACE && typedLen) {
        // убрать последний символ UTF-8
        do typedLen--;
        while (typedLen && (typed[typedLen] & 0xC0) == 0x80);
        return;
    }
    if (typedLen && (code == HK_SPACE || code == HK_ENTER || code == 0x36 || code == 0x37 || code == 0x38))
        swipe::learn(settings::get().lang, typed, typedLen, 1);
    typedLen = 0;
}

// точка и запятая в любом языке: в русской раскладке ПК они на «/» и Shift+«/»,
// в русской раскладке Mac — на Shift+7 и Shift+6
void typePunct(bool period) {
    clearSuggestions();
    trackTyped(0x36, 0);
    if (settings::get().lang == settings::EN)
        hid::tapKey(0, period ? 0x37 : 0x36);
    else if (settings::get().macHost)
        hid::tapKey(hid::MOD_SHIFT, period ? 0x24 : 0x23);
    else
        hid::tapKey(period ? 0 : hid::MOD_SHIFT, 0x38);
}

void pressKey(uint8_t code) {
    clearSuggestions();
    uint8_t mods = sticky;
    if (shifted(code)) mods |= hid::MOD_SHIFT;
    trackTyped(code, mods);
    if (macRu() && !(mods & ~hid::MOD_SHIFT)) {
        bool sh = mods & hid::MOD_SHIFT;
        // точка/запятая с клавиши «/» и Shift+цифры подписаны по ПК: отправляем то, что на Mac дает эти символы
        if (code == 0x38 && !sh) {
            code = 0x24;
            mods |= hid::MOD_SHIFT;
        } else if (code == 0x38) {
            code = 0x23;
        }
    }
    hid::tapKey(mods, code);
    bool rebuild = sticky != 0 || shift == ShiftState::Once;
    sticky = 0;
    if (shift == ShiftState::Once) shift = ShiftState::Off;
    if (rebuild) build();
}

void tapShift() {
    uint32_t now = millis();
    if (shift == ShiftState::Off)
        shift = ShiftState::Once;
    else if (shift == ShiftState::Once && now - shiftTapAt < DOUBLE_TAP_MS)
        shift = ShiftState::Locked;
    else
        shift = ShiftState::Off;
    shiftTapAt = now;
    build();
}

void switchLang(bool notifyHost) {
    auto& s = settings::get();
    s.lang = s.lang == settings::RU ? settings::EN : settings::RU;
    if (notifyHost) settings::sendHotkey();
    settings::save();
    clearSuggestions();
    build();
}

// первый символ подписи как codepoint
uint32_t labelCp(const char* s) {
    const char* p = s;
    return swipe::nextCp(p, s + strlen(s));
}

// буквы языка на экране: codepoint и центр клавиши (раскладки совпадают по позициям)
size_t letterKeys(settings::Lang lang, swipe::LetterKey* out, size_t maxOut, int& keyW) {
    auto* bm = reinterpret_cast<lv_btnmatrix_t*>(matrix);
    lv_area_t c;
    lv_obj_get_coords(matrix, &c);
    size_t n = 0;
    keyW = 40;
    for (uint16_t i = 0; i < btnCount && n < maxOut; i++) {
        const Btn& b = *byId[i];
        if (b.act != Act::Key || b.label || !isLetter(b.code, lang)) continue;
        const Glyph* g = findGlyph(b.code);
        const lv_area_t& a = bm->button_areas[i];
        out[n++] = {labelCp(lang == settings::RU ? g->ru : g->en),
                    {int16_t(c.x1 + (a.x1 + a.x2) / 2), int16_t(c.y1 + (a.y1 + a.y2) / 2)}};
        keyW = a.x2 - a.x1 + 1;
    }
    return n;
}

// перевести получателя и подписи на язык слова
void ensureLang(settings::Lang lang) {
    if (settings::get().lang == lang) return;
    settings::get().lang = lang;
    settings::sendHotkey();
    // получатель переключает раскладку не мгновенно
    delay(LANG_SWITCH_MS);
    settings::save();
    build();
}

// набрать слово кодами клавиш языка lang; возвращает число символов
size_t typeText(settings::Lang lang, const char* text, size_t len, bool capital, bool allCaps) {
    const char* p = text;
    const char* end = text + len;
    size_t count = 0;
    while (p < end) {
        uint32_t cp = swipe::nextCp(p, end);
        for (const Glyph& g : GLYPHS) {
            if (labelCp(lang == settings::RU ? g.ru : g.en) != cp) continue;
            bool up = allCaps || (capital && count == 0);
            hid::tapKey(up ? hid::MOD_SHIFT : 0, g.code);
            break;
        }
        count++;
    }
    return count;
}

void typeWord(size_t i) {
    ensureLang(suggestLang[i]);
    lastTypedChars = typeText(suggestLang[i], suggestPtrs[i], strlen(suggestPtrs[i]), lastCapital, lastAllCaps) + 1;
    hid::tapKey(0, HK_SPACE);
}

void finishSwipe() {
    settings::Lang cur = settings::get().lang;
    settings::Lang other = cur == settings::RU ? settings::EN : settings::RU;
    swipe::Word words[2 * MAX_SUGGEST];
    settings::Lang langs[2 * MAX_SUGGEST];
    uint32_t t = millis();
    size_t n = 0;
    for (settings::Lang lang : {cur, other}) {
        if (lang == other && !settings::get().bilingual) break;
        swipe::LetterKey keys[40];
        int keyW;
        size_t keyCount = letterKeys(lang, keys, 40, keyW);
        size_t got = swipe::decode(lang, path, pathLen, keys, keyCount, keyW, words + n, MAX_SUGGEST);
        for (size_t k = n; k < n + got; k++) {
            langs[k] = lang;
            // другой язык — только если заметно лучше: раскладка не переключается от случайных совпадений
            if (lang == other) words[k].score += OTHER_LANG_PENALTY;
        }
        n += got;
    }
    // лучшие из обоих языков
    size_t order[2 * MAX_SUGGEST];
    for (size_t k = 0; k < n; k++) order[k] = k;
    std::sort(order, order + n, [&](size_t a, size_t b) { return words[a].score < words[b].score; });
    n = std::min(n, MAX_SUGGEST);
    Serial.printf("[swipe] %u points -> %u words in %u ms\n", pathLen, n, millis() - t);
    if (!n) return;
    for (size_t i = 0; i < n; i++) {
        const swipe::Word& w = words[order[i]];
        size_t len = std::min<size_t>(w.len, sizeof suggest[i] - 1);
        memcpy(suggest[i], w.text, len);
        suggest[i][len] = 0;
        suggestPtrs[i] = suggest[i];
        suggestLang[i] = langs[order[i]];
    }
    suggestCount = n;
    typedLen = 0;
    swipe::learn(suggestLang[0], suggest[0], strlen(suggest[0]), 1);
    lastCapital = shift != ShiftState::Off;
    lastAllCaps = shift == ShiftState::Locked;
    typeWord(0);
    sticky = 0;
    if (shift == ShiftState::Once) {
        shift = ShiftState::Off;
        build();
    }
    notifySuggestions();
}

void trailAdd(const lv_point_t& p) {
    if (trailLen == MAX_TRAIL) {
        // след длинный: прореживаем вдвое
        for (uint16_t i = 0; i < MAX_TRAIL / 2; i++) trailPts[i] = trailPts[i * 2];
        trailLen = MAX_TRAIL / 2;
    }
    lv_area_t c;
    lv_obj_get_coords(lv_obj_get_parent(trail), &c);
    trailPts[trailLen++] = {lv_coord_t(p.x - c.x1), lv_coord_t(p.y - c.y1)};
    lv_line_set_points(trail, trailPts, trailLen);
}

void trackPath(lv_event_code_t code) {
    lv_point_t p;
    lv_indev_get_point(lv_indev_get_act(), &p);
    if (code == LV_EVENT_PRESSED) {
        uint16_t id = lv_btnmatrix_get_selected_btn(matrix);
        const Btn* b = id < btnCount ? byId[id] : nullptr;
        bool letter = b && b->act == Act::Key && !b->label &&
                      (isLetter(b->code, settings::get().lang) || (settings::get().bilingual && isLetter(b->code, settings::RU)));
        pathFromLetter = layer == 0 && letter;
        pathLen = 0;
        swiping = false;
        repeated = false;
        path[pathLen++] = {int16_t(p.x), int16_t(p.y)};
        return;
    }
    if (!pathFromLetter || pathLen == MAX_PATH) return;
    const swipe::Point& last = path[pathLen - 1];
    if (std::abs(p.x - last.x) + std::abs(p.y - last.y) < 3) return;
    path[pathLen++] = {int16_t(p.x), int16_t(p.y)};
    // палец ушел с клавиши дальше ~0.7 ее ширины — это свайп
    if (!swiping && std::abs(p.x - path[0].x) + std::abs(p.y - path[0].y) > 28) {
        swiping = true;
        trailLen = 0;
        lv_obj_clear_flag(trail, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(trail);
        for (size_t i = 0; i < pathLen; i += 4) trailAdd({path[i].x, path[i].y});
    }
    if (swiping) {
        const lv_point_t& t = trailPts[trailLen - 1];
        lv_area_t c;
        lv_obj_get_coords(lv_obj_get_parent(trail), &c);
        if (std::abs(p.x - c.x1 - t.x) + std::abs(p.y - c.y1 - t.y) >= 6) trailAdd(p);
    }
}

void onEvent(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
        trackPath(code);
        return;
    }
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        lv_obj_add_flag(trail, LV_OBJ_FLAG_HIDDEN);
        if (swiping) {
            swiping = false;
            finishSwipe();
        }
        return;
    }
    // во время свайпа клавиши не нажимаются; слово набирается по отпусканию
    if (swiping) return;

    uint16_t id = lv_btnmatrix_get_selected_btn(matrix);
    if (id == LV_BTNMATRIX_BTN_NONE || id >= btnCount) return;
    const Btn& b = *byId[id];

    // долгое нажатие на язык: только сменить подписи, если язык получателя рассинхронизировался
    if (code == LV_EVENT_LONG_PRESSED && b.act == Act::Lang) {
        langLongPress = true;
        switchLang(false);
        return;
    }
    if (code != LV_EVENT_VALUE_CHANGED) return;

    // VALUE_CHANGED при нажатом пальце — автоповтор; после повторов отпускание не нажимает еще раз
    if (lv_indev_get_act()->proc.state == LV_INDEV_STATE_PRESSED)
        repeated = true;
    else if (repeated)
        return;

    switch (b.act) {
        case Act::Key: pressKey(b.code); break;
        case Act::Shift: tapShift(); break;
        case Act::Ctrl: toggleSticky(hid::MOD_CTRL); build(); break;
        case Act::Alt: toggleSticky(hid::MOD_ALT); build(); break;
        case Act::Gui: toggleSticky(hid::MOD_GUI); build(); break;
        case Act::Media: hid::consumer(b.code); break;
        case Act::Comma: typePunct(false); break;
        case Act::Period: typePunct(true); break;
        case Act::Lang:
            if (langLongPress)
                langLongPress = false;
            else
                switchLang(true);
            break;
        case Act::Sym: layer = 1; build(); break;
        case Act::Abc: layer = 0; build(); break;
        default: break;
    }
}

// служебные клавиши темнее буквенных
void onDraw(lv_event_t* e) {
    lv_obj_draw_part_dsc_t* dsc = lv_event_get_draw_part_dsc(e);
    if (dsc->part != LV_PART_ITEMS || dsc->id >= btnCount || !dsc->rect_dsc) return;
    const Btn& b = *byId[dsc->id];
    if (lv_btnmatrix_has_btn_ctrl(matrix, dsc->id, LV_BTNMATRIX_CTRL_CHECKED)) return;
    if (isSpecial(b) && !(lv_obj_get_state(matrix) & LV_STATE_PRESSED && lv_btnmatrix_get_selected_btn(matrix) == dsc->id))
        dsc->rect_dsc->bg_color = theme::keySpecial();
}

// при свайпе на двух языках — мелкая серая подпись второго языка в углу клавиши
void onDrawEnd(lv_event_t* e) {
    if (!settings::get().bilingual || layer != 0) return;
    lv_obj_draw_part_dsc_t* dsc = lv_event_get_draw_part_dsc(e);
    if (dsc->part != LV_PART_ITEMS || dsc->id >= btnCount) return;
    const Btn& b = *byId[dsc->id];
    const Glyph* g = b.act == Act::Key && !b.label ? findGlyph(b.code) : nullptr;
    if (!g) return;
    bool s = shifted(b.code);
    const char* text = settings::get().lang == settings::RU ? (s ? g->enShift : g->en) : (s ? g->ruShift : g->ru);
    lv_draw_label_dsc_t ld;
    lv_draw_label_dsc_init(&ld);
    ld.font = theme::small();
    ld.color = theme::muted();
    ld.align = LV_TEXT_ALIGN_RIGHT;
    // draw_area здесь — область подписи, координаты клавиши берем у btnmatrix
    lv_area_t a = reinterpret_cast<lv_btnmatrix_t*>(matrix)->button_areas[dsc->id];
    lv_area_t c;
    lv_obj_get_coords(matrix, &c);
    lv_area_move(&a, c.x1, c.y1);
    a.x1 = a.x2 - 24;
    a.x2 -= 4;
    a.y1 += 3;
    a.y2 = a.y1 + lv_font_get_line_height(ld.font);
    lv_draw_label(dsc->draw_ctx, &ld, &a, text, nullptr);
}

}  // namespace

void create(lv_obj_t* parent) {
    matrix = lv_btnmatrix_create(parent);
    lv_obj_set_size(matrix, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(matrix, theme::bg(), 0);
    lv_obj_set_style_border_width(matrix, 0, 0);
    lv_obj_set_style_radius(matrix, 0, 0);
    lv_obj_set_style_pad_all(matrix, 4, 0);
    lv_obj_set_style_pad_row(matrix, 5, 0);
    lv_obj_set_style_pad_column(matrix, 4, 0);

    lv_obj_set_style_bg_color(matrix, theme::key(), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(matrix, theme::keyPressed(), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(matrix, theme::accent(), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(matrix, theme::text(), LV_PART_ITEMS);
    lv_obj_set_style_radius(matrix, 6, LV_PART_ITEMS);
    lv_obj_set_style_border_width(matrix, 0, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(matrix, 0, LV_PART_ITEMS);

    lv_obj_add_event_cb(matrix, onEvent, LV_EVENT_ALL, nullptr);
    lv_obj_add_event_cb(matrix, onDraw, LV_EVENT_DRAW_PART_BEGIN, nullptr);
    lv_obj_add_event_cb(matrix, onDrawEnd, LV_EVENT_DRAW_PART_END, nullptr);

    // след пальца при свайпе
    trail = lv_line_create(parent);
    lv_obj_set_pos(trail, 0, 0);
    lv_obj_clear_flag(trail, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(trail, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_line_width(trail, 6, 0);
    lv_obj_set_style_line_rounded(trail, true, 0);
    lv_obj_set_style_line_color(trail, theme::accent(), 0);
    lv_obj_set_style_line_opa(trail, LV_OPA_70, 0);
    build();
}

void refresh() {
    if (!matrix) return;
    clearSuggestions();
    build();
}

void onSuggestions(SuggestCallback cb) { suggestCb = cb; }

void pickSuggestion(size_t i) {
    if (i == 0 || i >= suggestCount) return;
    for (size_t k = 0; k < lastTypedChars; k++) hid::tapKey(0, HK_BACKSPACE);
    std::swap(suggestPtrs[0], suggestPtrs[i]);
    std::swap(suggestLang[0], suggestLang[i]);
    // выбранный вариант важнее автоматически набранного
    swipe::learn(suggestLang[0], suggestPtrs[0], strlen(suggestPtrs[0]), 3);
    typeWord(0);
    notifySuggestions();
}

}  // namespace keyboard
