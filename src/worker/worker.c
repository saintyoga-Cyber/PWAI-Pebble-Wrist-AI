/*
 * PWAI Background Worker
 *
 * Runs as a separate binary while the foreground app is closed.
 * Polls persistent storage every WORKER_POLL_INTERVAL_MS for a pending
 * AI reply flag written by the PebbleKit JS layer. When detected, fires
 * an AppWorkerMessage to wake the foreground app.
 *
 * Memory budget: ~10 KB heap. This file uses zero heap allocation.
 *
 * Lifecycle:
 *   - Worker is launched by the foreground app via app_worker_launch()
 *     when STATE_WAITING is entered (message is in flight to server).
 *   - Worker runs until the foreground app explicitly calls
 *     app_worker_kill() after the reply is displayed, or until PebbleOS
 *     terminates it.
 *   - If the watch reboots mid-wait, the worker is NOT auto-relaunched;
 *     the pending job flag in persist storage will be picked up on next
 *     foreground launch via transport.c normal flow.
 */

#include <pebble_worker.h>

// Poll interval: check persist storage every 30 seconds.
// Lower = more responsive but more battery drain.
// 30 s is a reasonable balance for an AI reply wait.
#define WORKER_POLL_INTERVAL_MS  30000

// Persist key must match PERSIST_KEY_PENDING_JOB in message_keys.h.
// Cannot #include main app headers from worker binary, so mirror the value.
#define PERSIST_KEY_PENDING_JOB  100u
#define WORKER_MSG_REPLY_READY   1u

static AppTimer *s_poll_timer = NULL;
static bool      s_job_active = false;

static void send_to_foreground(uint16_t type) {
  AppWorkerMessage msg = { .data0 = 0, .data1 = 0, .data2 = 0 };
  app_worker_send_message(type, &msg);
}

static void poll_callback(void *ctx) {
  s_poll_timer = NULL;

  if (!s_job_active) {
    // No active job: nothing to poll, worker will go quiet.
    // It will be relaunched by the foreground app on the next query.
    return;
  }

  // Check if PebbleKit JS has written the "reply ready" flag.
  int flag = persist_read_int(PERSIST_KEY_PENDING_JOB);
  if (flag == 1) {
    // Reply is waiting. Notify the foreground app to wake and display it.
    // Do NOT clear the flag here; the foreground app owns the clear.
    send_to_foreground(WORKER_MSG_REPLY_READY);
    s_job_active = false;
    // Do not reschedule: our job is done until the next query.
    return;
  }

  // Reply not yet ready. Reschedule.
  s_poll_timer = app_timer_register(WORKER_POLL_INTERVAL_MS, poll_callback, NULL);
}

static void worker_message_handler(uint16_t type, AppWorkerMessage *msg) {
  switch (type) {
    case 2u: // WORKER_MSG_JOB_STARTED
      s_job_active = true;
      // Cancel any existing timer and start a fresh poll cycle.
      if (s_poll_timer) { app_timer_cancel(s_poll_timer); s_poll_timer = NULL; }
      s_poll_timer = app_timer_register(WORKER_POLL_INTERVAL_MS, poll_callback, NULL);
      break;

    case 3u: // WORKER_MSG_JOB_CLEAR
      s_job_active = false;
      if (s_poll_timer) { app_timer_cancel(s_poll_timer); s_poll_timer = NULL; }
      break;

    default:
      break;
  }
}

static void worker_init(void) {
  app_worker_message_subscribe(worker_message_handler);
  // If a job was already active before a worker restart, resume polling.
  // The foreground app will re-send WORKER_MSG_JOB_STARTED on relaunch,
  // but if it never relaunched (e.g. after a crash), check persist now.
  int flag = persist_read_int(PERSIST_KEY_PENDING_JOB);
  if (flag == 1) {
    // Reply already arrived before worker launched. Fire immediately.
    send_to_foreground(WORKER_MSG_REPLY_READY);
  }
}

static void worker_deinit(void) {
  if (s_poll_timer) { app_timer_cancel(s_poll_timer); s_poll_timer = NULL; }
  app_worker_message_unsubscribe();
}

int main(void) {
  worker_init();
  worker_event_loop();
  worker_deinit();
}
