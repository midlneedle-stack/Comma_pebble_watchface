#include "general_magic_palette.h"

typedef struct {
  GColor background_fill;
  GColor grid_stroke;
  GColor digit_stroke;
  GColor digit_stage[3];
  GColor background_stage[3];
} GeneralMagicThemePalette;

static GeneralMagicTheme s_current_theme = GENERAL_MAGIC_THEME_DARK;

static inline GeneralMagicThemePalette prv_palette(void) {
  GeneralMagicThemePalette palette;
  if (s_current_theme == GENERAL_MAGIC_THEME_LIGHT) {
    palette.background_fill = GColorWhite;
    palette.grid_stroke = PBL_IF_COLOR_ELSE(GColorFromRGB(0x55, 0x55, 0x55), GColorBlack);
    palette.digit_stroke = GColorBlack;
    palette.digit_stage[0] = PBL_IF_COLOR_ELSE(GColorFromRGB(0xAA, 0xAA, 0xAA), GColorBlack);
    palette.digit_stage[1] = PBL_IF_COLOR_ELSE(GColorFromRGB(0x66, 0x66, 0x66), GColorBlack);
    palette.digit_stage[2] = GColorBlack;
    palette.background_stage[0] = palette.digit_stage[0];
    palette.background_stage[1] = palette.digit_stage[1];
    palette.background_stage[2] = palette.digit_stage[2];
  } else {
    palette.background_fill = GColorBlack;
    palette.grid_stroke = PBL_IF_COLOR_ELSE(GColorFromRGB(0x55, 0x55, 0x55), GColorBlack);
    palette.digit_stroke = GColorWhite;
    palette.digit_stage[0] = PBL_IF_COLOR_ELSE(GColorFromRGB(0x55, 0x55, 0x55), GColorWhite);
    palette.digit_stage[1] = PBL_IF_COLOR_ELSE(GColorFromRGB(0xAA, 0xAA, 0xAA), GColorWhite);
    palette.digit_stage[2] = GColorWhite;
    palette.background_stage[0] = palette.digit_stage[0];
    palette.background_stage[1] = palette.digit_stage[1];
    palette.background_stage[2] = palette.digit_stage[2];
  }
  return palette;
}

void general_magic_palette_set_theme(GeneralMagicTheme theme) {
  s_current_theme = theme;
}

GeneralMagicTheme general_magic_palette_get_theme(void) {
  return s_current_theme;
}

GColor general_magic_palette_background_fill(void) {
  return prv_palette().background_fill;
}

GColor general_magic_palette_background_stroke(void) {
  return prv_palette().grid_stroke;
}

GColor general_magic_palette_digit_fill(void) {
  return prv_palette().background_fill;
}

GColor general_magic_palette_digit_stroke(void) {
  return prv_palette().digit_stroke;
}

GColor general_magic_palette_window_background(void) {
  return prv_palette().background_fill;
}

GColor general_magic_palette_stage_color(int stage, bool is_digit) {
  if (stage < 0) {
    return prv_palette().background_fill;
  }
  if (stage > 2) {
    stage = 2;
  }
#if defined(PBL_COLOR)
  GeneralMagicThemePalette palette = prv_palette();
  return is_digit ? palette.digit_stage[stage] : palette.background_stage[stage];
#else
  GeneralMagicThemePalette palette = prv_palette();
  return is_digit ? palette.digit_stroke : palette.grid_stroke;
#endif
}
