#include "comma_background_layer.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "comma_glyphs.h"
#include "comma_layout.h"
#include "comma_palette.h"

#define COMMA_BG_FRAME_MS 16 /* target ~60fps */
#if defined(PBL_PLATFORM_EMERY)
#define COMMA_BG_CELL_ANIM_MS 728
#define COMMA_BG_CELL_STAGGER_MIN_MS 0
#define COMMA_BG_CELL_STAGGER_MAX_MS 235
#define COMMA_BG_ACTIVATION_DURATION_MS 291
#else
#define COMMA_BG_CELL_ANIM_MS 1300
#define COMMA_BG_CELL_STAGGER_MIN_MS 0
#define COMMA_BG_CELL_STAGGER_MAX_MS 420
#define COMMA_BG_ACTIVATION_DURATION_MS 520
#endif
#define COMMA_BG_ACTIVE_PERCENT 18
#define COMMA_BG_ACTIVE_DIGIT_PERCENT 100
#define COMMA_BG_INTRO_DELAY_MS 120

typedef struct {
  int32_t elapsed_ms;
  int32_t start_delay_ms;
  bool complete;
  bool active;
  bool is_digit;
} CommaBackgroundCellState;

typedef struct {
  CommaBackgroundCellState cells[COMMA_BG_CELL_CAPACITY];
  bool animation_complete;
  bool intro_complete;
  int32_t intro_elapsed_ms;
  int32_t activation_window_ms;
  float activation_ratio;
} CommaBackgroundLayerState;

struct CommaBackgroundLayer {
  Layer *layer;
  CommaBackgroundLayerState *state;
  AppTimer *timer;
};

static inline CommaBackgroundLayerState *prv_get_state(CommaBackgroundLayer *layer) {
  return (layer && layer->layer) ? layer->state : NULL;
}

static inline int prv_cell_index(int col, int row) {
  return (row * COMMA_BG_MAX_COLS) + col;
}

static float prv_ease(float t) {
  if (t < 0.0f) {
    t = 0.0f;
  } else if (t > 1.0f) {
    t = 1.0f;
  }
  const float inv = 1.0f - t;
  return 1.0f - (inv * inv * inv);
}

static void prv_seed_random(void) {
  static bool s_seeded = false;
  if (s_seeded) {
    return;
  }
  s_seeded = true;
  srand((unsigned int)time(NULL));
}

static int32_t prv_random_range(int32_t min_inclusive, int32_t max_inclusive) {
  if (max_inclusive <= min_inclusive) {
    return min_inclusive;
  }
  const int32_t span = max_inclusive - min_inclusive + 1;
  return min_inclusive + (rand() % span);
}

static float prv_digit_center_col(const CommaLayout *layout) {
  return layout->digit_start_col + ((COMMA_DIGIT_SPAN_COLS - 1) / 2.0f);
}

static float prv_digit_center_row(const CommaLayout *layout) {
  return layout->digit_start_row + ((COMMA_DIGIT_HEIGHT - 1) / 2.0f);
}

static float prv_cell_bias(int cell_col, int cell_row,
                           const CommaLayout *layout) {
  const float col_center = prv_digit_center_col(layout);
  const float row_center = prv_digit_center_row(layout);

  const float col_half_span = (COMMA_DIGIT_SPAN_COLS / 2.0f);
  const float row_half_span = (COMMA_DIGIT_HEIGHT / 2.0f);

  const float col_dist =
      fabsf(((float)cell_col) - col_center) / (col_half_span + 1.0f);
  const float row_dist =
      fabsf(((float)cell_row) - row_center) / (row_half_span + 1.0f);

  float bias = 0.0f;
  if (col_dist < 1.0f) {
    bias += (1.0f - col_dist);
  }
  if (row_dist < 1.0f) {
    bias += (1.0f - row_dist) * 0.8f;
  }
  bias /= 1.8f;
  if (bias < 0.0f) {
    bias = 0.0f;
  } else if (bias > 1.0f) {
    bias = 1.0f;
  }
  return bias;
}

