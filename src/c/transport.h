#pragma once
#include "message_keys.h"

typedef void (*TransportResponseHandler)(char *owned_response);
typedef void (*TransportErrorHandler)(OwuiErrorCode code);

void transport_init(TransportResponseHandler on_response, TransportErrorHandler on_error);
void transport_deinit(void);
void transport_send_utterance(const char *utterance);
void transport_send_reset(void);
void transport_send_cancel(void);
// PWAI addition: send selected provider to JS bridge (0=Perplexity, 1=Claude)
void transport_send_provider(int provider);
