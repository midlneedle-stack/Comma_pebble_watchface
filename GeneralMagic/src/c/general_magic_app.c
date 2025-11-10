#include <pebble.h>
#include <time.h>

#include "general_magic_background_layer.h"
#include "general_magic_digit_layer.h"
#include "general_magic_layout.h"
#include "general_magic_palette.h"

static Window *s_main_window;
static GeneralMagicBackgroundLayer *s_background_layer;
static GeneralMagicDigitLayer *s_digit_layer;

typedef struct {
  bool use_24h_time;
  GeneralMagicTheme theme;
  bool vibration_enabled;
  bool animations_enabled;
  bool vibrate_on_open;
  bool hourly_chime;
} GeneralMagicSettings;

static GeneralMagicSettings s_settings;
static int s_last_chime_hour = -1;

enum {
  GENERAL_MAGIC_SETTINGS_PERSIST_KEY = 1,
};

static const uint32_t s_intro_vibe_segments[] = {
    /* short warm-up pulses */
    18, 220,
    20, 180,
    22, 160,
    /* ramp into the main sweep */
    25, 140,
    28, 120,
    30, 110,
    32, 95,
    /* hit current peak intensity (34 ms) */
    34, 90,
    34, 160,
    /* gentle release */
    30, 200,
    24, 260,
    20, 360,
};

static const uint32_t s_hourly_chime_segments[] = {
    32, 70, 24, 110, 18, 200,
};

static bool prv_vibes_allowed(void) {
  return s_settings.vibration_enabled && !quiet_time_is_active();
}

static void prv_play_intro_vibe(void) {
  if (!s_settings.vibrate_on_open || !prv_vibes_allowed()) {
    return;
  }
  const VibePattern pattern = {
      .durations = s_intro_vibe_segments,
      .num_segments = ARRAY_LENGTH(s_intro_vibe_segments),
  };
  vibes_cancel();
  vibes_enqueue_custom_pattern(pattern);
}

static void prv_play_hourly_chime(void) {
  if (!s_settings.hourly_chime || !prv_vibes_allowed()) {
    return;
  }
  const VibePattern pattern = {
      .durations = s_hourly_chime_segments,
      .num_segments = ARRAY_LENGTH(s_hourly_chime_segments),
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
}

static void prv_load_settings(void) {
  prv_set_default_settings();
  if (!persist_exists(GENERAL_MAGIC_SETTINGS_PERSIST_KEY)) {
    return;
  }
  GeneralMagicSettings stored;
  const int read =
      persist_read_data(GENERAL_MAGIC_SETTINGS_PERSIST_KEY, &stored, sizeof(stored));
  if (read == (int)sizeof(stored)) {
    s_settings = stored;
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
  prv_apply_animation_state();
}

static void prv_window_unload(Window *window) {
  (void)window;

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
