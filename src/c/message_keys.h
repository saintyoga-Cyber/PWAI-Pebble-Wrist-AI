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
