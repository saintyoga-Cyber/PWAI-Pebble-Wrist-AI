#include "state.h"
#include "ui_idle.h"
#include "ui_spinner.h"
#include "ui_response.h"
#include <string.h>
#include <stdlib.h>

static AppState      s_state            = STATE_IDLE;
static OwuiErrorCode s_last_error       = ERR_NONE;
static FontChoice    s_font             = FONT_MEDIUM;
static int           s_dictation_status = 0;
static int           s_provider         = 0; // 0=Perplexity, 1=Claude

static Turn s_turns[MAX_TURNS];
static int  s_turn_count   = 0;
static char *s_pending_user = NULL;

static char *dup_str(const char *src) {
  if (!src) return NULL;
  size_t n = strlen(src);
  char *out = malloc(n + 1);
  if (!out) return NULL;
  memcpy(out, src, n + 1);
  return out;
}

static void free_turn(Turn *t) {
  if (t->user) { free(t->user); t->user = NULL; }
  if (t->ai)   { free(t->ai);   t->ai   = NULL; }
}

static void update_ui_for_state(AppState s) {
  switch (s) {
    case STATE_IDLE:
    case STATE_ERROR:
      ui_response_hide(); ui_spinner_hide(); ui_idle_show(); break;
    case STATE_DICTATING:
      break;
    case STATE_SENDING:
      ui_response_hide(); ui_spinner_show("Sending..."); break;
    case STATE_WAITING:
      ui_response_hide(); ui_spinner_show("Thinking..."); break;
    case STATE_SHOWING:
      ui_spinner_hide(); ui_response_show(); break;
  }
}

void state_init(void) {
  s_state = STATE_IDLE; s_last_error = ERR_NONE;
  s_turn_count = 0; memset(s_turns, 0, sizeof(s_turns));
  s_pending_user = NULL;
  update_ui_for_state(s_state);
}

void state_deinit(void) {
  for (int i = 0; i < MAX_TURNS; i++) free_turn(&s_turns[i]);
  s_turn_count = 0;
  if (s_pending_user) { free(s_pending_user); s_pending_user = NULL; }
}

AppState state_current(void) { return s_state; }

static const char *state_name(AppState s) {
  switch (s) {
    case STATE_IDLE:      return "IDLE";
    case STATE_DICTATING: return "DICTATING";
    case STATE_SENDING:   return "SENDING";
    case STATE_WAITING:   return "WAITING";
    case STATE_SHOWING:   return "SHOWING";
    case STATE_ERROR:     return "ERROR";
    default:              return "?";
  }
}

void state_set(AppState next) {
  APP_LOG(APP_LOG_LEVEL_INFO, "STATE: %s->%s", state_name(s_state), state_name(next));
  s_state = next;
  update_ui_for_state(next);
}

OwuiErrorCode state_last_error(void)   { return s_last_error; }
void state_set_error(OwuiErrorCode code) { s_last_error = code; state_set(STATE_ERROR); }
int  state_dictation_status(void)      { return s_dictation_status; }
void state_set_dictation_status(int s) { s_dictation_status = s; }
FontChoice  state_font(void)           { return s_font; }
void        state_set_font(FontChoice f) { s_font = f; }
const char *state_font_key(void)       { return s_font == FONT_LARGE ? FONT_KEY_GOTHIC_28 : FONT_KEY_GOTHIC_24; }
int  state_provider(void)              { return s_provider; }
void state_set_provider(int p)         { s_provider = p; }

int state_turn_count(void) { return s_turn_count; }
const Turn *state_turn_at(int idx) {
  if (idx < 0 || idx >= s_turn_count) return NULL;
  return &s_turns[idx];
}
void state_set_pending_user_text(const char *text) {
  if (s_pending_user) { free(s_pending_user); s_pending_user = NULL; }
  s_pending_user = dup_str(text);
}
const char *state_pending_user_text(void) { return s_pending_user ? s_pending_user : ""; }
void state_commit_turn(char *owned_ai_text) {
  if (s_turn_count == MAX_TURNS) {
    free_turn(&s_turns[0]);
    memmove(&s_turns[0], &s_turns[1], sizeof(Turn) * (MAX_TURNS - 1));
    s_turn_count--;
  }
  Turn *t = &s_turns[s_turn_count++];
  t->user = s_pending_user; s_pending_user = NULL;
  t->ai   = owned_ai_text;
}
void state_clear_turns(void) {
  for (int i = 0; i < s_turn_count; i++) free_turn(&s_turns[i]);
  s_turn_count = 0;
  if (s_pending_user) { free(s_pending_user); s_pending_user = NULL; }
}
