#include "transport.h"
#include "state.h"
#include <string.h>

static TransportResponseHandler s_on_response = NULL;
static TransportErrorHandler    s_on_error    = NULL;

static char     *s_buffer        = NULL;
static int       s_total_chunks  = 0;
static uint32_t  s_received_mask = 0;
static AppTimer *s_chunk_timer   = NULL;

static void free_buffer(void) {
  if (s_buffer) { free(s_buffer); s_buffer = NULL; }
  s_total_chunks = 0;
  s_received_mask = 0;
  if (s_chunk_timer) { app_timer_cancel(s_chunk_timer); s_chunk_timer = NULL; }
}

static void chunk_timeout(void *ctx) {
  s_chunk_timer = NULL;
  free_buffer();
  if (s_on_error) s_on_error(ERR_TRANSPORT_FAILED);
}

static void reset_timeout(void) {
  if (s_chunk_timer) app_timer_cancel(s_chunk_timer);
  s_chunk_timer = app_timer_register(CHUNK_TIMEOUT_MS, chunk_timeout, NULL);
}

static void send_chunk_ack(int idx) {
  DictionaryIterator *out;
  if (app_message_outbox_begin(&out) != APP_MSG_OK) return;
  dict_write_int32(out, MESSAGE_KEY_ChunkAck, idx);
  app_message_outbox_send();
}

static void inbox_received(DictionaryIterator *iter, void *context) {
  Tuple *font_t = dict_find(iter, MESSAGE_KEY_FontSize);
  if (font_t) {
    int v = (int)font_t->value->int32;
    state_set_font(v == 1 ? FONT_LARGE : FONT_MEDIUM);
  }

  Tuple *err_t = dict_find(iter, MESSAGE_KEY_ErrorCode);
  if (err_t) {
    free_buffer();
    if (s_on_error) s_on_error((OwuiErrorCode)err_t->value->int32);
    return;
  }

  Tuple *idx_t = dict_find(iter, MESSAGE_KEY_ResponseChunkIndex);
  Tuple *tot_t = dict_find(iter, MESSAGE_KEY_ResponseChunkTotal);
  Tuple *txt_t = dict_find(iter, MESSAGE_KEY_ResponseChunkText);
  if (!idx_t || !tot_t || !txt_t) return;

  int idx   = idx_t->value->int32;
  int total = tot_t->value->int32;
  const char *text = txt_t->value->cstring;

  if (total <= 0 || total > MAX_CHUNKS || idx < 0 || idx >= total) {
    free_buffer();
    if (s_on_error) s_on_error(ERR_RESPONSE_TOO_LARGE);
    return;
  }

  if (s_buffer == NULL) {
    s_buffer = malloc((size_t)total * CHUNK_SIZE + 1);
    if (!s_buffer) { if (s_on_error) s_on_error(ERR_OUT_OF_MEMORY); return; }
    s_buffer[0] = '\0';
    s_total_chunks = total;
    s_received_mask = 0;
  }

  size_t offset = (size_t)idx * CHUNK_SIZE;
  size_t len    = strlen(text);
  if (len > CHUNK_SIZE) len = CHUNK_SIZE;
  memcpy(s_buffer + offset, text, len);
  s_buffer[offset + len] = '\0';

  s_received_mask |= (1u << idx);
  send_chunk_ack(idx);
  reset_timeout();

  uint32_t complete_mask = (s_total_chunks >= 32) ? 0xFFFFFFFFu
                                                  : ((1u << s_total_chunks) - 1u);
  if (s_received_mask == complete_mask) {
    size_t total_len = (size_t)(s_total_chunks - 1) * CHUNK_SIZE + strlen(text);
    s_buffer[total_len] = '\0';
    char *owned = s_buffer;
    s_buffer = NULL;
    free_buffer();
    if (s_on_response) s_on_response(owned);
  }
}

static void outbox_failed(DictionaryIterator *iter, AppMessageResult reason, void *ctx) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "outbox_failed: %d", reason);
  if (s_on_error) s_on_error(ERR_TRANSPORT_FAILED);
}

void transport_init(TransportResponseHandler on_response, TransportErrorHandler on_error) {
  s_on_response = on_response;
  s_on_error    = on_error;
  app_message_register_inbox_received(inbox_received);
  app_message_register_outbox_failed(outbox_failed);
  const uint32_t inbox_size  = app_message_inbox_size_maximum();
  const uint32_t outbox_size = app_message_outbox_size_maximum();
  AppMessageResult r = app_message_open(inbox_size, outbox_size);
  if (r != APP_MSG_OK) APP_LOG(APP_LOG_LEVEL_ERROR, "app_message_open failed: %d", r);
}

void transport_deinit(void) { free_buffer(); }

void transport_send_utterance(const char *utterance) {
  DictionaryIterator *out;
  if (app_message_outbox_begin(&out) != APP_MSG_OK) {
    if (s_on_error) s_on_error(ERR_TRANSPORT_FAILED); return;
  }
  dict_write_cstring(out, MESSAGE_KEY_UserMessage, utterance);
  if (app_message_outbox_send() != APP_MSG_OK)
    if (s_on_error) s_on_error(ERR_TRANSPORT_FAILED);
}

void transport_send_reset(void) {
  DictionaryIterator *out;
  if (app_message_outbox_begin(&out) != APP_MSG_OK) return;
  dict_write_int32(out, MESSAGE_KEY_ResetConversation, 1);
  app_message_outbox_send();
}

void transport_send_cancel(void) {
  DictionaryIterator *out;
  if (app_message_outbox_begin(&out) != APP_MSG_OK) return;
  dict_write_int32(out, MESSAGE_KEY_CancelInflight, 1);
  app_message_outbox_send();
}

void transport_send_provider(int provider) {
  DictionaryIterator *out;
  if (app_message_outbox_begin(&out) != APP_MSG_OK) return;
  dict_write_int32(out, MESSAGE_KEY_Provider, provider);
  app_message_outbox_send();
}
