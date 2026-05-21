/*
 * PWAI v0.2 — Pebble Wrist AI
 *
 * C layer: thin coordinator. All state lives in state.c.
 * UI layers: ui_idle, ui_spinner, ui_response.
 * Voice input: dictation.c
 * AppMessage transport: transport.c (bitmask chunking + ChunkAck handshake)
 *
 * PWAI additions over open-pebble-ai:
 *   - UP/DOWN on idle screen selects Perplexity vs Claude
 *   - transport_send_provider() keeps JS side in sync
 */

#include <pebble.h>
#include "state.h"
#include "dictation.h"
#include "transport.h"
#include "ui_idle.h"
#include "ui_spinner.h"
#include "ui_response.h"
#include "message_keys.h"

static OwuiErrorCode dictation_status_to_error(int status) {
  switch (status) {
    case DictationSessionStatusFailureConnectivityError:              return ERR_PHONE_DISCONNECTED;
    case DictationSessionStatusFailureRecognizerError:                return ERR_RECOGNITION_FAILED;
    case DictationSessionStatusFailureTranscriptionRejected:
    case DictationSessionStatusFailureTranscriptionRejectedWithError: return ERR_NO_SPEECH;
    default:                                                          return ERR_RECOGNITION_FAILED;
  }
}

static void on_dictation_done(const char *utterance) {
  state_set_pending_user_text(utterance);
  state_set(STATE_SENDING);
  transport_send_utterance(utterance);
  state_set(STATE_WAITING);
}

static void on_dictation_fail(int status) {
  APP_LOG(APP_LOG_LEVEL_WARNING, "dictation_fail: %d", status);
  if (status == DictationSessionStatusFailureSystemAborted) {
    state_set(STATE_IDLE); return;
  }
  state_set_dictation_status(status);
  state_set_error(dictation_status_to_error(status));
}

static void on_response(char *owned_response) {
  state_commit_turn(owned_response);
  state_set(STATE_SHOWING);
}

static void on_transport_error(OwuiErrorCode code) {
  state_set_error(code);
}

static void init(void) {
  ui_idle_init();
  ui_spinner_init();
  ui_response_init();
  dictation_init(on_dictation_done, on_dictation_fail);
  transport_init(on_response, on_transport_error);
  state_init();
  transport_send_reset();
  transport_send_provider(state_provider());
}

static void deinit(void) {
  state_deinit();
  transport_deinit();
  dictation_deinit();
  ui_response_deinit();
  ui_spinner_deinit();
  ui_idle_deinit();
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
