#pragma once

#include <pebble.h>

#if defined(PBL_PLATFORM_EMERY)
#define COMMA_CELL_SIZE 8
#else
#define COMMA_CELL_SIZE 6
#endif

enum {
  COMMA_DIGIT_WIDTH = 4,
  COMMA_DIGIT_HEIGHT = 9,
  COMMA_DIGIT_COLON_WIDTH = 2,
  COMMA_DIGIT_COUNT = 4,
  COMMA_DIGIT_GAP = 1,
};

#define COMMA_TOTAL_GLYPHS (COMMA_DIGIT_COUNT + 1)
#define COMMA_DIGIT_SPAN_COLS \
  ((COMMA_DIGIT_WIDTH * COMMA_DIGIT_COUNT) + COMMA_DIGIT_COLON_WIDTH + \
   (COMMA_DIGIT_GAP * (COMMA_TOTAL_GLYPHS - 1)))

#define COMMA_BG_MAX_COLS 35
#define COMMA_BG_MAX_ROWS 40
#define COMMA_BG_CELL_CAPACITY (COMMA_BG_MAX_COLS * COMMA_BG_MAX_ROWS)

typedef struct {
  int grid_cols;
  int grid_rows;
  int digit_start_col;
  int digit_start_row;
  int offset_x;
  int offset_y;
} CommaLayout;

void comma_layout_configure(GSize bounds);
const CommaLayout *comma_layout_get(void);

static inline GPoint comma_cell_origin(int cell_col, int cell_row) {
  const CommaLayout *layout = comma_layout_get();
  return GPoint(layout->offset_x + (cell_col * COMMA_CELL_SIZE),
                layout->offset_y + (cell_row * COMMA_CELL_SIZE));
}

static inline GRect comma_cell_frame(int cell_col, int cell_row) {
  const CommaLayout *layout = comma_layout_get();
  return GRect(layout->offset_x + (cell_col * COMMA_CELL_SIZE),
               layout->offset_y + (cell_row * COMMA_CELL_SIZE),
               COMMA_CELL_SIZE, COMMA_CELL_SIZE);
}
