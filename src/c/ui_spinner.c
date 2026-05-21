#include "ui_spinner.h"
#include "state.h"
#include "transport.h"
#include <pebble.h>

static Window    *s_window        = NULL;
static TextLayer *s_message_layer = NULL;
static TextLayer *s_elapsed_layer = NULL;
static TextLayer *s_hint_layer    = NULL;
static AppTimer  *s_tick_timer    = NULL;
static int        s_elapsed_secs  = 0;
static char s_message_buf[32];
static char s_elapsed_buf[16];

static void tick(void *ctx) {
  s_elapsed_secs++;
  snprintf(s_elapsed_buf, sizeof(s_elapsed_buf), "%ds", s_elapsed_secs);
  if (s_elapsed_layer) layer_mark_dirty(text_layer_get_layer(s_elapsed_layer));
  s_tick_timer = app_timer_register(1000, tick, NULL);
}

static void on_back(ClickRecognizerRef rec, void *ctx) {
  transport_send_cancel();
  state_set(STATE_IDLE);
}

static void click_config_provider(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_BACK, on_back);
}

static void window_load(Window *window) {
  Layer *root   = window_get_root_layer(window);
  GRect  bounds = layer_get_bounds(root);

  s_message_layer = text_layer_create(GRect(0, bounds.size.h / 2 - 30, bounds.size.w, 24));
  text_layer_set_text(s_message_layer, s_message_buf);
  text_layer_set_text_alignment(s_message_layer, GTextAlignmentCenter);
  text_layer_set_font(s_message_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  layer_add_child(root, text_layer_get_layer(s_message_layer));

  s_elapsed_layer = text_layer_create(GRect(0, bounds.size.h / 2, bounds.size.w, 24));
  text_layer_set_text(s_elapsed_layer, s_elapsed_buf);
  text_layer_set_text_alignment(s_elapsed_layer, GTextAlignmentCenter);
  text_layer_set_font(s_elapsed_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  layer_add_child(root, text_layer_get_layer(s_elapsed_layer));

  s_hint_layer = text_layer_create(GRect(0, bounds.size.h - 22, bounds.size.w, 20));
  text_layer_set_text(s_hint_layer, "BACK to cancel");
  text_layer_set_text_alignment(s_hint_layer, GTextAlignmentCenter);
  text_layer_set_font(s_hint_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  layer_add_child(root, text_layer_get_layer(s_hint_layer));
}

static void window_unload(Window *window) {
  if (s_message_layer) text_layer_destroy(s_message_layer);
  if (s_elapsed_layer) text_layer_destroy(s_elapsed_layer);
  if (s_hint_layer)    text_layer_destroy(s_hint_layer);
  s_message_layer = s_elapsed_layer = s_hint_layer = NULL;
}

void ui_spinner_init(void) {
  s_window = window_create();
  window_set_background_color(s_window, GColorWhite);
  window_set_click_config_provider(s_window, click_config_provider);
  window_set_window_handlers(s_window, (WindowHandlers){ .load = window_load, .unload = window_unload });
  s_message_buf[0] = '\0'; s_elapsed_buf[0] = '\0';
}

void ui_spinner_deinit(void) {
  if (s_tick_timer) { app_timer_cancel(s_tick_timer); s_tick_timer = NULL; }
  if (s_window) { window_destroy(s_window); s_window = NULL; }
}

void ui_spinner_show(const char *message) {
  snprintf(s_message_buf, sizeof(s_message_buf), "%s", message);
  s_elapsed_secs = 0;
  snprintf(s_elapsed_buf, sizeof(s_elapsed_buf), "0s");
  if (!window_stack_contains_window(s_window)) window_stack_push(s_window, true);
  if (s_tick_timer) app_timer_cancel(s_tick_timer);
  s_tick_timer = app_timer_register(1000, tick, NULL);
}

void ui_spinner_hide(void) {
  if (s_tick_timer) { app_timer_cancel(s_tick_timer); s_tick_timer = NULL; }
  if (window_stack_contains_window(s_window)) window_stack_remove(s_window, false);
}
