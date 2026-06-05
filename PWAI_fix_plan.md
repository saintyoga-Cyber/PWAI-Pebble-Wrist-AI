# PWAI Fix Plan — Code Assessment (June 2026)

> **Status:** Draft — awaiting approval before any code changes are made.
> **Last updated:** 2026-06-05

---

## Summary

Full code audit of `main` branch post-merge of Critical-1 through Critical-4.
The architecture is solid. All 4 critical merges landed. However, there are
**2 build-breaking issues** and **2 missing worker-lifecycle calls** that will
prevent the background worker from ever running. One orphan file set also
needs cleanup.

Fixes are ordered by severity. **Each critical fix is its own isolated change.**
No fix shall be bundled with another.

---

## Issue Register

### 🔴 FIX-1 — wscript: wrong worker source path (BUILD-BREAKING)

**File:** `wscript`  
**Severity:** Critical — worker binary is never compiled  
**Blocks:** Background worker feature (Critical-3 & Critical-4)

**Root cause:**  
The `wscript` build script checks for the worker at `worker_src/` (legacy path):

```python
build_worker = os.path.exists('worker_src')
# ...
ctx.pbl_build(source=ctx.path.ant_glob('worker_src/c/**/*.c'), ...)
```

But `worker.c` was placed at `src/worker/worker.c` during the Critical-3 merge.
The `worker_src/` directory does not exist, so `build_worker` is always `False`.
The worker binary is never built into the `.pbw`.

**Required fix:**  
Change the wscript to detect and build from the correct path:

```python
# Before:
build_worker = os.path.exists('worker_src')
# ...
ctx.pbl_build(source=ctx.path.ant_glob('worker_src/c/**/*.c'), ...)

# After:
build_worker = os.path.exists('src/worker')
# ...
ctx.pbl_build(source=ctx.path.ant_glob('src/worker/**/*.c'), ...)
```

**Risk:** Low. Pure path change. No logic altered. Must be done alone.

---

### 🔴 FIX-2 — src/c/main.c: worker lifecycle calls missing (FUNCTIONALITY-BREAKING)

**File:** `src/c/main.c`  
**Severity:** Critical — background worker is never started or stopped  
**Blocks:** Background worker feature end-to-end

**Root cause:**  
`worker.c` is designed to be launched by the foreground app via
`app_worker_launch()` when a query is in-flight (STATE_WAITING), and killed via
`app_worker_kill()` after the reply is displayed. Neither call exists anywhere
in `src/c/main.c` or `src/c/state.c`.

Without `app_worker_launch()`, the worker binary (even after FIX-1) will simply
never run. The worker also expects a `WORKER_MSG_JOB_STARTED` message to begin
polling — this is also missing.

**Required fix (3 additions to `src/c/main.c`):**

1. **In `on_dictation_done()`** — launch the worker and signal job start when
   entering STATE_WAITING:

```c
static void on_dictation_done(const char *utterance) {
  state_set_pending_user_text(utterance);
  state_set(STATE_SENDING);
  transport_send_utterance(utterance);
  state_set(STATE_WAITING);
  // Launch background worker so it can poll for the reply if foreground closes.
  if (app_worker_launch() == APP_WORKER_RESULT_SUCCESS ||
      app_worker_launch() == APP_WORKER_RESULT_ALREADY_RUNNING) {
    AppWorkerMessage msg = { .data0 = 0, .data1 = 0, .data2 = 0 };
    app_worker_send_message(WORKER_MSG_JOB_STARTED, &msg);
  }
}
```

2. **In `on_response()`** — clear the persist flag and signal the worker to stop
   after displaying the reply:

```c
static void on_response(char *owned_response) {
  // Clear the pending-job flag set by transport.c so the worker stops polling.
  persist_delete(PERSIST_KEY_PENDING_JOB);
  // Signal worker to clear its active job.
  AppWorkerMessage msg = { .data0 = 0, .data1 = 0, .data2 = 0 };
  app_worker_send_message(WORKER_MSG_JOB_CLEAR, &msg);

  if (state_query_elapsed_ms() >= HAPTIC_GATE_MS) {
    vibes_short_pulse();
  }
  state_commit_turn(owned_response);
  state_set(STATE_SHOWING);
}
```

3. **In `on_transport_error()`** — also clear the job on error:

```c
static void on_transport_error(OwuiErrorCode code) {
  persist_delete(PERSIST_KEY_PENDING_JOB);
  AppWorkerMessage msg = { .data0 = 0, .data1 = 0, .data2 = 0 };
  app_worker_send_message(WORKER_MSG_JOB_CLEAR, &msg);
  state_set_error(code);
}
```

