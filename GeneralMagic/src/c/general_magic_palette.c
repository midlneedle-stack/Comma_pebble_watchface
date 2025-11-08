#include "general_magic_palette.h"

static GeneralMagicTheme s_current_theme = GENERAL_MAGIC_THEME_DARK;

void general_magic_palette_set_theme(GeneralMagicTheme theme) {
  s_current_theme = theme;
}

GeneralMagicTheme general_magic_palette_get_theme(void) {
  return s_current_theme;
}

static inline bool prv_theme_is_light(void) {
  return s_current_theme == GENERAL_MAGIC_THEME_LIGHT;
}

GColor general_magic_palette_background_fill(void) {
  return prv_theme_is_light() ? GColorWhite : GColorBlack;
}

GColor general_magic_palette_background_stroke(void) {
  if (prv_theme_is_light()) {
    return PBL_IF_COLOR_ELSE(GColorFromRGB(0xAA, 0xAA, 0xAA), GColorBlack);
  }
  return PBL_IF_COLOR_ELSE(GColorFromRGB(0x55, 0x55, 0x55), GColorBlack);
}

GColor general_magic_palette_digit_fill(void) {
  return general_magic_palette_background_fill();
}

GColor general_magic_palette_digit_stroke(void) {
  return prv_theme_is_light() ? GColorBlack : GColorWhite;
}

GColor general_magic_palette_window_background(void) {
  return general_magic_palette_background_fill();
}
