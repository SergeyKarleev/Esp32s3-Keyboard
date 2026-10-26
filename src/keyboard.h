#pragma once
// Экранная клавиатура EN/RU: буквы и слой символов/управления.
// Отправляет коды клавиш по позициям (ЙЦУКЕН = QWERTY), русские буквы получатель печатает
// в своей русской раскладке; кнопка языка переключает подписи и шлет комбинацию смены языка.

#include <lvgl.h>
#include <stddef.h>

namespace keyboard {

void create(lv_obj_t* parent);
// перерисовать подписи (смена языка из настроек)
void refresh();

// варианты слова после свайпа: words[0] уже набрано, n == 0 — подсказок нет
using SuggestCallback = void (*)(const char* const* words, size_t n);
void onSuggestions(SuggestCallback cb);
// заменить последнее набранное свайпом слово вариантом i
void pickSuggestion(size_t i);

// подсказка в верхней панели (режим тренировки); nullptr — убрать
using PromptCallback = void (*)(const char* text);
void onPrompt(PromptCallback cb);
// тренировка свайпа: пользователь проводит показанные слова, распознавание подстраивается под него
void startTraining();
void stopTraining();

}  // namespace keyboard
