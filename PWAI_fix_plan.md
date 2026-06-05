# PWAI Fix Plan — Code Assessment (June 2026)

> **Status:** FIX-1 ✅ FIX-2 ✅ — both critical fixes applied. FIX-3/4/5 pending approval.
> **Last updated:** 2026-06-05

---

## Summary

Full code audit of `main` branch post-merge of Critical-1 through Critical-4.
The architecture is solid. All 4 critical merges landed. The 2 build-breaking
and functionality-breaking issues have now been fixed (FIX-1 & FIX-2).
Remaining items are cleanup only and do not affect build or runtime.

---

## Issue Register

### 🔴 FIX-1 — wscript: wrong worker source path (BUILD-BREAKING)

**File:** `wscript`  
**Status:** ✅ APPLIED — commit [`be0e8f1`](https://github.com/saintyoga-Cyber/PWAI-Pebble-Wrist-AI/commit/be0e8f1014a337502ecb0a75a63803bf48564ae0)

`worker_src/` never existed; `worker.c` lives at `src/worker/worker.c`
since the Critical-3 merge. `build_worker` was always `False`, so the
worker binary was never compiled into the `.pbw`.

Fixed: changed existence check and `ant_glob` path to `src/worker/`.

---

### 🔴 FIX-2 — src/c/main.c: worker lifecycle calls missing (FUNCTIONALITY-BREAKING)

**File:** `src/c/main.c`  
**Status:** ✅ APPLIED — commit [`dc79465`](https://github.com/saintyoga-Cyber/PWAI-Pebble-Wrist-AI/commit/dc794657a93f43533a116a00d2b3d99ad653419c)

The worker binary (now correctly built by FIX-1) was never launched,
never signalled, and the persist flag was never cleared.

Fixed with 3 additions to `src/c/main.c`:
1. `on_dictation_done()` — `app_worker_launch()` + `WORKER_MSG_JOB_STARTED`.
2. `on_response()` — `persist_delete(PERSIST_KEY_PENDING_JOB)` + `WORKER_MSG_JOB_CLEAR`.
3. `on_transport_error()` — same cleanup so errors don’t leave worker polling forever.

A `worker_clear_job()` helper was added to deduplicate items 2 & 3.

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
| `CHUNK_SIZE` in `message_keys.h` (2048) vs `src/pkjs/chunker.js` | ✅ Both 2048, MAX_CHUNKS both 16 |
| Chunker ack-gated sending matches `send_chunk_ack()` in `transport.c` | ✅ Consistent |
| Worker `PERSIST_KEY_PENDING_JOB` mirror value (100u) | ✅ Matches `message_keys.h` |
| Worker does NOT clear the flag (foreground owns it) | ✅ Correct in `worker.c` |
| `wscript` JS glob includes `src/pkjs/**/*.js` | ✅ Correct |

---

## Fix Execution Order

```
FIX-1 ✅  →  FIX-2 ✅  →  FIX-3 + FIX-4 (pending)  →  FIX-5 (pending)
```

---

## Open Questions

1. ~~`src/pkjs/chunker.js` CHUNK_SIZE — verified: both 2048, MAX_CHUNKS 16. ✅ Closed.~~

2. **Worker not auto-relaunched after watch reboot** — By design per `worker.c`
   comments. Acceptable trade-off. Document in README if desired.

---

*Document maintained by the PWAI vibe-coding session. Update this file before
applying any code changes. Do not bundle critical fixes with cleanup.*