static bool prv_cell_is_digit(int cell_col, int cell_row,
                              const CommaLayout *layout) {
  const int rel_row = cell_row - layout->digit_start_row;
  if (rel_row < 0 || rel_row >= COMMA_DIGIT_HEIGHT) {
    return false;
  }

  int slot_col = layout->digit_start_col;
  for (int slot = 0; slot < COMMA_TOTAL_GLYPHS; ++slot) {
    const bool is_colon = (slot == 2);
    const int width = is_colon ? COMMA_DIGIT_COLON_WIDTH : COMMA_DIGIT_WIDTH;
    if (cell_col >= slot_col && cell_col < slot_col + width) {
      const int rel_col = cell_col - slot_col;
      if (is_colon) {
        const CommaGlyph *glyph = &COMMA_GLYPHS[COMMA_GLYPH_COLON];
        return glyph->rows[rel_row] &
               (1 << (glyph->width - 1 - rel_col));
      }
      const int bit = 1 << (COMMA_DIGIT_WIDTH - 1 - rel_col);
      for (int glyph = COMMA_GLYPH_ZERO; glyph <= COMMA_GLYPH_NINE; ++glyph) {
        const CommaGlyph *g = &COMMA_GLYPHS[glyph];
        if (g->rows[rel_row] & bit) {
          return true;
        }
      }
      return false;
    }
    slot_col += width;
    if (slot < COMMA_TOTAL_GLYPHS - 1) {
      slot_col += COMMA_DIGIT_GAP;
    }
  }
  return false;
}

static void prv_reset_cell(CommaBackgroundCellState *cell) {
  if (!cell) {
    return;
  }
  if (!cell->active) {
    cell->elapsed_ms = 0;
    cell->start_delay_ms = 0;
    cell->complete = true;
    return;
  }
  cell->elapsed_ms = 0;
  cell->complete = false;
  cell->start_delay_ms =
      prv_random_range(COMMA_BG_CELL_STAGGER_MIN_MS, COMMA_BG_CELL_STAGGER_MAX_MS);
}

static void prv_init_cells(CommaBackgroundLayerState *state) {
  if (!state) {
    return;
  }
  const CommaLayout *layout = comma_layout_get();
  const int grid_cols = layout->grid_cols;
  const int grid_rows = layout->grid_rows;
  memset(state->cells, 0, sizeof(state->cells));
  state->animation_complete = false;
  state->intro_complete = false;
  state->intro_elapsed_ms = 0;
  state->activation_window_ms = COMMA_BG_CELL_STAGGER_MIN_MS;
  state->activation_ratio = 0.0f;
  for (int row = 0; row < grid_rows; ++row) {
    for (int col = 0; col < grid_cols; ++col) {
      const int idx = prv_cell_index(col, row);
      CommaBackgroundCellState *cell = &state->cells[idx];
      const bool is_digit = prv_cell_is_digit(col, row, layout);
      cell->is_digit = is_digit;
      if (is_digit) {
        cell->active = true;
      } else {
        int percent = COMMA_BG_ACTIVE_PERCENT +
                      (int)(prv_cell_bias(col, row, layout) * 32.0f);
        if (percent > 100) {
          percent = 100;
        }
        cell->active = (rand() % 100) < percent;
      }
      prv_reset_cell(cell);
    }
  }
}

#if COMMA_CELL_SIZE == 6
static void prv_draw_row_span(GContext *ctx, const GPoint origin, int row,
                              int col_start, int col_end) {
  if (col_end < col_start) {
    return;
  }
  for (int col = col_start; col <= col_end; ++col) {
    graphics_draw_pixel(ctx, GPoint(origin.x + col, origin.y + row));
  }
}
#endif

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

static void prv_draw_background_shape(GContext *ctx, const GPoint origin,
                                      int size_level) {
#if COMMA_CELL_SIZE == 6
  switch (size_level) {
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
    case 0:
      for (int row = 2; row <= 3; ++row) {
        prv_draw_row_span(ctx, origin, row, 2, 3);
      }
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
      const int core_w = (size >= 8) ? 4 : core_size;
      const int core_h = (size >= 8) ? 4 : core_size;
      const int start_col = (size - core_w) / 2;
      const int start_row = (size - core_h) / 2;
      prv_fill_block(ctx, origin, start_row, start_row + core_h - 1,
                     start_col, start_col + core_w - 1);
      break;
    }
    default:
      break;
  }
#endif
}

static void prv_draw_background_cell(GContext *ctx, int cell_col, int cell_row,
                                     int size_level) {
  if (size_level < 0) {
    return;
  }
  const GRect frame = comma_cell_frame(cell_col, cell_row);
  prv_draw_background_shape(ctx, frame.origin, size_level);
}

static bool prv_cell_progress_value(const CommaBackgroundLayerState *state,
                                    const CommaBackgroundCellState *cell,
                                    float *progress_out) {
  if (!state || !cell || !progress_out) {
    return false;
  }

  if (!state->intro_complete ||
      cell->start_delay_ms > state->activation_window_ms) {
    *progress_out = 0.0f;
    return false;
  }

  if (!cell->active) {
    *progress_out = 0.0f;
    return false;
  }

  int32_t local = cell->elapsed_ms - cell->start_delay_ms;
  if (local <= 0 && !cell->complete) {
    *progress_out = 0.0f;
    return false;
  }

  if (local < 0) {
    local = 0;
  }
  const int32_t total = COMMA_BG_CELL_ANIM_MS;
  if (local > total) {
    local = total;
  }
  float progress = (float)local / (float)total;
  *progress_out = prv_ease(progress);
  return true;
}

