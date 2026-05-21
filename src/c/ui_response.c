#include "ui_response.h"
#include "state.h"
#include "dictation.h"
#include "transport.h"
#include <pebble.h>
#include <string.h>

static Window      *s_window       = NULL;
static ScrollLayer *s_scroll_layer = NULL;

static int16_t s_touch_start_y;
static int16_t s_touch_start_offset_y;
static bool    s_touch_dragging;
#define TOUCH_TAP_THRESHOLD_PX 8

typedef struct { Layer *border; TextLayer *text; GColor border_color; } Bubble;
static Bubble s_user_bubbles[MAX_TURNS];
static Bubble s_ai_bubbles[MAX_TURNS];

#define BUBBLE_RADIUS     6
#define BUBBLE_BORDER     2
#define BUBBLE_PAD_X      8
#define BUBBLE_PAD_TOP    0
#define BUBBLE_PAD_BOTTOM 2
#define BUBBLE_TEXT_Y_TRIM 6
#define BUBBLE_DESCENDER  6
#define BUBBLE_GAP_Y      6
#define BUBBLE_MARGIN_X   4
#define BUBBLE_TOP_MARGIN 4
#define BUBBLE_BOTTOM_PAD 6
#define MAX_TEXT_HEIGHT   2000

static void draw_bubble(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  GColor color = GColorBlack;
  for (int i = 0; i < MAX_TURNS; i++) {
    if (s_user_bubbles[i].border == layer) { color = s_user_bubbles[i].border_color; break; }
    if (s_ai_bubbles[i].border   == layer) { color = s_ai_bubbles[i].border_color;   break; }
  }
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, BUBBLE_BORDER);
  graphics_draw_round_rect(ctx, GRect(0, 0, b.size.w, b.size.h), BUBBLE_RADIUS);
}

static void init_bubble(Bubble *bubble, GColor border_color, GFont font) {
  bubble->border_color = border_color;
  bubble->border = layer_create(GRect(0, 0, 0, 0));
  layer_set_update_proc(bubble->border, draw_bubble);
  bubble->text = text_layer_create(GRect(0, 0, 1, 1));
  text_layer_set_text(bubble->text, "");
  text_layer_set_font(bubble->text, font);
  text_layer_set_background_color(bubble->text, GColorClear);
  text_layer_set_text_color(bubble->text, GColorBlack);
  text_layer_set_overflow_mode(bubble->text, GTextOverflowModeWordWrap);
  layer_add_child(bubble->border, text_layer_get_layer(bubble->text));
}

static void destroy_bubble(Bubble *bubble) {
  if (bubble->text)   text_layer_destroy(bubble->text);
  if (bubble->border) layer_destroy(bubble->border);
  bubble->text = bubble->border = NULL;
}

static void hide_bubble(Bubble *bubble) {
  layer_set_frame(bubble->border, GRect(0, 0, 0, 0));
}

static int16_t layout_bubble(Bubble *bubble, const char *text,
                              int16_t y, int16_t scroll_w, bool align_right) {
  text_layer_set_text(bubble->text, text ? text : "");
  int16_t max_width   = scroll_w - 2 * BUBBLE_MARGIN_X;
  int16_t inner_w_max = max_width - 2 * (BUBBLE_PAD_X + BUBBLE_BORDER);
  text_layer_set_size(bubble->text, GSize(inner_w_max, MAX_TEXT_HEIGHT));
  GSize   used    = text_layer_get_content_size(bubble->text);
  int16_t inner_w = used.w;
  int16_t inner_h = used.h + BUBBLE_DESCENDER;
  int16_t border_w = inner_w + 2 * (BUBBLE_PAD_X + BUBBLE_BORDER);
  if (border_w > max_width) border_w = max_width;
  int16_t border_h = inner_h - BUBBLE_TEXT_Y_TRIM
                     + BUBBLE_PAD_TOP + BUBBLE_PAD_BOTTOM + 2 * BUBBLE_BORDER;
  int16_t x = align_right ? (scroll_w - BUBBLE_MARGIN_X - border_w) : BUBBLE_MARGIN_X;
  layer_set_frame(bubble->border, GRect(x, y, border_w, border_h));
  text_layer_set_size(bubble->text,
    GSize(border_w - 2 * (BUBBLE_PAD_X + BUBBLE_BORDER), inner_h));
  text_layer_set_text_alignment(bubble->text,
    align_right ? GTextAlignmentRight : GTextAlignmentLeft);
  layer_set_frame(text_layer_get_layer(bubble->text),
    GRect(BUBBLE_PAD_X + BUBBLE_BORDER,
          BUBBLE_PAD_TOP + BUBBLE_BORDER - BUBBLE_TEXT_Y_TRIM,
          border_w - 2 * (BUBBLE_PAD_X + BUBBLE_BORDER), inner_h));
  return border_h;
}

