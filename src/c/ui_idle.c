#include "ui_idle.h"
#include "state.h"
#include "dictation.h"
#include "transport.h"
#include <pebble.h>

static Window    *s_window     = NULL;
static TextLayer *s_body_layer = NULL;
static char s_body_buf[128];

static const char *error_text_for(OwuiErrorCode code) {
  switch (code) {
    case ERR_PHONE_DISCONNECTED: return "Phone not connected";
    case ERR_NO_SPEECH:          return "Didn't hear anything";
    case ERR_RECOGNITION_FAILED: return "Could not transcribe";
    case ERR_SERVER_UNREACHABLE: return "Server unreachable";
    case ERR_BAD_API_KEY:        return "Bad API key";
    case ERR_ACCESS_DENIED:      return "Access denied";
    case ERR_SERVER_ERROR:       return "Server error";
    case ERR_TIMEOUT:            return "Timed out";
    case ERR_RESPONSE_TOO_LARGE: return "Response too long";
    case ERR_OUT_OF_MEMORY:      return "Out of memory";
    case ERR_TRANSPORT_FAILED:   return "Lost connection";
    case ERR_BUSY:               return "Busy, try again";
    default:                     return "Unknown error";
  }
}

static const char *provider_label(void) {
  return state_provider() == 1 ? "[Claude]" : "[Perplexity]";
}

static void refresh_text(void) {
  if (state_current() == STATE_ERROR) {
    OwuiErrorCode code = state_last_error();
    int ds = state_dictation_status();
    if (ds != 0 && (code == ERR_RECOGNITION_FAILED ||
                    code == ERR_NO_SPEECH ||
                    code == ERR_PHONE_DISCONNECTED)) {
      snprintf(s_body_buf, sizeof(s_body_buf), "%s\n(status %d)",
               error_text_for(code), ds);
    } else {
      snprintf(s_body_buf, sizeof(s_body_buf), "%s", error_text_for(code));
    }
  } else {
    snprintf(s_body_buf, sizeof(s_body_buf),
             "%s\nSELECT to talk\nUP/DOWN=AI", provider_label());
  }
  if (s_body_layer) layer_mark_dirty(text_layer_get_layer(s_body_layer));
}

static void on_select(ClickRecognizerRef rec, void *ctx) {
  if (state_current() == STATE_ERROR) {
    state_set(STATE_IDLE); refresh_text();
  } else if (state_current() == STATE_IDLE) {
    state_set(STATE_DICTATING);
    dictation_start();
  }
}

static void on_up(ClickRecognizerRef rec, void *ctx) {
  state_set_provider(0);       // Perplexity
  transport_send_provider(0);
  refresh_text();
}

static void on_down(ClickRecognizerRef rec, void *ctx) {
  state_set_provider(1);       // Claude
  transport_send_provider(1);
  refresh_text();
}

static void on_back(ClickRecognizerRef rec, void *ctx) {
  if (state_current() == STATE_ERROR) {
    state_set(STATE_IDLE); refresh_text();
  } else {
    window_stack_pop_all(true);
  }
}

static void click_config_provider(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_SELECT, on_select);
  window_single_click_subscribe(BUTTON_ID_UP,     on_up);
  window_single_click_subscribe(BUTTON_ID_DOWN,   on_down);
  window_single_click_subscribe(BUTTON_ID_BACK,   on_back);
}

static void window_load(Window *window) {
  Layer *root   = window_get_root_layer(window);
  GRect  bounds = layer_get_bounds(root);
  const int16_t body_h = 90;
  int16_t body_y = (bounds.size.h - body_h) / 2;
  if (body_y < 0) body_y = 0;
  s_body_layer = text_layer_create(GRect(0, body_y, bounds.size.w, body_h));
  text_layer_set_text(s_body_layer, s_body_buf);
  text_layer_set_text_alignment(s_body_layer, GTextAlignmentCenter);
  text_layer_set_font(s_body_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_overflow_mode(s_body_layer, GTextOverflowModeWordWrap);
  layer_add_child(root, text_layer_get_layer(s_body_layer));
}

static void window_unload(Window *window) {
  if (s_body_layer) text_layer_destroy(s_body_layer);
  s_body_layer = NULL;
}

void ui_idle_init(void) {
  s_window = window_create();
  window_set_background_color(s_window, GColorWhite);
  window_set_click_config_provider(s_window, click_config_provider);
  window_set_window_handlers(s_window, (WindowHandlers){ .load = window_load, .unload = window_unload });
  s_body_buf[0] = '\0';
}

void ui_idle_deinit(void) {
  if (s_window) { window_destroy(s_window); s_window = NULL; }
}

void ui_idle_show(void) {
  refresh_text();
  if (!window_stack_contains_window(s_window)) window_stack_push(s_window, true);
}
