#include <math.h>
#include <pebble.h>
#include <time.h>

#include "general_magic_background_layer.h"
#include "general_magic_digit_layer.h"
#include "general_magic_layout.h"
#include "general_magic_palette.h"

static Window *s_main_window;
static GeneralMagicBackgroundLayer *s_background_layer;
static GeneralMagicDigitLayer *s_digit_layer;

typedef enum {
  GENERAL_MAGIC_HOURLY_CHIME_STRENGTH_LIGHT = 0,
  GENERAL_MAGIC_HOURLY_CHIME_STRENGTH_MEDIUM = 1,
  GENERAL_MAGIC_HOURLY_CHIME_STRENGTH_HARD = 2,
  GENERAL_MAGIC_HOURLY_CHIME_STRENGTH_COUNT
} GeneralMagicHourlyChimeStrength;

typedef struct {
  bool use_24h_time;
  GeneralMagicTheme theme;
  bool vibration_enabled;
  bool animations_enabled;
  bool vibrate_on_open;
  bool hourly_chime;
  GeneralMagicHourlyChimeStrength hourly_chime_strength;
} GeneralMagicSettings;

static GeneralMagicSettings s_settings;
static int s_last_chime_hour = -1;

enum {
  GENERAL_MAGIC_SETTINGS_PERSIST_KEY = 1,
};

static AppTimer *s_intro_vibe_timer;

static const uint32_t s_intro_vibe_segments_base[] = {
    /* gradual warm-up */
    22, 224,
    26, 190,
    30, 176,
    /* ramp into the main sweep */
    35, 157,
    41, 142,
    46, 128,
    50, 115,
    /* hit current peak intensity */
    55, 111,
    60, 194,
    /* gentle release (longer tail) */
    66, 267,
    71, 343,
    76, 472,
};

static uint32_t s_intro_vibe_segments_scaled[ARRAY_LENGTH(s_intro_vibe_segments_base)];

static const uint32_t s_hourly_chime_segments_base[] = {
    /* Apple-ish spaced double tap: crisp start + delayed accent */
    30, 150, 42, 360,
};
static uint32_t
    s_hourly_chime_segments_scaled[GENERAL_MAGIC_HOURLY_CHIME_STRENGTH_COUNT]
                                  [ARRAY_LENGTH(s_hourly_chime_segments_base)];
static bool s_hourly_chime_segments_ready;

static bool prv_vibes_allowed(void) {
  return s_settings.vibration_enabled && !quiet_time_is_active();
}

static GeneralMagicHourlyChimeStrength prv_clamp_hourly_strength(int value) {
  if (value < GENERAL_MAGIC_HOURLY_CHIME_STRENGTH_LIGHT ||
      value >= GENERAL_MAGIC_HOURLY_CHIME_STRENGTH_COUNT) {
    return GENERAL_MAGIC_HOURLY_CHIME_STRENGTH_MEDIUM;
  }
  return (GeneralMagicHourlyChimeStrength)value;
}

static void prv_prepare_hourly_chime_segments(void) {
  if (s_hourly_chime_segments_ready) {
    return;
  }
  static const float s_multipliers[GENERAL_MAGIC_HOURLY_CHIME_STRENGTH_COUNT] = {
      0.85f,
      1.0f,
      1.3f,
  };
  for (int strength = 0; strength < GENERAL_MAGIC_HOURLY_CHIME_STRENGTH_COUNT;
       ++strength) {
    for (size_t idx = 0; idx < ARRAY_LENGTH(s_hourly_chime_segments_base); ++idx) {
      const bool is_vibe_segment = (idx % 2) == 0;
      float value = (float)s_hourly_chime_segments_base[idx];
      if (is_vibe_segment) {
        value *= s_multipliers[strength];
      }
      if (value < 1.0f) {
        value = 1.0f;
      }
      s_hourly_chime_segments_scaled[strength][idx] = (uint32_t)roundf(value);
    }
  }
  s_hourly_chime_segments_ready = true;
}

static void prv_cancel_intro_vibe_timer(void) {
  if (s_intro_vibe_timer) {
    app_timer_cancel(s_intro_vibe_timer);
    s_intro_vibe_timer = NULL;
  }
}

static uint32_t prv_intro_vibe_pattern_base_total(void) {
  static uint32_t s_total = 0;
  if (s_total == 0) {
    for (size_t i = 0; i < ARRAY_LENGTH(s_intro_vibe_segments_base); ++i) {
      s_total += s_intro_vibe_segments_base[i];
    }
  }
  return s_total;
}

