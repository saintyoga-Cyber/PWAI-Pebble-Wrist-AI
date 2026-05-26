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
 *
 * Critical-3: background worker
 *   - Worker binary (src/worker/worker.c) runs when foreground is closed.
 *   - Foreground sends WORKER_MSG_JOB_STARTED when STATE_WAITING entered.
 *   - Worker polls persist storage; fires WORKER_MSG_REPLY_READY when done.
 *   - Foreground receives wake, buzzes unconditionally (it IS a background
 *     notification), clears the persist flag, shows response window.
 *   - Foreground sends WORKER_MSG_JOB_CLEAR on error or cancel.
 */

#include <pebble.h>
#include "state.h"
#include "dictation.h"
#include "transport.h"
#include "ui_idle.h"
#include "ui_spinner.h"
#include "ui_response.h"
#include "message_keys.h"

// Minimum elapsed query time (ms) before a haptic buzz fires on reply
// when the foreground app is in the foreground (Critical-2).
// Background wakes (Critical-3) always buzz regardless of this gate.
#define HAPTIC_GATE_MS 5000u

// ---------------------------------------------------------------------------
// Worker helpers
// ---------------------------------------------------------------------------

static void worker_send(uint16_t type) {
  AppWorkerMessage msg = { .data0 = 0, .data1 = 0, .data2 = 0 };
  // Launch worker if not running, then send.
  AppWorkerResult res = app_worker_launch();
  // APP_WORKER_RESULT_RUNNING means it was already up; both are fine.
  if (res == APP_WORKER_RESULT_SUCCESS || res == APP_WORKER_RESULT_RUNNING) {
    app_worker_send_message(type, &msg);
  }
}

// Called by the worker when a background reply is ready.
// The foreground app may or may not be visible when this fires.
static void on_worker_message(uint16_t type, AppWorkerMessage *msg) {
  if (type != WORKER_MSG_REPLY_READY) return;

  // Clear the persist flag so the worker doesn't fire again.
  persist_delete(PERSIST_KEY_PENDING_JOB);

  // Background wake: always buzz — this IS the notification.
  vibes_short_pulse();

  // If state is already SHOWING (foreground was open and got the reply
  // through the normal transport path), do nothing further.
  if (state_current() == STATE_SHOWING) return;

  // Foreground was closed or in a different state: show the response window.
  // The latest turn was already committed by transport when JS wrote to persist,
  // so ui_response_show() will render the correct content.
  state_set(STATE_SHOWING);
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
  // STATE_WAITING stamps s_query_start_ms inside state_set() (Critical-2).
  state_set(STATE_WAITING);
  // Critical-3: tell worker a job is in flight so it starts polling.
  worker_send(WORKER_MSG_JOB_STARTED);
}

static void on_dictation_fail(int status) {
  APP_LOG(APP_LOG_LEVEL_WARNING, "dictation_fail: %d", status);
  if (status == DictationSessionStatusFailureSystemAborted) {
    state_set(STATE_IDLE);
    worker_send(WORKER_MSG_JOB_CLEAR);
    return;
  }
  state_set_dictation_status(status);
  state_set_error(dictation_status_to_error(status));
  worker_send(WORKER_MSG_JOB_CLEAR);
}

// ---------------------------------------------------------------------------
// Transport callbacks
// ---------------------------------------------------------------------------

static void on_response(char *owned_response) {
  // Critical-3: clear job from worker since reply arrived via foreground path.
  worker_send(WORKER_MSG_JOB_CLEAR);
  persist_delete(PERSIST_KEY_PENDING_JOB);

  // Critical-2: buzz only if the reply took longer than the gate threshold.
  if (state_query_elapsed_ms() >= HAPTIC_GATE_MS) {
    vibes_short_pulse();
  }
  state_commit_turn(owned_response);
  state_set(STATE_SHOWING);
}

static void on_transport_error(OwuiErrorCode code) {
  worker_send(WORKER_MSG_JOB_CLEAR);
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
  // Critical-3: subscribe to worker messages before anything else.
  app_worker_message_subscribe(on_worker_message);
  transport_send_reset();
  transport_send_provider(state_provider());
}

static void deinit(void) {
  app_worker_message_unsubscribe();
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