static int prv_shape_level_for_progress(float progress) {
  if (progress <= 0.0f) {
    return -1;
  }
  if (progress < 0.28f) {
    return 2;
  }
  if (progress < 0.6f) {
    return 1;
  }
  if (progress < 0.92f) {
    return 0;
  }
  return -1;
}

static GColor prv_color_from_stage(int stage) {
#if defined(PBL_COLOR)
  switch (stage) {
    case 0:
      return GColorFromRGB(0x55, 0x55, 0x55);
    case 1:
      return GColorFromRGB(0xAA, 0xAA, 0xAA);
    default:
      return GColorFromRGB(0xFF, 0xFF, 0xFF);
  }
#else
  (void)stage;
  return GColorWhite;
#endif
}

static GColor prv_color_for_progress(float progress, bool is_digit) {
  if (progress < 0.0f) {
    progress = 0.0f;
  } else if (progress > 1.0f) {
    progress = 1.0f;
  }
  if (is_digit) {
    if (progress < (1.0f / 3.0f)) {
      return prv_color_from_stage(0);
    } else if (progress < (2.0f / 3.0f)) {
      return prv_color_from_stage(1);
    }
    return prv_color_from_stage(2);
  }

  if (progress < 0.5f) {
    const float phase = progress / 0.5f;
    if (phase < (1.0f / 3.0f)) {
      return prv_color_from_stage(0);
    } else if (phase < (2.0f / 3.0f)) {
      return prv_color_from_stage(1);
    }
    return prv_color_from_stage(2);
  }
  const float phase = (progress - 0.5f) / 0.5f;
  if (phase < (1.0f / 3.0f)) {
    return prv_color_from_stage(2);
  } else if (phase < (2.0f / 3.0f)) {
    return prv_color_from_stage(1);
  }
  return prv_color_from_stage(0);
}

static bool prv_step_animation(CommaBackgroundLayer *layer) {
  CommaBackgroundLayerState *state = prv_get_state(layer);
  if (!state) {
    return true;
  }

  if (!state->intro_complete) {
    state->intro_elapsed_ms += COMMA_BG_FRAME_MS;
    if (state->intro_elapsed_ms >= COMMA_BG_INTRO_DELAY_MS) {
      state->intro_complete = true;
      state->activation_window_ms = COMMA_BG_CELL_STAGGER_MIN_MS;
    }
    return false;
  }

  if (state->activation_ratio < 1.0f &&
      COMMA_BG_ACTIVATION_DURATION_MS > 0) {
    state->activation_ratio +=
        (float)COMMA_BG_FRAME_MS / (float)COMMA_BG_ACTIVATION_DURATION_MS;
    if (state->activation_ratio > 1.0f) {
      state->activation_ratio = 1.0f;
    }
    const float eased =
        prv_ease(state->activation_ratio);
    const int span =
        COMMA_BG_CELL_STAGGER_MAX_MS - COMMA_BG_CELL_STAGGER_MIN_MS;
    state->activation_window_ms =
        COMMA_BG_CELL_STAGGER_MIN_MS + (int)((float)span * eased);
  }

  const CommaLayout *layout = comma_layout_get();
  bool all_complete = true;
  for (int row = 0; row < layout->grid_rows; ++row) {
    for (int col = 0; col < layout->grid_cols; ++col) {
      CommaBackgroundCellState *cell =
          &state->cells[prv_cell_index(col, row)];
    if (!cell->active) {
      continue;
    }
    if (cell->start_delay_ms > state->activation_window_ms) {
      all_complete = false;
      continue;
    }
    const int32_t max_elapsed =
        cell->start_delay_ms + COMMA_BG_CELL_ANIM_MS;
    if (cell->complete && cell->elapsed_ms >= max_elapsed) {
      continue;
    }

    all_complete = false;
    cell->elapsed_ms += COMMA_BG_FRAME_MS;
    if (cell->elapsed_ms >= max_elapsed) {
      cell->elapsed_ms = max_elapsed;
      cell->complete = true;
      }
    }
  }

  if (all_complete) {
    state->animation_complete = true;
  }
  return state->animation_complete;
}

static void prv_timer_proc(void *ctx);