static void prv_prepare_intro_vibe_pattern(uint32_t target_duration_ms) {
  const uint32_t base_total = prv_intro_vibe_pattern_base_total();
  float scale = 1.0f;
  if (base_total > 0) {
    scale = (float)target_duration_ms / (float)base_total;
  }
  if (scale < 0.3f) {
    scale = 0.3f;
  }
  for (size_t i = 0; i < ARRAY_LENGTH(s_intro_vibe_segments_base); ++i) {
    float scaled = (float)s_intro_vibe_segments_base[i] * scale;
    if (scaled < 1.0f) {
      scaled = 1.0f;
    }
    s_intro_vibe_segments_scaled[i] = (uint32_t)roundf(scaled);
  }
}

static void prv_intro_vibe_fire(void *context) {
  (void)context;
  s_intro_vibe_timer = NULL;
  if (!s_settings.vibrate_on_open || !prv_vibes_allowed()) {
    return;
  }
  const VibePattern pattern = {
      .durations = s_intro_vibe_segments_scaled,
      .num_segments = ARRAY_LENGTH(s_intro_vibe_segments_scaled),
  };
  vibes_cancel();
  vibes_enqueue_custom_pattern(pattern);
}

static void prv_play_intro_vibe(void) {
  if (!s_settings.vibrate_on_open || !prv_vibes_allowed()) {
    return;
  }
  prv_cancel_intro_vibe_timer();
  GeneralMagicBackgroundTiming timing = {
      .intro_delay_ms = GENERAL_MAGIC_BG_BASE_INTRO_DELAY_MS,
      .cell_anim_ms = GENERAL_MAGIC_BG_BASE_CELL_ANIM_MS,
      .activation_duration_ms = GENERAL_MAGIC_BG_BASE_ACTIVATION_DURATION_MS,
  };
  if (!general_magic_background_layer_get_timing(s_background_layer, &timing)) {
    timing.intro_delay_ms = GENERAL_MAGIC_BG_BASE_INTRO_DELAY_MS;
    timing.cell_anim_ms = GENERAL_MAGIC_BG_BASE_CELL_ANIM_MS;
  }
  const float lead_ratio = 0.1f;
  const float trail_ratio = 0.1f;
  const uint32_t target_duration_ms = timing.cell_anim_ms + timing.intro_delay_ms;
  const uint32_t lead_ms = (uint32_t)roundf((float)target_duration_ms * lead_ratio);
  const uint32_t trail_ms = (uint32_t)roundf((float)target_duration_ms * trail_ratio);
  const uint32_t extended_duration_ms = target_duration_ms + lead_ms + trail_ms;
  prv_prepare_intro_vibe_pattern(extended_duration_ms);
  int32_t desired_delay = (int32_t)GENERAL_MAGIC_BG_FRAME_MS - (int32_t)lead_ms;
  if (desired_delay < 0) {
    desired_delay = 0;
  }
  s_intro_vibe_timer = app_timer_register((uint32_t)desired_delay, prv_intro_vibe_fire, NULL);
}

static void prv_play_hourly_chime(void) {
  if (!s_settings.hourly_chime || !prv_vibes_allowed()) {
    return;
  }
  prv_prepare_hourly_chime_segments();
  const GeneralMagicHourlyChimeStrength strength =
      prv_clamp_hourly_strength(s_settings.hourly_chime_strength);
  const uint32_t *segments = s_hourly_chime_segments_scaled[strength];
  const VibePattern pattern = {
      .durations = segments,
      .num_segments = ARRAY_LENGTH(s_hourly_chime_segments_base),
  };
  vibes_enqueue_custom_pattern(pattern);
}

static void prv_maybe_trigger_hourly_chime(struct tm *tick_time) {
  if (!s_settings.hourly_chime) {
    s_last_chime_hour = -1;
    return;
  }
  if (!tick_time) {
    time_t now = time(NULL);
    tick_time = localtime(&now);
  }
  if (!tick_time) {
    return;
  }
  if (tick_time->tm_min != 0) {
    s_last_chime_hour = -1;
    return;
  }
  if (s_last_chime_hour == tick_time->tm_hour) {
    return;
  }
  prv_play_hourly_chime();
  s_last_chime_hour = tick_time->tm_hour;
}

static void prv_set_default_settings(void) {
  s_settings.use_24h_time = clock_is_24h_style();
  s_settings.theme = GENERAL_MAGIC_THEME_DARK;
  s_settings.vibration_enabled = true;
  s_settings.animations_enabled = true;
  s_settings.vibrate_on_open = true;
  s_settings.hourly_chime = false;
  s_settings.hourly_chime_strength = GENERAL_MAGIC_HOURLY_CHIME_STRENGTH_MEDIUM;
}

