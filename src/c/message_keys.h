#pragma once

// Wire constants shared between watch C and PebbleKit JS.
// JS-side CHUNK_SIZE and MAX_CHUNKS in src/pkjs/chunker.js must stay in sync.

#define CHUNK_SIZE       2048
#define MAX_CHUNKS       16
#define MAX_UTTERANCE    1024
#define CHUNK_TIMEOUT_MS 15000

typedef enum {
  ERR_NONE = 0,
  ERR_PHONE_DISCONNECTED    = 1,
  ERR_NO_SPEECH             = 2,
  ERR_RECOGNITION_FAILED    = 3,
  ERR_SERVER_UNREACHABLE    = 4,
  ERR_BAD_API_KEY           = 5,
  ERR_ACCESS_DENIED         = 6,
  ERR_SERVER_ERROR          = 7,
  ERR_TIMEOUT               = 8,
  ERR_RESPONSE_TOO_LARGE    = 9,
  ERR_OUT_OF_MEMORY         = 10,
  ERR_TRANSPORT_FAILED      = 11,
  ERR_BUSY                  = 12,
} OwuiErrorCode;

// ---------------------------------------------------------------------------
// Background Worker <-> Foreground App message types (AppWorkerMessage.type)
// ---------------------------------------------------------------------------

// Worker sends this to the foreground app when it detects a pending AI reply
// stored in persistent storage. The foreground app responds by launching,
// buzzing the wrist, and showing the response window.
#define WORKER_MSG_REPLY_READY   1u

// Foreground app sends this to the worker to tell it a job is in flight.
// The worker will start polling until the reply lands or the job clears.
#define WORKER_MSG_JOB_STARTED   2u

// Foreground app sends this to the worker to clear any pending job
// (e.g. user cancelled, error received). Worker stops polling.
#define WORKER_MSG_JOB_CLEAR     3u

// ---------------------------------------------------------------------------
// Persistent storage keys (persist_read / persist_write)
// ---------------------------------------------------------------------------

// Set to 1 by PebbleKit JS (via AppMessage -> foreground -> persist_write)
// when the server reply has been fully reassembled in the JS layer but the
// foreground app is not running. The worker polls this key every
// WORKER_POLL_INTERVAL_MS and fires WORKER_MSG_REPLY_READY when it sees 1.
// Cleared by the foreground app after it reads and displays the reply.
#define PERSIST_KEY_PENDING_JOB  100u
