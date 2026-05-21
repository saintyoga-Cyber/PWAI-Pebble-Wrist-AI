/*
 * PWAI — Pebble Wrist AI
 * Watch app C layer
 *
 * Buttons:
 *   UP     — select Perplexity
 *   DOWN   — select Claude
 *   SELECT — send starter prompt
 *
 * AppMessage key schema (mirrors appinfo.json appKeys):
 *   0  KEY_MSG_TYPE    uint8   0=chunk 2=status 3=ping
 *   1  KEY_CHUNK_IDX   uint8   0-based index
 *   2  KEY_CHUNK_TOTAL uint8   total chunks
 *   3  KEY_CHUNK_DATA  cstring up to 180 chars
 *   4  KEY_JOB_ID      cstring job UUID
 *   5  KEY_STATUS      cstring human-readable status
 *   6  KEY_PROVIDER    uint8   0=Perplexity 1=Claude
 *   7  KEY_COMMAND     uint8   1=send prompt
 *   8  KEY_PROMPT      cstring prompt text
 *   9  KEY_TOKEN       cstring (unused watch-side)
 */

#include <pebble.h>

#define KEY_MSG_TYPE    0
#define KEY_CHUNK_IDX   1
#define KEY_CHUNK_TOTAL 2
#define KEY_CHUNK_DATA  3
#define KEY_JOB_ID      4
#define KEY_STATUS      5
#define KEY_PROVIDER    6
#define KEY_COMMAND     7
#define KEY_PROMPT      8
#define KEY_TOKEN       9

#define CMD_SEND_PROMPT     1
#define PROVIDER_PERPLEXITY 0
#define PROVIDER_CLAUDE     1
#define REPLY_BUF_SIZE      4096

static Window      *s_main_window;
static TextLayer   *s_title_layer;
static TextLayer   *s_provider_layer;
static ScrollLayer *s_scroll_layer;
static TextLayer   *s_output_layer;

static char s_reply_buffer[REPLY_BUF_SIZE];
static int  s_expected_chunks = 0;
static int  s_received_chunks = 0;
static int  s_provider = PROVIDER_PERPLEXITY;

static void refresh_scroll(void) {
  Layer *root   = window_get_root_layer(s_main_window);
  GRect  bounds = layer_get_bounds(root);
  GSize  size   = text_layer_get_content_size(s_output_layer);
  int    h      = size.h + 16;
  text_layer_set_size(s_output_layer, GSize(bounds.size.w - 8, h));
  scroll_layer_set_content_size(s_scroll_layer, GSize(bounds.size.w, h));
}

static void update_output(const char *text) {
  text_layer_set_text(s_output_layer, text);
  refresh_scroll();
}

static void update_provider_label(void) {
  text_layer_set_text(s_provider_layer,
    s_provider == PROVIDER_PERPLEXITY ? "PPX" : "Claude");
}

static void reset_reply(void) {
  memset(s_reply_buffer, 0, sizeof(s_reply_buffer));
  s_expected_chunks = 0;
  s_received_chunks = 0;
}

static void send_prompt(const char *prompt) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK) {
    update_output("Outbox error. Retry.");
    return;
  }
  dict_write_uint8(iter,   KEY_COMMAND,  CMD_SEND_PROMPT);
  dict_write_uint8(iter,   KEY_PROVIDER, (uint8_t)s_provider);
  dict_write_cstring(iter, KEY_PROMPT,   prompt);
  dict_write_end(iter);
  app_message_outbox_send();
  update_output("Sent to phone...");
}

