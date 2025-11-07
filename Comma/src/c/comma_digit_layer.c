#include "comma_digit_layer.h"

#include <stdlib.h>
#include <time.h>

#include "comma_background_layer.h"
#include "comma_glyphs.h"
#include "comma_layout.h"
#include "comma_palette.h"

#define COMMA_DIGIT_TIMER_MS 16
#define COMMA_DIGIT_COMPACT_THRESHOLD 0.15f
#define COMMA_DIGIT_FULL_THRESHOLD 0.45f

typedef struct {
  int16_t digits[COMMA_DIGIT_COUNT];
  bool use_24h_time;
  AppTimer *anim_timer;
  bool reveal_complete;
  CommaBackgroundLayer *background;
  /* -1 = off, 0 = core, 1 = compact, 2 = full */
  int8_t cell_level[COMMA_TOTAL_GLYPHS][COMMA_DIGIT_HEIGHT]
                   [COMMA_DIGIT_WIDTH];
} CommaDigitLayerState;

struct CommaDigitLayer {
  Layer *layer;
  CommaDigitLayerState *state;
};

static inline CommaDigitLayerState *prv_get_state(CommaDigitLayer *layer) {
  return layer ? layer->state : NULL;
}

static inline int prv_slot_for_digit_index(int digit_index) {
  return (digit_index < 2) ? digit_index : (digit_index + 1);
}

static inline int prv_slot_width(int slot) {
  return (slot == 2) ? COMMA_DIGIT_COLON_WIDTH : COMMA_DIGIT_WIDTH;
}

static inline bool prv_digit_present(const CommaDigitLayerState *state,
                                     int slot) {
  if (slot == 2) {
    return true;
  }
  const int digit_index = (slot < 2) ? slot : (slot - 1);
  return state->digits[digit_index] >= 0;
}

static void prv_zero_cell_levels(CommaDigitLayerState *state, int slot) {
  if (!state || slot < 0 || slot >= COMMA_TOTAL_GLYPHS) {
    return;
  }
  for (int row = 0; row < COMMA_DIGIT_HEIGHT; ++row) {
    for (int col = 0; col < COMMA_DIGIT_WIDTH; ++col) {
      state->cell_level[slot][row][col] = -1;
    }
  }
}

static void prv_zero_all_levels(CommaDigitLayerState *state) {
  for (int slot = 0; slot < COMMA_TOTAL_GLYPHS; ++slot) {
    prv_zero_cell_levels(state, slot);
  }
  if (state) {
    state->reveal_complete = false;
  }
}

static int prv_digit_level_from_progress(float progress) {
  if (progress < COMMA_DIGIT_COMPACT_THRESHOLD) {
    return 0;
  }
  if (progress < COMMA_DIGIT_FULL_THRESHOLD) {
    return 1;
  }
  return 2;
}

static int prv_glyph_for_slot(const CommaDigitLayerState *state, int slot) {
  if (slot == 2) {
    return COMMA_GLYPH_COLON;
  }
  const int digit_index = (slot < 2) ? slot : (slot - 1);
  return state->digits[digit_index];
}

static bool prv_update_slot_levels(CommaDigitLayerState *state, int slot,
                                   int base_col, const CommaLayout *layout) {
  if (!state || !state->background) {
    return true;
  }

  const int glyph_index = prv_glyph_for_slot(state, slot);
  if (glyph_index < COMMA_GLYPH_ZERO ||
      glyph_index > COMMA_GLYPH_COLON) {
    /* no glyph to draw (blank slot) */
    prv_zero_cell_levels(state, slot);
    return true;
  }

  const CommaGlyph *glyph = &COMMA_GLYPHS[glyph_index];
  bool slot_complete = true;
  for (int row = 0; row < COMMA_DIGIT_HEIGHT; ++row) {
    const uint8_t mask = glyph->rows[row];
    const uint8_t pin_mask = glyph->pins[row];
    if (!mask) {
      continue;
    }
    for (int col = 0; col < glyph->width; ++col) {
      const int bit = (1 << (glyph->width - 1 - col));
      if (!(mask & bit)) {
        continue;
      }
      const bool pinned = pin_mask & bit;
      const int grid_col = base_col + col;
      const int grid_row = layout->digit_start_row + row;

      if (pinned) {
        float progress = 0.0f;
        if (comma_background_layer_cell_progress(state->background, grid_col,
                                                 grid_row, &progress)) {
          const int target = prv_digit_level_from_progress(progress);
          state->cell_level[slot][row][col] = (target >= 0) ? 0 : -1;
        }
      } else {
        float progress = 0.0f;
        if (comma_background_layer_cell_progress(state->background, grid_col,
                                                  grid_row, &progress)) {
          const int target = prv_digit_level_from_progress(progress);
          if (target > state->cell_level[slot][row][col]) {
            state->cell_level[slot][row][col] = target;
          }
        }
      }
      if ((pinned && state->cell_level[slot][row][col] < 0) ||
          (!pinned && state->cell_level[slot][row][col] < 2)) {
        slot_complete = false;
      }
    }
  }
  return slot_complete;
}