static void prv_load_settings(void) {
  prv_set_default_settings();
  if (!persist_exists(GENERAL_MAGIC_SETTINGS_PERSIST_KEY)) {
    return;
  }
  GeneralMagicSettings stored = s_settings;
  const int read =
      persist_read_data(GENERAL_MAGIC_SETTINGS_PERSIST_KEY, &stored, sizeof(stored));
  if (read > 0) {
    s_settings = stored;
    s_settings.hourly_chime_strength =
        prv_clamp_hourly_strength(s_settings.hourly_chime_strength);
  }
}

static void prv_save_settings(void) {
  persist_write_data(GENERAL_MAGIC_SETTINGS_PERSIST_KEY, &s_settings, sizeof(s_settings));
}

static void prv_apply_theme(void) {
  general_magic_palette_set_theme(s_settings.theme);
  if (s_main_window) {
    window_set_background_color(s_main_window, general_magic_palette_window_background());
  }
  if (s_background_layer) {
    general_magic_background_layer_mark_dirty(s_background_layer);
  }
  if (s_digit_layer) {
    general_magic_digit_layer_force_redraw(s_digit_layer);
  }
}

static void prv_apply_time_format(void) {
  if (s_digit_layer) {
    general_magic_digit_layer_set_use_24h(s_digit_layer, s_settings.use_24h_time);
  }
}

static void prv_prepare_animation_layers(void) {
  if (s_background_layer) {
    general_magic_background_layer_set_animated(s_background_layer, false);
  }
  if (s_digit_layer) {
    general_magic_digit_layer_set_static_display(s_digit_layer, true);
    general_magic_digit_layer_stop_animation(s_digit_layer);
    general_magic_digit_layer_force_redraw(s_digit_layer);
  }
}

static void prv_apply_animation_state(void) {
  if (!s_digit_layer) {
    return;
  }
  if (s_background_layer) {
    general_magic_background_layer_set_animated(s_background_layer,
                                                s_settings.animations_enabled);
  }
  if (s_settings.animations_enabled) {
    general_magic_digit_layer_set_static_display(s_digit_layer, false);
    general_magic_digit_layer_start_diag_flip(s_digit_layer);
  } else {
    general_magic_digit_layer_set_static_display(s_digit_layer, true);
    general_magic_digit_layer_stop_animation(s_digit_layer);
    general_magic_digit_layer_force_redraw(s_digit_layer);
  }
}

static void prv_send_settings_to_phone(void) {
  DictionaryIterator *iter = NULL;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK || !iter) {
    return;
  }
  dict_write_uint8(iter, MESSAGE_KEY_TimeFormat, s_settings.use_24h_time ? 24 : 12);
  dict_write_uint8(iter, MESSAGE_KEY_Theme, (uint8_t)s_settings.theme);
  dict_write_uint8(iter, MESSAGE_KEY_Vibration, s_settings.vibration_enabled ? 1 : 0);
  dict_write_uint8(iter, MESSAGE_KEY_Animation, s_settings.animations_enabled ? 1 : 0);
  dict_write_uint8(iter, MESSAGE_KEY_VibrateOnOpen, s_settings.vibrate_on_open ? 1 : 0);
  dict_write_uint8(iter, MESSAGE_KEY_HourlyChime, s_settings.hourly_chime ? 1 : 0);
  dict_write_uint8(iter, MESSAGE_KEY_HourlyChimeStrength,
                   (uint8_t)prv_clamp_hourly_strength(s_settings.hourly_chime_strength));
  dict_write_end(iter);
  app_message_outbox_send();
}