static void prv_schedule_timer(CommaBackgroundLayer *layer) {
  if (!layer) {
    return;
  }
  CommaBackgroundLayerState *state = prv_get_state(layer);
  if (state && state->animation_complete) {
    layer->timer = NULL;
    return;
  }
  layer->timer = app_timer_register(COMMA_BG_FRAME_MS, prv_timer_proc, layer);
}

static void prv_timer_proc(void *ctx) {
  CommaBackgroundLayer *layer = ctx;
  if (!layer || !layer->layer) {
    return;
  }
  const bool done = prv_step_animation(layer);
  layer_mark_dirty(layer->layer);
  if (!done) {
    prv_schedule_timer(layer);
  } else {
    layer->timer = NULL;
  }
}

static void prv_start_animation(CommaBackgroundLayer *layer) {
  if (!layer) {
    return;
  }
  if (layer->timer) {
    app_timer_cancel(layer->timer);
    layer->timer = NULL;
  }
  CommaBackgroundLayerState *state = prv_get_state(layer);
  if (state) {
    prv_init_cells(state);
  }
  prv_schedule_timer(layer);
}

static void prv_stop_animation(CommaBackgroundLayer *layer) {
  if (!layer || !layer->timer) {
    return;
  }
  app_timer_cancel(layer->timer);
  layer->timer = NULL;
}

static void prv_background_update_proc(Layer *layer_ref, GContext *ctx) {
  CommaBackgroundLayerState *state = layer_get_data(layer_ref);
  if (!state) {
    return;
  }

  const GRect bounds = layer_get_bounds(layer_ref);
  graphics_context_set_fill_color(ctx, comma_palette_background_fill());
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  const CommaLayout *layout = comma_layout_get();
  graphics_context_set_stroke_color(ctx, comma_palette_background_stroke());
  for (int row = 0; row < layout->grid_rows; ++row) {
    for (int col = 0; col < layout->grid_cols; ++col) {
      prv_draw_background_cell(ctx, col, row, 0);
    }
  }

  for (int row = 0; row < layout->grid_rows; ++row) {
    for (int col = 0; col < layout->grid_cols; ++col) {
      CommaBackgroundCellState *cell =
          &state->cells[prv_cell_index(col, row)];
      float progress = 0.0f;
      if (!prv_cell_progress_value(state, cell, &progress)) {
        continue;
      }
      const int size_level = prv_shape_level_for_progress(progress);
      if (size_level < 0) {
        continue;
      }
      const GColor color = prv_color_for_progress(progress, cell->is_digit);
      graphics_context_set_stroke_color(ctx, color);
      prv_draw_background_cell(ctx, col, row, size_level);
    }
  }
}

CommaBackgroundLayer *comma_background_layer_create(GRect frame) {
  prv_seed_random();

  CommaBackgroundLayer *layer = calloc(1, sizeof(*layer));
  if (!layer) {
    return NULL;
  }

  layer->layer =
      layer_create_with_data(frame, sizeof(CommaBackgroundLayerState));
  if (!layer->layer) {
    free(layer);
    return NULL;
  }

  layer->state = layer_get_data(layer->layer);
  prv_init_cells(layer->state);

  layer_set_update_proc(layer->layer, prv_background_update_proc);
  prv_start_animation(layer);
  return layer;
}

void comma_background_layer_destroy(CommaBackgroundLayer *layer) {
  if (!layer) {
    return;
  }

  prv_stop_animation(layer);

  if (layer->layer) {
    layer_destroy(layer->layer);
    layer->layer = NULL;
    layer->state = NULL;
  }
  free(layer);
}

Layer *comma_background_layer_get_layer(CommaBackgroundLayer *layer) {
  return layer ? layer->layer : NULL;
}

void comma_background_layer_mark_dirty(CommaBackgroundLayer *layer) {
  if (layer && layer->layer) {
    layer_mark_dirty(layer->layer);
  }
}

bool comma_background_layer_cell_progress(CommaBackgroundLayer *layer,
                                          int cell_col,
                                          int cell_row,
                                          float *progress_out) {
  CommaBackgroundLayerState *state = prv_get_state(layer);
  if (!state || !progress_out) {
    return false;
  }
  const CommaLayout *layout = comma_layout_get();
  if (cell_col < 0 || cell_col >= layout->grid_cols || cell_row < 0 ||
      cell_row >= layout->grid_rows) {
    *progress_out = 0.0f;
    return false;
  }
  const int idx = prv_cell_index(cell_col, cell_row);
  const CommaBackgroundCellState *cell = &state->cells[idx];
  return prv_cell_progress_value(state, cell, progress_out);
}
