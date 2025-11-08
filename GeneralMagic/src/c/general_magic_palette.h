#pragma once

#include <pebble.h>

typedef enum {
  GENERAL_MAGIC_THEME_DARK = 0,
  GENERAL_MAGIC_THEME_LIGHT = 1,
} GeneralMagicTheme;

void general_magic_palette_set_theme(GeneralMagicTheme theme);
GeneralMagicTheme general_magic_palette_get_theme(void);

GColor general_magic_palette_background_fill(void);
GColor general_magic_palette_background_stroke(void);
GColor general_magic_palette_digit_fill(void);
GColor general_magic_palette_digit_stroke(void);
GColor general_magic_palette_window_background(void);
