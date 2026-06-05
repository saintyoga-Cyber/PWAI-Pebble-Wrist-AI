# PWAI Fix Plan — Code Assessment (June 2026)

> **Status:** FIX-1 and FIX-2 approved — in progress.
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
**Status:** ✅ APPLIED

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

**Applied fix:**

```python
# Before:
build_worker = os.path.exists('worker_src')
ctx.pbl_build(source=ctx.path.ant_glob('worker_src/c/**/*.c'), ...)

# After:
build_worker = os.path.exists('src/worker')
ctx.pbl_build(source=ctx.path.ant_glob('src/worker/**/*.c'), ...)
```

---

### 🔴 FIX-2 — src/c/main.c: worker lifecycle calls missing (FUNCTIONALITY-BREAKING)

**File:** `src/c/main.c`  
**Severity:** Critical — background worker is never started or stopped  
**Blocks:** Background worker feature end-to-end  
**Status:** ✅ APPLIED

**Root cause:**  
`worker.c` is designed to be launched by the foreground app via
`app_worker_launch()` when a query is in-flight (STATE_WAITING), and killed via
`app_worker_kill()` after the reply is displayed. Neither call existed in
`src/c/main.c`. The worker also expected `WORKER_MSG_JOB_STARTED` to begin
polling — also missing.

**Applied fix (3 additions to `src/c/main.c`):**

1. `on_dictation_done()` — launch worker + send `WORKER_MSG_JOB_STARTED`.
2. `on_response()` — `persist_delete(PERSIST_KEY_PENDING_JOB)` + send `WORKER_MSG_JOB_CLEAR`.
3. `on_transport_error()` — `persist_delete(PERSIST_KEY_PENDING_JOB)` + send `WORKER_MSG_JOB_CLEAR`.

---

### 🟡 FIX-3 — Delete orphan file: `src/main.c` (v1 dead code)

**File:** `src/main.c`  
**Severity:** Medium — dead code, source of confusion  
**Blocks:** Nothing (file is not compiled — wscript builds `src/c/**/*.c`)  
**Status:** ⏳ Pending approval

**Required fix:** Delete `src/main.c`.  
**Risk:** Zero. File is not compiled.

---

### 🟡 FIX-4 — Delete orphan file: `pkjs/index.js` (v1 dead JS bridge)

**File:** `pkjs/index.js`  
**Severity:** Medium — dead code, contains placeholder API_BASE  
**Blocks:** Nothing (wscript JS entry point is `src/pkjs/index.js`)  
**Status:** ⏳ Pending approval

**Required fix:** Delete `pkjs/index.js`.  
**Risk:** Zero. File is not included in the build.

---

### 🔵 FIX-5 — Delete stale feature branches

**Branches:** `feat/critical-1-websocket`, `feat/critical-2-haptic-gate`,
`feat/critical-3-background-worker`, `feat/critical-4-persist-flag`  
**Severity:** Low — housekeeping  
**Status:** ⏳ Pending approval

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
| `CHUNK_SIZE` in `message_keys.h` (2048) vs `src/pkjs/chunker.js` | ✅ Verified — both 2048, MAX_CHUNKS both 16 |
| Chunker ack-gated sending matches `send_chunk_ack()` in `transport.c` | ✅ Consistent |
| Worker `PERSIST_KEY_PENDING_JOB` mirror value (100u) | ✅ Matches `message_keys.h` |
| Worker does NOT clear the flag (foreground owns it) | ✅ Correct in `worker.c` |
| `wscript` JS glob includes `src/pkjs/**/*.js` | ✅ Correct |

---

## Fix Execution Order

```
FIX-1 ✅  →  FIX-2 ✅  →  FIX-3 + FIX-4 (can be one commit)  →  FIX-5
```

FIX-1 and FIX-2 are **critical** and must each be a standalone, single-purpose
commit. FIX-3 and FIX-4 are cleanup and can be combined. FIX-5 is branch
deletion only.

---

## Open Questions

1. ~~**`src/pkjs/chunker.js`** — CHUNK_SIZE verified: both sides 2048, MAX_CHUNKS 16. ✅ Closed.~~

2. **`scroll_layer_set_click_config_onto_window` in `src/main.c` (v1)** —
   In the orphan file only; v2 handles clicks in `ui_response.c` / `ui_idle.c`.
   No action needed, but confirm during live testing.

3. **Worker not auto-relaunched after watch reboot** — By design per `worker.c`
   comments. Acceptable trade-off. Document in README if desired.

---

*Document maintained by the PWAI vibe-coding session. Update this file before
applying any code changes. Do not bundle critical fixes with cleanup.*