static void apply_text(void) {
  if (!s_scroll_layer) return;
  GFont   font      = fonts_get_system_font(state_font_key());
  GRect   sb        = layer_get_bounds(scroll_layer_get_layer(s_scroll_layer));
  int16_t scroll_w  = sb.size.w;
  int16_t scroll_h  = sb.size.h;
  int16_t y = BUBBLE_TOP_MARGIN;
  int n = state_turn_count();
  for (int i = 0; i < MAX_TURNS; i++) {
    Bubble *u = &s_user_bubbles[i];
    Bubble *a = &s_ai_bubbles[i];
    text_layer_set_font(u->text, font);
    text_layer_set_font(a->text, font);
    if (i < n) {
      const Turn *t = state_turn_at(i);
      if (t && t->user && t->user[0]) { y += layout_bubble(u, t->user, y, scroll_w, true);  y += BUBBLE_GAP_Y; }
      else hide_bubble(u);
      if (t && t->ai)                  { y += layout_bubble(a, t->ai,   y, scroll_w, false); y += BUBBLE_GAP_Y; }
      else hide_bubble(a);
    } else { hide_bubble(u); hide_bubble(a); }
  }
  int16_t content_h = (n > 0) ? (y - BUBBLE_GAP_Y + BUBBLE_BOTTOM_PAD) : scroll_h;
  scroll_layer_set_content_size(s_scroll_layer, GSize(scroll_w, content_h));
  int16_t bottom_offset = (content_h > scroll_h) ? -(content_h - scroll_h) : 0;
  scroll_layer_set_content_offset(s_scroll_layer, GPoint(0, bottom_offset), true);
}

static void on_touch_event(const TouchEvent *event, void *ctx) {
  if (!s_scroll_layer) return;
  switch (event->type) {
    case TouchEvent_Touchdown: {
      GPoint cur = scroll_layer_get_content_offset(s_scroll_layer);
      s_touch_start_y = event->y; s_touch_start_offset_y = cur.y; s_touch_dragging = true; break;
    }
    case TouchEvent_PositionUpdate: break;
    case TouchEvent_Liftoff: {
      int16_t dy = s_touch_dragging ? event->y - s_touch_start_y : 0;
      if (!s_touch_dragging) break;
      s_touch_dragging = false;
      if (dy > -TOUCH_TAP_THRESHOLD_PX && dy < TOUCH_TAP_THRESHOLD_PX) break;
      scroll_layer_set_content_offset(s_scroll_layer,
        GPoint(0, s_touch_start_offset_y + dy), true);
      break;
    }
  }
}

static void on_select(ClickRecognizerRef rec, void *ctx) { state_set(STATE_DICTATING); dictation_start(); }
static void on_back(ClickRecognizerRef rec, void *ctx)   { state_set(STATE_IDLE); }

static void click_config_provider(void *ctx) {
  window_set_click_context(BUTTON_ID_UP,   s_scroll_layer);
  window_set_click_context(BUTTON_ID_DOWN, s_scroll_layer);
  window_single_click_subscribe(BUTTON_ID_UP,   (ClickHandler)scroll_layer_scroll_up_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, (ClickHandler)scroll_layer_scroll_down_click_handler);
  window_single_repeating_click_subscribe(BUTTON_ID_UP,   80, (ClickHandler)scroll_layer_scroll_up_click_handler);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 80, (ClickHandler)scroll_layer_scroll_down_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, on_select);
  window_single_click_subscribe(BUTTON_ID_BACK,   on_back);
}

static void window_load(Window *window) {
  Layer *root   = window_get_root_layer(window);
  GRect  bounds = layer_get_bounds(root);
  s_scroll_layer = scroll_layer_create(bounds);
  scroll_layer_set_shadow_hidden(s_scroll_layer, true);
  GFont  font       = fonts_get_system_font(state_font_key());
  GColor user_color = PBL_IF_COLOR_ELSE(GColorVividCerulean, GColorBlack);
  GColor ai_color   = PBL_IF_COLOR_ELSE(GColorOrange,        GColorBlack);
  for (int i = 0; i < MAX_TURNS; i++) {
    init_bubble(&s_user_bubbles[i], user_color, font);
    init_bubble(&s_ai_bubbles[i],   ai_color,   font);
    scroll_layer_add_child(s_scroll_layer, s_user_bubbles[i].border);
    scroll_layer_add_child(s_scroll_layer, s_ai_bubbles[i].border);
  }
  layer_add_child(root, scroll_layer_get_layer(s_scroll_layer));
  window_set_click_config_provider(s_window, click_config_provider);
  touch_service_subscribe(on_touch_event, NULL);
  apply_text();
}

static void window_unload(Window *window) {
  touch_service_unsubscribe();
  s_touch_dragging = false;
  for (int i = 0; i < MAX_TURNS; i++) { destroy_bubble(&s_user_bubbles[i]); destroy_bubble(&s_ai_bubbles[i]); }
  if (s_scroll_layer) { scroll_layer_destroy(s_scroll_layer); s_scroll_layer = NULL; }
}

void ui_response_init(void) {
  s_window = window_create();
  window_set_background_color(s_window, GColorWhite);
  window_set_window_handlers(s_window, (WindowHandlers){ .load = window_load, .unload = window_unload });
}
void ui_response_deinit(void) { if (s_window) { window_destroy(s_window); s_window = NULL; } }
void ui_response_show(void) {
  if (!window_stack_contains_window(s_window)) window_stack_push(s_window, true);
  else apply_text();
}
void ui_response_hide(void) {
  if (window_stack_contains_window(s_window)) window_stack_remove(s_window, false);
}