static void prv_handle_settings_message(DictionaryIterator *iter) {
  bool updated = false;
  Tuple *tuple = dict_find(iter, MESSAGE_KEY_TimeFormat);
  if (tuple) {
    const bool use_24h = tuple->value->uint8 >= 24;
    if (s_settings.use_24h_time != use_24h) {
      s_settings.use_24h_time = use_24h;
      updated = true;
      prv_apply_time_format();
    }
  }

  tuple = dict_find(iter, MESSAGE_KEY_Theme);
  if (tuple) {
    const GeneralMagicTheme theme = (tuple->value->uint8 == GENERAL_MAGIC_THEME_LIGHT)
                                        ? GENERAL_MAGIC_THEME_LIGHT
                                        : GENERAL_MAGIC_THEME_DARK;
    if (s_settings.theme != theme) {
      s_settings.theme = theme;
      updated = true;
      prv_apply_theme();
    }
  }

  tuple = dict_find(iter, MESSAGE_KEY_Vibration);
  if (tuple) {
    const bool enabled = tuple->value->uint8 > 0;
    if (s_settings.vibration_enabled != enabled) {
      s_settings.vibration_enabled = enabled;
      updated = true;
    }
  }

  tuple = dict_find(iter, MESSAGE_KEY_Animation);
  if (tuple) {
    const bool enabled = tuple->value->uint8 > 0;
    if (s_settings.animations_enabled != enabled) {
      s_settings.animations_enabled = enabled;
      if (!enabled && s_settings.vibrate_on_open) {
        s_settings.vibrate_on_open = false;
        updated = true;
      }
      updated = true;
      prv_apply_animation_state();
    }
  }

  tuple = dict_find(iter, MESSAGE_KEY_VibrateOnOpen);
  if (tuple) {
    const bool enabled = tuple->value->uint8 > 0;
    if (s_settings.vibrate_on_open != enabled) {
      s_settings.vibrate_on_open = enabled;
      updated = true;
    }
  }

  tuple = dict_find(iter, MESSAGE_KEY_HourlyChime);
  if (tuple) {
    const bool enabled = tuple->value->uint8 > 0;
    if (s_settings.hourly_chime != enabled) {
      s_settings.hourly_chime = enabled;
      updated = true;
    }
  }

  tuple = dict_find(iter, MESSAGE_KEY_HourlyChimeStrength);
  if (tuple) {
    const GeneralMagicHourlyChimeStrength strength =
        prv_clamp_hourly_strength(tuple->value->uint8);
    if (s_settings.hourly_chime_strength != strength) {
      s_settings.hourly_chime_strength = strength;
      updated = true;
    }
  }

  if (dict_find(iter, MESSAGE_KEY_SettingsRequest)) {
    prv_send_settings_to_phone();
  }

  if (updated) {
    prv_save_settings();
    prv_send_settings_to_phone();
  }
}

static void prv_inbox_received(DictionaryIterator *iter, void *context) {
  (void)context;
  if (!iter) {
    return;
  }
  prv_handle_settings_message(iter);
}

static void prv_message_init(void) {
  app_message_register_inbox_received(prv_inbox_received);
  app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());
}

static void prv_tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  (void)units_changed;
  if (s_digit_layer) {
    general_magic_digit_layer_set_time(s_digit_layer, tick_time);
  }
  prv_maybe_trigger_hourly_chime(tick_time);
}

static void prv_window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  const GRect bounds = layer_get_bounds(root);

  general_magic_layout_configure(bounds.size);
  s_background_layer = general_magic_background_layer_create(bounds);
  if (s_background_layer) {
    layer_add_child(root, general_magic_background_layer_get_layer(s_background_layer));
  }

  s_digit_layer = general_magic_digit_layer_create(bounds);
  if (s_digit_layer) {
    layer_add_child(root, general_magic_digit_layer_get_layer(s_digit_layer));
    general_magic_digit_layer_bind_background(s_digit_layer, s_background_layer);
    general_magic_digit_layer_set_use_24h(s_digit_layer, s_settings.use_24h_time);
    general_magic_digit_layer_refresh_time(s_digit_layer);
  }

  prv_apply_theme();
  prv_prepare_animation_layers();
}

static void prv_window_unload(Window *window) {
  (void)window;

  prv_cancel_intro_vibe_timer();

  general_magic_digit_layer_destroy(s_digit_layer);
  s_digit_layer = NULL;

  general_magic_background_layer_destroy(s_background_layer);
  s_background_layer = NULL;
}

static void prv_window_appear(Window *window) {
  (void)window;
  prv_apply_animation_state();
  prv_play_intro_vibe();
}

static void prv_init(void) {
  prv_load_settings();
  general_magic_palette_set_theme(s_settings.theme);

  s_main_window = window_create();
  window_set_background_color(s_main_window, general_magic_palette_window_background());
  window_set_window_handlers(s_main_window, (WindowHandlers){
                                            .load = prv_window_load,
                                            .appear = prv_window_appear,
                                            .unload = prv_window_unload,
                                          });

  window_stack_push(s_main_window, true);

  prv_message_init();
  tick_timer_service_subscribe(MINUTE_UNIT, prv_tick_handler);
  prv_send_settings_to_phone();
}

static void prv_deinit(void) {
  tick_timer_service_unsubscribe();
  window_destroy(s_main_window);
  s_main_window = NULL;
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}
