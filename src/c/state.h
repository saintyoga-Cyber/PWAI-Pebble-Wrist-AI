#pragma once
#include <pebble.h>
#include "message_keys.h"

typedef enum {
  STATE_IDLE,
  STATE_DICTATING,
  STATE_SENDING,
  STATE_WAITING,
  STATE_SHOWING,
  STATE_ERROR,
} AppState;

void      state_init(void);
void      state_deinit(void);
AppState  state_current(void);
void      state_set(AppState next);

OwuiErrorCode state_last_error(void);
void          state_set_error(OwuiErrorCode code);
int           state_dictation_status(void);
void          state_set_dictation_status(int status);

// Conversation history ring (FIFO, capped at MAX_TURNS)
#define MAX_TURNS 8
typedef struct {
  char *user;   // owned, malloc'd
  char *ai;     // owned, malloc'd
} Turn;

int         state_turn_count(void);
const Turn *state_turn_at(int idx);
void        state_set_pending_user_text(const char *text);
const char *state_pending_user_text(void);
void        state_commit_turn(char *owned_ai_text);
void        state_clear_turns(void);

// Font size (pushed from PKJS config)
typedef enum { FONT_MEDIUM = 0, FONT_LARGE = 1 } FontChoice;
FontChoice  state_font(void);
void        state_set_font(FontChoice f);
const char *state_font_key(void);

// PWAI: active provider (0=Perplexity, 1=Claude)
int  state_provider(void);
void state_set_provider(int p);

// Critical-2: query-start timestamp for haptic gate.
// state_mark_query_start() is called automatically by state_set(STATE_WAITING).
// state_query_elapsed_ms() returns ms since the query was sent, or 0 if not waiting.
void     state_mark_query_start(void);
uint32_t state_query_elapsed_ms(void);
