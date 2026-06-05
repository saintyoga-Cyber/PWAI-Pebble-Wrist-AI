/*
 * PWAI v0.3 — Pebble Wrist AI
 *
 * C layer: thin coordinator. All state lives in state.c.
 * UI layers: ui_idle, ui_spinner, ui_response.
 * Voice input: dictation.c
 * AppMessage transport: transport.c (bitmask chunking + ChunkAck handshake)
 *
 * PWAI additions over open-pebble-ai:
 *   - UP/DOWN on idle screen selects Perplexity vs Claude
 *   - transport_send_provider() keeps JS side in sync
 *
 * Critical-2: haptic gate
 *   - vibes_short_pulse() fires in on_response() ONLY when the AI reply
 *     took >= HAPTIC_GATE_MS (5 seconds) to arrive.
 *   - Fast, immediate replies are silent — no buzz spam.
 *   - Slow or background replies (network hiccup, Claude reasoning, etc.)
 *     get a single buzz to alert the user without them watching the screen.
 *
 * FIX-2: worker lifecycle
 *   - app_worker_launch() + WORKER_MSG_JOB_STARTED on query dispatch.
 *   - persist_delete(PERSIST_KEY_PENDING_JOB) + WORKER_MSG_JOB_CLEAR
 *     on reply received or error, so the worker stops polling.
 */

#include <pebble.h>
#include <pebble_worker.h>
#include "state.h"
#include "dictation.h"
#include "transport.h"
#include "ui_idle.h"
#include "ui_spinner.h"
#include "ui_response.h"
#include "message_keys.h"

// Minimum elapsed query time (ms) before a haptic buzz fires on reply.
// 5 000 ms = 5 seconds. Tune freely: 3000 for more buzz, 8000 for less.
#define HAPTIC_GATE_MS 5000u

// ---------------------------------------------------------------------------
// Worker helpers
// ---------------------------------------------------------------------------

static void worker_signal(uint16_t type) {
  AppWorkerMessage msg = { .data0 = 0, .data1 = 0, .data2 = 0 };
  app_worker_send_message(type, &msg);
}

static void worker_clear_job(void) {
  persist_delete(PERSIST_KEY_PENDING_JOB);
  worker_signal(WORKER_MSG_JOB_CLEAR);
}

// ---------------------------------------------------------------------------
// Dictation callbacks
// ---------------------------------------------------------------------------

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
  // STATE_WAITING stamps s_query_start_ms inside state_set().
  state_set(STATE_WAITING);
  // FIX-2: launch the background worker so it can detect the reply even
  // if the user closes the foreground app while waiting.
  AppWorkerResult wr = app_worker_launch();
  if (wr == APP_WORKER_RESULT_SUCCESS ||
      wr == APP_WORKER_RESULT_ALREADY_RUNNING) {
    worker_signal(WORKER_MSG_JOB_STARTED);
  }
}

static void on_dictation_fail(int status) {
  APP_LOG(APP_LOG_LEVEL_WARNING, "dictation_fail: %d", status);
  if (status == DictationSessionStatusFailureSystemAborted) {
    state_set(STATE_IDLE); return;
  }
  state_set_dictation_status(status);
  state_set_error(dictation_status_to_error(status));
}

// ---------------------------------------------------------------------------
// Transport callbacks
// ---------------------------------------------------------------------------

static void on_response(char *owned_response) {
  // FIX-2: clear the pending-job persist flag (set by transport.c when all
  // chunks arrived) and tell the worker to stop polling.
  worker_clear_job();
  // Critical-2: buzz only if the reply took longer than the gate threshold.
  if (state_query_elapsed_ms() >= HAPTIC_GATE_MS) {
    vibes_short_pulse();
  }
  state_commit_turn(owned_response);
  state_set(STATE_SHOWING);
}

static void on_transport_error(OwuiErrorCode code) {
  // FIX-2: also clear the worker on error so it does not poll forever.
  worker_clear_job();
  state_set_error(code);
}

// ---------------------------------------------------------------------------
// App lifecycle
// ---------------------------------------------------------------------------

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