static bool prv_step_digit_levels(CommaDigitLayerState *state) {
  if (!state || !state->background) {
    return true;
  }

  bool all_complete = true;
  const CommaLayout *layout = comma_layout_get();
  int base_col = layout->digit_start_col;
  for (int slot = 0; slot < COMMA_TOTAL_GLYPHS; ++slot) {
    if (!prv_digit_present(state, slot)) {
      prv_zero_cell_levels(state, slot);
      continue;
    }
    const bool slot_done =
        prv_update_slot_levels(state, slot, base_col, layout);
    if (!slot_done) {
      all_complete = false;
    }
    switch (slot) {
      case 0:
      case 1:
      case 3:
        base_col += COMMA_DIGIT_WIDTH + COMMA_DIGIT_GAP;
        break;
      case 2:
        base_col += COMMA_DIGIT_COLON_WIDTH + COMMA_DIGIT_GAP;
        break;
      default:
        break;
    }
  }
  return all_complete;
}

static void prv_schedule_anim_timer(CommaDigitLayer *layer);

static void prv_anim_timer_cb(void *ctx) {
  CommaDigitLayer *layer = ctx;
  if (!layer || !layer->layer) {
    return;
  }
  CommaDigitLayerState *state = layer_get_data(layer->layer);
  if (!state) {
    return;
  }

  const bool done = prv_step_digit_levels(state);
  layer_mark_dirty(layer->layer);
  if (done) {
    state->reveal_complete = true;
    state->anim_timer = NULL;
  } else {
    prv_schedule_anim_timer(layer);
  }
}

static void prv_schedule_anim_timer(CommaDigitLayer *layer) {
  if (!layer || !layer->layer) {
    return;
  }
  CommaDigitLayerState *state = layer_get_data(layer->layer);
  if (!state || state->reveal_complete) {
    return;
  }
  if (state->anim_timer) {
    app_timer_cancel(state->anim_timer);
  }
  state->anim_timer =
      app_timer_register(COMMA_DIGIT_TIMER_MS, prv_anim_timer_cb, layer);
}

#if COMMA_CELL_SIZE != 6
static void prv_fill_block(GContext *ctx, const GPoint origin,
                           int row_start, int row_end,
                           int col_start, int col_end) {
  if (row_start > row_end || col_start > col_end) {
    return;
  }
  if (row_start < 0) {
    row_start = 0;
  }
  if (col_start < 0) {
    col_start = 0;
  }
  if (row_end >= COMMA_CELL_SIZE) {
    row_end = COMMA_CELL_SIZE - 1;
  }
  if (col_end >= COMMA_CELL_SIZE) {
    col_end = COMMA_CELL_SIZE - 1;
  }
  for (int row = row_start; row <= row_end; ++row) {
    for (int col = col_start; col <= col_end; ++col) {
      graphics_draw_pixel(ctx, GPoint(origin.x + col, origin.y + row));
    }
  }
}
#endif

#if COMMA_CELL_SIZE == 6
static void prv_draw_row_span(GContext *ctx, const GPoint origin, int row,
                              int col_start, int col_end) {
  for (int col = col_start; col <= col_end; ++col) {
    graphics_draw_pixel(ctx, GPoint(origin.x + col, origin.y + row));
  }
}
#endif

