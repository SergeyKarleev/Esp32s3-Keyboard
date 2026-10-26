#pragma once

// Guition JC4827W543C. Дисплей: Arduino_GFX NV3041A по QSPI.
#define LCD_W 480
#define LCD_H 272
#define LCD_CS 45
#define LCD_SCK 47
#define LCD_D0 21
#define LCD_D1 48
#define LCD_D2 40
#define LCD_D3 39
#define LCD_BL 1

// GT911; RST и INT нужны для аппаратного сброса тача
#define TOUCH_SDA 8
#define TOUCH_SCL 4
#define TOUCH_INT 3
#define TOUCH_RST 38