**Risk:** Medium. Adds worker lifecycle calls. Must be done alone, after FIX-1.

---

### 🟡 FIX-3 — Delete orphan file: `src/main.c` (v1 dead code)

**File:** `src/main.c`  
**Severity:** Medium — dead code, source of confusion  
**Blocks:** Nothing (file is not compiled — wscript builds `src/c/**/*.c`)

**Root cause:**  
`src/main.c` is the original v1 single-file C app. It uses raw integer message
keys (0–9) instead of the named `MESSAGE_KEY_*` constants in `message_keys.h`,
and has no concept of state.c / transport.c / dictation. It is not compiled.
However, its presence will mislead anyone editing the project.

**Required fix:** Delete `src/main.c`.

**Risk:** Zero. File is not compiled.

---

### 🟡 FIX-4 — Delete orphan file: `pkjs/index.js` (v1 dead JS bridge)

**File:** `pkjs/index.js`  
**Severity:** Medium — dead code, contains placeholder API_BASE  
**Blocks:** Nothing (wscript JS entry point is `src/pkjs/index.js`)

**Root cause:**  
`pkjs/index.js` is the v1 PebbleKit JS bridge with:
- `API_BASE = 'https://YOUR-WORKER.YOUR-SUBDOMAIN.workers.dev'` (placeholder)
- Raw integer key schema (0–9) inconsistent with v2
- No intent classifier, no chunker module

The wscript correctly uses `src/pkjs/index.js` as the JS entry point, so this
file is never executed. But it causes confusion alongside the real bridge.

**Required fix:** Delete `pkjs/index.js`.

**Risk:** Zero. File is not included in the build.

---

### 🔵 FIX-5 — Delete stale feature branches

**Branches:** `feat/critical-1-websocket`, `feat/critical-2-haptic-gate`,
`feat/critical-3-background-worker`, `feat/critical-4-persist-flag`  
**Severity:** Low — housekeeping  
**Blocks:** Nothing

All 4 branches have been merged into `main`. They serve no further purpose.

**Required fix:** Delete all 4 branches from GitHub.

**Risk:** Zero.

---

## Confirmed Good (No Fix Needed)

| Item | Status |
|---|---|
| `persist_write_int(PERSIST_KEY_PENDING_JOB, 1)` in `transport.c` | ✅ Present in chunk-complete path |
| `PERSIST_KEY_PENDING_JOB` value (100u) matches between `message_keys.h` and `worker.c` | ✅ Consistent |
| `appinfo.json` messageKeys match `src/pkjs/index.js` and `src/c/message_keys.h` | ✅ Consistent |
| `background_worker` capability declared in `appinfo.json` | ✅ Present |
| Critical-2 haptic gate (5s threshold, `state_query_elapsed_ms()`) | ✅ Implemented in `state.c` + `main.c` |
| Critical-4 `persist_write` landing in correct file (`transport.c`) | ✅ Confirmed |
| `CHUNK_SIZE` in `message_keys.h` (2048) vs `src/pkjs/chunker.js` | ✅ To verify (chunker.js not yet read) |
| Worker `PERSIST_KEY_PENDING_JOB` mirror value (100u) | ✅ Matches `message_keys.h` |
| Worker does NOT clear the flag (foreground owns it) | ✅ Correct in `worker.c` |
| `wscript` JS glob includes `src/pkjs/**/*.js` | ✅ Correct |

---

## Fix Execution Order

```
FIX-1  →  FIX-2  →  FIX-3 + FIX-4 (can be one commit)  →  FIX-5
```

FIX-1 and FIX-2 are **critical** and must each be a standalone, single-purpose
commit. FIX-3 and FIX-4 are cleanup and can be combined. FIX-5 is branch
deletion only.

---

## Open Questions

1. **`src/pkjs/chunker.js`** — Not yet fully read. The `CHUNK_SIZE` constant
   inside it must match `message_keys.h` (2048 bytes). Needs verification
   before FIX-1/FIX-2 are implemented.

2. **`scroll_layer_set_click_config_onto_window` in `src/main.c` (v1)** —
   This is in the orphan file only; `src/c/ui_response.c` and `src/c/ui_idle.c`
   handle clicks in v2. No action needed, but worth confirming during testing.

3. **Worker not auto-relaunched after watch reboot** — By design per `worker.c`
   comments. Acceptable trade-off. Document in README if desired.

---

*Document maintained by the PWAI vibe-coding session. Update this file before
applying any code changes. Do not bundle critical fixes with cleanup.*