static void inbox_received_handler(DictionaryIterator *iter, void *context) {
  Tuple *type_t = dict_find(iter, KEY_MSG_TYPE);
  if (!type_t) return;
  uint8_t type = type_t->value->uint8;

  if (type == 0) {
    Tuple *idx_t   = dict_find(iter, KEY_CHUNK_IDX);
    Tuple *total_t = dict_find(iter, KEY_CHUNK_TOTAL);
    Tuple *data_t  = dict_find(iter, KEY_CHUNK_DATA);
    if (!idx_t || !total_t || !data_t) return;
    if (idx_t->value->uint8 == 0) reset_reply();
    s_expected_chunks = total_t->value->uint8;
    size_t cur   = strlen(s_reply_buffer);
    size_t added = strlen(data_t->value->cstring);
    if (cur + added < REPLY_BUF_SIZE - 1)
      strncat(s_reply_buffer, data_t->value->cstring, REPLY_BUF_SIZE - cur - 1);
    s_received_chunks++;
    update_output(s_reply_buffer);
    if (s_received_chunks >= s_expected_chunks) vibes_short_pulse();
    return;
  }

  if (type == 2) {
    Tuple *st = dict_find(iter, KEY_STATUS);
    if (st) update_output(st->value->cstring);
    return;
  }

  if (type == 3) {
    vibes_double_pulse();
    update_output("Reply ready! Press Select.");
    return;
  }
}

static void outbox_failed_handler(DictionaryIterator *iter,
                                   AppMessageResult reason, void *ctx) {
  update_output("Send failed. Check BT.");
}

static void select_click_handler(ClickRecognizerRef r, void *ctx) {
  send_prompt("Continue our conversation. What is the next actionable step?");
}
static void up_click_handler(ClickRecognizerRef r, void *ctx) {
  s_provider = PROVIDER_PERPLEXITY;
  update_provider_label();
  update_output("Provider: Perplexity\nPress Select to send.");
}
static void down_click_handler(ClickRecognizerRef r, void *ctx) {
  s_provider = PROVIDER_CLAUDE;
  update_provider_label();
  update_output("Provider: Claude\nPress Select to send.");
}
static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
  window_single_click_subscribe(BUTTON_ID_UP,     up_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN,   down_click_handler);
}

static void main_window_load(Window *window) {
  Layer *root   = window_get_root_layer(window);
  GRect  bounds = layer_get_bounds(root);
  int    w      = bounds.size.w;

  s_title_layer = text_layer_create(GRect(0, 0, w - 40, 22));
  text_layer_set_text(s_title_layer, "PWAI");
  text_layer_set_font(s_title_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_background_color(s_title_layer, GColorBlack);
  text_layer_set_text_color(s_title_layer, GColorWhite);
  text_layer_set_text_alignment(s_title_layer, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_title_layer));

  s_provider_layer = text_layer_create(GRect(w - 40, 0, 40, 22));
  text_layer_set_text(s_provider_layer, "PPX");
  text_layer_set_font(s_provider_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_background_color(s_provider_layer, GColorBlack);
  text_layer_set_text_color(s_provider_layer, GColorCyan);
  text_layer_set_text_alignment(s_provider_layer, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_provider_layer));

  s_scroll_layer = scroll_layer_create(GRect(0, 22, w, bounds.size.h - 22));
  scroll_layer_set_click_config_onto_window(s_scroll_layer, window);
  layer_add_child(root, scroll_layer_get_layer(s_scroll_layer));

  s_output_layer = text_layer_create(GRect(4, 2, w - 8, 2000));
  text_layer_set_font(s_output_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_overflow_mode(s_output_layer, GTextOverflowModeWordWrap);
  text_layer_set_text_alignment(s_output_layer, GTextAlignmentLeft);
  scroll_layer_add_child(s_scroll_layer, text_layer_get_layer(s_output_layer));

  update_output("UP=PPX  DOWN=Claude\nSELECT=send prompt");
}

static void main_window_unload(Window *window) {
  text_layer_destroy(s_title_layer);
  text_layer_destroy(s_provider_layer);
  text_layer_destroy(s_output_layer);
  scroll_layer_destroy(s_scroll_layer);
}

static void init(void) {
  s_main_window = window_create();
  window_set_background_color(s_main_window, GColorWhite);
  window_set_click_config_provider(s_main_window, click_config_provider);
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load   = main_window_load,
    .unload = main_window_unload,
  });
  app_message_register_inbox_received(inbox_received_handler);
  app_message_register_outbox_failed(outbox_failed_handler);
  app_message_open(1024, 512);
  window_stack_push(s_main_window, true);
}

static void deinit(void) {
  window_destroy(s_main_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
