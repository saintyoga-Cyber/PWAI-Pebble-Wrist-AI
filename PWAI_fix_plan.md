# PWAI Fix Plan — Code Assessment (June 2026)

> **Status:** FIX-1 ✅ FIX-2 ✅ FIX-3 ✅ FIX-4 ✅ — FIX-5 requires manual branch deletion.
> **Last updated:** 2026-06-05

---

## Summary

Full code audit of `main` branch post-merge of Critical-1 through Critical-4.
All build-breaking and functionality-breaking issues have been resolved.
The codebase is clean. Only stale branch deletion (FIX-5) remains.

---

## Issue Register

### 🔴 FIX-1 — wscript: wrong worker source path
**Status:** ✅ APPLIED — commit [`be0e8f1`](https://github.com/saintyoga-Cyber/PWAI-Pebble-Wrist-AI/commit/be0e8f1014a337502ecb0a75a63803bf48564ae0)

Changed `worker_src/` → `src/worker/` in both the existence check and
`ant_glob`. Worker binary now compiles into the `.pbw`.

---

### 🔴 FIX-2 — src/c/main.c: worker lifecycle calls missing
**Status:** ✅ APPLIED — commit [`dc79465`](https://github.com/saintyoga-Cyber/PWAI-Pebble-Wrist-AI/commit/dc794657a93f43533a116a00d2b3d99ad653419c)

Added `app_worker_launch()` + `WORKER_MSG_JOB_STARTED` on query dispatch,
and `persist_delete` + `WORKER_MSG_JOB_CLEAR` on reply/error.
Added `#include <pebble_worker.h>` and `worker_clear_job()` helper.

---

### 🟡 FIX-3 — Delete orphan `src/main.c`
**Status:** ✅ APPLIED — commit [`c675308`](https://github.com/saintyoga-Cyber/PWAI-Pebble-Wrist-AI/commit/c675308a2d533169d81f1942c28e51c7a69be163)

---

### 🟡 FIX-4 — Delete orphan `pkjs/index.js`
**Status:** ✅ APPLIED — commit [`b64020e`](https://github.com/saintyoga-Cyber/PWAI-Pebble-Wrist-AI/commit/b64020e9b55429d799ec1ce1128c32987dd763b8)

---

### 🔵 FIX-5 — Delete stale feature branches
**Status:** ⏳ Manual step required

The 4 merged branches cannot be deleted via the current toolset.
Delete them from the GitHub web UI (repo → Branches) or via CLI:

```bash
git push origin --delete feat/critical-1-websocket
git push origin --delete feat/critical-2-haptic-gate
git push origin --delete feat/critical-3-background-worker
git push origin --delete feat/critical-4-persist-flag
```

Or in one shot:
```bash
for b in feat/critical-1-websocket feat/critical-2-haptic-gate feat/critical-3-background-worker feat/critical-4-persist-flag; do
  git push origin --delete "$b"
done
```

---

## Confirmed Good (No Fix Needed)

| Item | Status |
|---|---|
| `persist_write_int(PERSIST_KEY_PENDING_JOB, 1)` in `transport.c` | ✅ Present |
| `PERSIST_KEY_PENDING_JOB` value (100u) consistent across all files | ✅ Verified |
| `appinfo.json` messageKeys match v2 C and JS layers | ✅ Consistent |
| `background_worker` capability in `appinfo.json` | ✅ Present |
| Critical-2 haptic gate (`state_query_elapsed_ms()`, 5s) | ✅ Implemented |
| `CHUNK_SIZE` = 2048 and `MAX_CHUNKS` = 16 on both C and JS sides | ✅ Verified |
| Chunker ack-gated sending matches `send_chunk_ack()` in `transport.c` | ✅ Consistent |
| Worker does NOT clear the persist flag (foreground owns it) | ✅ Correct |
| `wscript` JS glob covers `src/pkjs/**/*.js` | ✅ Correct |

---

## Notes

- **Worker not auto-relaunched after watch reboot** — By design. The user must
  open the app once; the worker will then be launched on next query.
  Document in README if desired.

---

*Document maintained by the PWAI vibe-coding session. All critical and medium
fixes are complete. FIX-5 is branch hygiene only.*
