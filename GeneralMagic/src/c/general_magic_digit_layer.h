#pragma once

#include <pebble.h>

typedef struct GeneralMagicDigitLayer GeneralMagicDigitLayer;
typedef struct GeneralMagicBackgroundLayer GeneralMagicBackgroundLayer;

GeneralMagicDigitLayer *general_magic_digit_layer_create(GRect frame);
void general_magic_digit_layer_destroy(GeneralMagicDigitLayer *layer);
Layer *general_magic_digit_layer_get_layer(GeneralMagicDigitLayer *layer);
void general_magic_digit_layer_bind_background(GeneralMagicDigitLayer *layer,
                                        GeneralMagicBackgroundLayer *background);
void general_magic_digit_layer_set_time(GeneralMagicDigitLayer *layer, const struct tm *time);
void general_magic_digit_layer_refresh_time(GeneralMagicDigitLayer *layer);
void general_magic_digit_layer_force_redraw(GeneralMagicDigitLayer *layer);
void general_magic_digit_layer_set_use_24h(GeneralMagicDigitLayer *layer, bool use_24h);
/** Trigger the artifact-style reveal animation for the digits. */
void general_magic_digit_layer_start_diag_flip(GeneralMagicDigitLayer *layer);