static void prv_draw_digit_shape(GContext *ctx, const GRect frame,
                                 int size_level) {
  const GPoint origin = frame.origin;
#if COMMA_CELL_SIZE == 6
  switch (size_level) {
    case 0:
      for (int row = 2; row <= 3; ++row) {
        prv_draw_row_span(ctx, origin, row, 2, 3);
      }
      break;
    case 2:
      for (int row = 1; row <= 4; ++row) {
        prv_draw_row_span(ctx, origin, row, 1, 4);
      }
      break;
    case 1:
      prv_draw_row_span(ctx, origin, 1, 2, 3);
      for (int row = 2; row <= 3; ++row) {
        prv_draw_row_span(ctx, origin, row, 1, 4);
      }
      prv_draw_row_span(ctx, origin, 4, 2, 3);
      break;
    default:
      break;
  }
#else
  const int size = COMMA_CELL_SIZE;
  const int outer = 1;
  const int inner = (size >= 8) ? 2 : 1;
  const int core_size = (size >= 8) ? 3 : 2;
  switch (size_level) {
    case 2:
      prv_fill_block(ctx, origin, outer, size - outer - 1,
                     outer, size - outer - 1);
      break;
    case 1:
      prv_fill_block(ctx, origin, inner, size - inner - 1,
                     outer, size - outer - 1);
      prv_fill_block(ctx, origin, inner - 1, inner - 1,
                     inner, size - inner - 1);
      prv_fill_block(ctx, origin, size - inner, size - inner,
                     inner, size - inner - 1);
      break;
    case 0: {
      const int start = (size - core_size) / 2;
      prv_fill_block(ctx, origin, start, start + core_size - 1,
                     start, start + core_size - 1);
      break;
    }
    default:
      break;
  }
#endif
}

static void prv_draw_digit_cell(GContext *ctx, int cell_col, int cell_row,
                                int size_level) {
  if (size_level < 0) {
    return;
  }
  const GRect frame = comma_cell_frame(cell_col, cell_row);
  prv_draw_digit_shape(ctx, frame, size_level);
}

static void prv_draw_glyph(GContext *ctx, const CommaGlyph *glyph, int cell_col,
                           int cell_row, GColor base_stroke,
                           const uint8_t (*levels)[COMMA_DIGIT_WIDTH]) {
  if (!glyph) {
    return;
  }
  graphics_context_set_stroke_color(ctx, base_stroke);
  for (int row = 0; row < COMMA_DIGIT_HEIGHT; ++row) {
    const uint8_t mask = glyph->rows[row];
    if (!mask) {
      continue;
    }
    for (int col = 0; col < glyph->width; ++col) {
      if (mask & (1 << (glyph->width - 1 - col))) {
        prv_draw_digit_cell(ctx, cell_col + col, cell_row + row,
                            levels[row][col]);
      }
    }
  }
}

static void prv_digit_layer_update_proc(Layer *layer, GContext *ctx) {
  CommaDigitLayerState *state = layer_get_data(layer);
  if (!state) {
    return;
  }

  if (!state->reveal_complete) {
    if (prv_step_digit_levels(state)) {
      state->reveal_complete = true;
      if (state->anim_timer) {
        app_timer_cancel(state->anim_timer);
        state->anim_timer = NULL;
      }
    }
  }

  graphics_context_set_fill_color(ctx, comma_palette_digit_fill());
  const GColor base_stroke = comma_palette_digit_stroke();

  const CommaLayout *layout = comma_layout_get();
  int cell_col = layout->digit_start_col;
  const int cell_row = layout->digit_start_row;

  for (int slot = 0; slot < COMMA_TOTAL_GLYPHS; ++slot) {
    if (!prv_digit_present(state, slot)) {
      switch (slot) {
        case 0:
        case 1:
        case 3:
          cell_col += COMMA_DIGIT_WIDTH + COMMA_DIGIT_GAP;
          break;
        case 2:
          cell_col += COMMA_DIGIT_COLON_WIDTH + COMMA_DIGIT_GAP;
          break;
        default:
          break;
      }
      continue;
    }

    const int glyph_index = prv_glyph_for_slot(state, slot);
    const CommaGlyph *glyph = &COMMA_GLYPHS[glyph_index];
    prv_draw_glyph(ctx, glyph, cell_col, cell_row, base_stroke,
                   (const uint8_t (*)[COMMA_DIGIT_WIDTH])state->cell_level[slot]);

    switch (slot) {
      case 0:
      case 1:
      case 3:
        cell_col += COMMA_DIGIT_WIDTH + COMMA_DIGIT_GAP;
        break;
      case 2:
        cell_col += COMMA_DIGIT_COLON_WIDTH + COMMA_DIGIT_GAP;
        break;
      default:
        break;
    }
  }
}

