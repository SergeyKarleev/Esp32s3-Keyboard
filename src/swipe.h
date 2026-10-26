#pragma once
// Распознавание ввода свайпом: траектория пальца по буквам -> слова из частотного словаря.
// Путь сравнивается с ломаной через центры клавиш слова (обе ресемплируются до N точек),
// к среднему расстоянию добавляется штраф за редкость слова.

#include <stddef.h>
#include <stdint.h>

#include "settings.h"

namespace swipe {

struct Point {
    int16_t x, y;
};

// центр клавиши буквы (codepoint в нижнем регистре)
struct LetterKey {
    uint32_t cp;
    Point center;
};

struct Word {
    const char* text;  // UTF-8, без завершающего нуля
    uint8_t len;       // байт
    float score;       // меньше — лучше; сравним между языками
};

// построить индексы словарей (PSRAM); вызвать один раз при старте
void begin();

// до maxOut кандидатов, лучший первым; keyW — ширина буквенной клавиши
size_t decode(settings::Lang lang, const Point* path, size_t n, const LetterKey* keys, size_t keyCount, int keyW,
              Word* out, size_t maxOut);

// запомнить слово пользователя (UTF-8, нижний регистр): чем больше weight, тем выше оно в подсказках
void learn(settings::Lang lang, const char* word, size_t len, uint8_t weight);
// вызывать из loop(): сохраняет выученные слова во flash после паузы
void poll();
// забыть выученные слова
void forget();

// образец для калибровки: путь, которым пользователь провел слово word (UTF-8);
// false — путь слишком не похож на слово и не учтен
bool addSample(settings::Lang lang, const Point* path, size_t n, const LetterKey* keys, size_t keyCount, int keyW,
               const char* word, size_t len);
void resetCalibration();

// следующий символ UTF-8 (codepoint), p сдвигается
uint32_t nextCp(const char*& p, const char* end);

}  // namespace swipe
