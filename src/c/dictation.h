#pragma once
#define MAX_UTTERANCE 1024
typedef void (*DictationDoneHandler)(const char *utterance);
typedef void (*DictationFailHandler)(int status);
void dictation_init(DictationDoneHandler on_done, DictationFailHandler on_fail);
void dictation_deinit(void);
void dictation_start(void);
#ifdef OWUI_DEBUG_FAKE_DICTATION
void dictation_debug_reset(void);
#endif