static void prv_start_animation(CommaDigitLayer *layer) {
  if (!layer || !layer->layer) {
    return;
  }
  CommaDigitLayerState *state = layer_get_data(layer->layer);
  if (!state) {
    return;
  }
  prv_zero_all_levels(state);
  if (state->anim_timer) {
    app_timer_cancel(state->anim_timer);
    state->anim_timer = NULL;
  }
  prv_schedule_anim_timer(layer);
}

CommaDigitLayer *comma_digit_layer_create(GRect frame) {
  CommaDigitLayer *layer = calloc(1, sizeof(*layer));
  if (!layer) {
    return NULL;
  }

  layer->layer = layer_create_with_data(frame, sizeof(CommaDigitLayerState));
  if (!layer->layer) {
    free(layer);
    return NULL;
  }

  layer->state = layer_get_data(layer->layer);
  layer->state->use_24h_time = clock_is_24h_style();
  layer->state->anim_timer = NULL;
  layer->state->reveal_complete = false;
  layer->state->background = NULL;
  for (int i = 0; i < COMMA_DIGIT_COUNT; ++i) {
    layer->state->digits[i] = -1;
  }
  prv_zero_all_levels(layer->state);

  layer_set_update_proc(layer->layer, prv_digit_layer_update_proc);
  return layer;
}

void comma_digit_layer_destroy(CommaDigitLayer *layer) {
  if (!layer) {
    return;
  }
  if (layer->state && layer->state->anim_timer) {
    app_timer_cancel(layer->state->anim_timer);
    layer->state->anim_timer = NULL;
  }
  if (layer->layer) {
    layer_destroy(layer->layer);
  }
  free(layer);
}

Layer *comma_digit_layer_get_layer(CommaDigitLayer *layer) {
  return layer ? layer->layer : NULL;
}

void comma_digit_layer_bind_background(CommaDigitLayer *layer,
                                        CommaBackgroundLayer *background) {
  CommaDigitLayerState *state = prv_get_state(layer);
  if (!state) {
    return;
  }
  state->background = background;
  prv_start_animation(layer);
  if (layer && layer->layer) {
    layer_mark_dirty(layer->layer);
  }
}

void comma_digit_layer_set_time(CommaDigitLayer *layer,
                                 const struct tm *time_info) {
  CommaDigitLayerState *state = prv_get_state(layer);
  if (!state || !time_info) {
    return;
  }

  const bool use_24h = clock_is_24h_style();
  int hour = time_info->tm_hour;
  if (!use_24h) {
    hour %= 12;
    if (hour == 0) {
      hour = 12;
    }
  }

  int16_t new_digits[COMMA_DIGIT_COUNT] = {
      hour / 10,
      hour % 10,
      time_info->tm_min / 10,
      time_info->tm_min % 10,
  };

  if (!use_24h && hour < 10) {
    new_digits[0] = -1;
  }

  bool changed = (state->use_24h_time != use_24h);
  for (int i = 0; i < COMMA_DIGIT_COUNT; ++i) {
    if (state->digits[i] != new_digits[i]) {
      state->digits[i] = new_digits[i];
      changed = true;
    }
  }

  if (!changed) {
    return;
  }

  state->use_24h_time = use_24h;
  prv_start_animation(layer);
  if (layer && layer->layer) {
    layer_mark_dirty(layer->layer);
  }
}

void comma_digit_layer_refresh_time(CommaDigitLayer *layer) {
  time_t now = time(NULL);
  struct tm *time_info = localtime(&now);
  if (!time_info) {
    return;
  }
  comma_digit_layer_set_time(layer, time_info);
}

void comma_digit_layer_force_redraw(CommaDigitLayer *layer) {
  if (layer && layer->layer) {
    layer_mark_dirty(layer->layer);
  }
}

void comma_digit_layer_start_diag_flip(CommaDigitLayer *layer) {
  prv_start_animation(layer);
  comma_digit_layer_force_redraw(layer);
}
