#pragma once

#include <pebble.h>

typedef struct GeneralMagicBackgroundLayer GeneralMagicBackgroundLayer;

GeneralMagicBackgroundLayer *general_magic_background_layer_create(GRect frame);
void general_magic_background_layer_destroy(GeneralMagicBackgroundLayer *layer);
Layer *general_magic_background_layer_get_layer(GeneralMagicBackgroundLayer *layer);
void general_magic_background_layer_mark_dirty(GeneralMagicBackgroundLayer *layer);
bool general_magic_background_layer_cell_progress(GeneralMagicBackgroundLayer *layer,
                                           int cell_col,
                                           int cell_row,
                                           float *progress_out);
void general_magic_background_layer_set_animated(GeneralMagicBackgroundLayer *layer,
                                                 bool animated);
