#include "theme.h"

namespace theme {

namespace {

lv_font_t fontSmall, fontMedium, fontLarge;

}  // namespace

void initFonts() {
    fontSmall = font_14;
    fontSmall.fallback = &lv_font_montserrat_14;
    fontMedium = font_18;
    fontMedium.fallback = &lv_font_montserrat_18;
    fontLarge = font_22;
    fontLarge.fallback = &lv_font_montserrat_20;
}

const lv_font_t* small() { return &fontSmall; }
const lv_font_t* medium() { return &fontMedium; }
const lv_font_t* large() { return &fontLarge; }

}  // namespace theme
