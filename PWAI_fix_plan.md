# PWAI Fix Plan — Code Assessment (June 2026)

> **Status:** FIX-1 ✅ FIX-2 ✅ FIX-3 ✅ FIX-4 ✅ — FIX-5 requires manual branch deletion.
> **Last updated:** 2026-06-11 — added the **v0.3 Full Review** (issues R1–R17) below.
> ⚠️ The "codebase is clean" conclusion of the 2026-06-05 audit is **superseded** —
> the new review found 7 critical issues (see Review section).

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

*Document maintained by the PWAI vibe-coding session.*

---
---

# v0.3 Full Review — 2026-06-11

Scope: entire repo (watch C, PebbleKit JS, Cloudflare Worker, build files) plus
up-to-date Pebble tooling research (developer.repebble.com / Rebble docs).
**No code has been modified — this section is the plan only.**

Rule applied throughout: every 🔴 critical issue is a **standalone change**
(own branch/commit, nothing bundled with it).

## Verdict in one paragraph

The watch-side C code (state machine, chunked transport with ACKs, chat-bubble
UI, dictation) is genuinely good. The three layers, however, do not agree with
each other: the JS layer uses APIs that don't exist in PebbleKit JS (`fetch`,
arbitrary `Pebble.openURL`), the Worker has a deploy blocker and two
Claude-API bugs that make `/reminder` fail 100% of the time, and the
background-worker "wake the app when a reply lands" design cannot work on
Pebble at all. The good news: the honest replacement for the broken background
path is exactly the feature you want most — **timeline pins pushed by the
Cloudflare Worker** — and `/pin` plus the Rebble timeline already work.

## Build / CloudPebble issues

### 🔴 R1 — `touch_service` API breaks the build on every current target
`src/c/ui_response.c` uses `touch_service_subscribe()` / `TouchEvent` /
`touch_service_unsubscribe()`. That API was introduced in the **new
Core Devices PebbleOS SDK for the touchscreen Pebble Time 2** (PebbleOS
4.9.171+). None of the platforms in `appinfo.json`
(`aplite/basalt/chalk/diorite/emery`) have a touchscreen, and the SDK that
Rebble's CloudPebble builds against does not define those symbols →
**compile error**. This is almost certainly why CloudPebble won't build the
project.

**Fix (standalone):** wrap the touch block in the platform guard the SDK
provides (`#if defined(PBL_TOUCH) … #endif`) around the `touch_service_*`
calls, the `TouchEvent` handler, and the three `s_touch_*` statics — or simply
delete the block until a Time 2 target is added. Buttons already cover
scrolling.

### 🟡 R2 — CloudPebble is the wrong vehicle for this repo
CloudPebble (cloudpebble.net) still exists (Core Devices modernised it in
2026) but it **ignores your custom `wscript`** and uses its own fixed build:
the multi-directory `src/c/` + `src/worker/` layout and the FIX-1 worker path
will not survive an import. Current recommended workflows
(developer.repebble.com/sdk):
1. **cloud.repebble.com** — browser VS Code with the SDK + emulators
   pre-installed. Respects `wscript`. **Recommended for this repo.**
2. **Local `pebble-tool`** — `pip`/`uv` install, needs Python 3.10–3.13.
   Same benefits.
3. CloudPebble — only for simple single-folder projects.

**Action:** stop fighting CloudPebble; build via 1 or 2. No code change.

### 🟡 R3 — Drop `aplite` from `targetPlatforms`
- aplite (Pebble Classic/Steel) has **no microphone** — `dictation_session_create()`
  returns NULL, so a voice-only app is dead on arrival there.
- aplite has ~24 KB of app heap; `app_message_open(inbox_max, outbox_max)`
  (~8 KB each) plus a worst-case 32 KB response buffer (`16 × 2048`) cannot fit.

**Fix (standalone, after R1):** remove `"aplite"` from `appinfo.json`.

### 🔵 R4 — Legacy project format (informational)
`appinfo.json` is the legacy CloudPebble-era manifest. The modern SDK uses
`package.json` with a `"pebble"` key (`pebble convert-project` does it
automatically). Not blocking — the tools still read `appinfo.json` — but plan
the conversion when moving to cloud.repebble.com.

## Watch C issues

### 🔴 R5 — Haptic gate timer is broken (`time_ms` misuse)
`state.c` uses `(uint32_t)time_ms(NULL, NULL)` as a millisecond clock.
`time_ms()` returns only the **milliseconds within the current second
(0–999)**. So `state_query_elapsed_ms()` returns noise and the Critical-2
5-second haptic gate fires (or stays silent) essentially at random.

**Fix (standalone):**
```c
static uint64_t now_ms(void) {
  time_t s; uint16_t ms = time_ms(&s, &ms);
  return (uint64_t)s * 1000 + ms;
}
```
Store `s_query_start_ms` as `uint64_t`; the wrap-around branch can be deleted.

### 🔴 R6 — The background-worker design cannot work (architecture)
Three independent reasons, any one fatal:
1. **PebbleKit JS dies when the watchapp closes.** Polling in `index.js`
   stops, so the reply is never fetched while the app is closed.
2. **AppMessage only reaches a running foreground app**, and the persist flag
   is only written by `transport.c` *in the foreground*. The flag the worker
   polls for can never be set while the app is closed.
3. Even if it were set: the foreground never calls
   `app_worker_message_subscribe()`, so `WORKER_MSG_REPLY_READY` is dropped,
   and the worker never calls `worker_launch_app()` to actually open the app.

**Fix (standalone, replaces FIX-2/Critical-3/Critical-4 machinery):**
delete `src/worker/`, the `pebble_worker` calls in `main.c`, the persist-flag
write in `transport.c`, and `background_worker` from `appinfo.json`. The
"answer arrives while app is closed" feature is delivered server-side instead:
the **Cloudflare Worker pushes a timeline pin** when a job completes (see R13)
— the pin notifies the wrist, and `actions: openWatchApp` reopens PWAI.
On launch, JS checks the last jobId in `localStorage` via `/status` and
delivers any finished reply.

### 🔵 R7 — Chunk reassembly is silently coupled to the sanitizer
`transport.c` places chunk *i* at offset `i × CHUNK_SIZE` and computes the
total length assuming every non-final chunk is exactly `CHUNK_SIZE` bytes.
That only holds because `sanitizeForPebble()` strips all non-ASCII before
`splitUtf8()` (which otherwise produces short chunks at UTF-8 boundaries).
Works today; document the invariant with a comment in both files, or switch
to append-order reassembly. Low priority.

## PebbleKit JS issues

### 🔴 R8 — `fetch()` is not a PebbleKit JS API
The documented (and only guaranteed) network API in the PebbleKit JS sandbox
is **`XMLHttpRequest`** — `fetch`/Promises are not part of the runtime
contract. Your own Sports-simplified app uses XHR for exactly this reason.
Every network call in `src/pkjs/index.js` (`/chat`, `/status`, `/reminder`,
`/register`) uses `fetch().then()` and will throw `fetch is not defined` on
the stock mobile apps.

**Fix (standalone):** add a small `xhrJson(method, url, body, cb)` helper
(mirror `Sports-simplified/src/pkjs/timeline.js`, incl. `xhr.timeout`) and
convert the five call sites. Pure mechanical change, ES5 callbacks.

### 🔴 R9 — `Pebble.openURL()` cannot launch iOS Shortcuts
`Pebble.openURL()` is only valid inside a `showConfiguration` handler (it
opens the app's config page). Calling it from `triggerReminderShortcut()` /
`triggerNoteShortcut()` does nothing — and the code then tells the user
"Reminder queued via Shortcuts." / "Note saved via Shortcuts." which is
false. The entire iOS-Shortcut bridge is non-functional.

**Fix (standalone):** remove the Shortcut paths. Reminders go to the
**timeline pin** path for both providers (R13); notes can become pins too
(or be dropped for now). If a phone-side action is ever needed, the supported
pattern is a config-page interaction, not URL schemes.

### 🟡 R10 — Polling never times out
`startPolling()` retries every 15 s forever. If the Worker errors without
writing a status, the watch spins endlessly. Add a max (e.g. 20 polls = 5 min)
then `chunker.sendErrorCode(8 /* ERR_TIMEOUT */)` — the C side already has
the error text for it.

### 🟡 R11 — Listener leak in `chunker.sendChunked`
Each send registers a new `appmessage` listener and relies on
`Pebble.removeEventListener` (wrapped in try/catch because it may not exist
on all runtimes). If removal fails, dead listeners accumulate and every ACK
fans out to all of them. Restructure to **one** module-level ack listener
registered once, dispatching to the active transfer.

### 🟡 R12 — Memory has two sources of truth
JS keeps a `conversation` array and sends `history`, but the Worker ignores
it and rebuilds history from D1 (`LIMIT 10`). Consequences:
- `ResetConversation` (sent on every app launch!) only clears the JS copy —
  server memory silently persists, so "reset" lies and every launch *looks*
  like a fresh chat while the model still remembers.
- The user-configurable system prompt in `config.js` **never reaches the
  model** (see R16).

Since persistent memory is the selling point: make **D1 the single source of
truth**. Delete the JS `conversation` array, send only `prompt` (+ system
prompt), and add a Worker `POST /reset` that deletes the token's
`conversations` rows; wire `ResetConversation` to it. Decide deliberately
whether app launch should still send reset (probably not — memory should
survive launches).

## Cloudflare Worker issues

### 🔴 R13 — `/reminder` creates nothing; push the timeline pin
`processReminderJob` extracts title/date via Claude tool-use and then merely
writes a confirmation string. No pin, no reminder, nowhere. Meanwhile
`pushPin()` and the `pins` table sit unused, and the Rebble timeline API is
confirmed working (Sports-simplified pushes pins to the same endpoint today;
sandbox tokens work for sideloaded apps).

**Fix (standalone — this is the headline feature):** after a successful
tool_use, compute `pinTime` from `due_date`+`due_time` (default e.g. 09:00,
watch the timeline's ~2-day visibility window), call
`pushPin(token, input.title, input.notes, pinTime)`, store in `pins`, and
only then set status `ready` with "Reminder pinned: …". Also call `pushPin`
from `processJob` when a *chat* job finishes (title "PWAI reply ready") —
that is the R6 replacement for background notification.

### 🔴 R14 — `wrangler.toml` deploy blocker: phantom Durable Object
`wrangler.toml` declares `JOB_SOCKET` / `class_name = "JobSocket"` plus a
migration, but `src/index.js` exports no `JobSocket` class →
`wrangler deploy` fails. The polling architecture doesn't need a DO.

**Fix (standalone):** delete the `[[durable_objects.bindings]]` and
`[[migrations]]` blocks.

### 🔴 R15 — Claude API: dead model ID + invalid `tool_choice`
1. Both call sites use `model: 'claude-sonnet-4-20250514'` — that model is
   deprecated and **retires 2026-06-15** (four days after this review). All
   Claude traffic then 404s. Use `claude-sonnet-4-6` (exact string, no date
   suffix).
2. `tool_choice: { type: 'required', name: 'create_reminder' }` is not a
   valid Anthropic shape (`required` is an OpenAI-ism). Valid values are
   `auto` / `any` / `none` / `{ type: 'tool', name: '…' }`. Today every
   `/reminder` request 400s → `tool_failed` → the (also broken, R9) Shortcut
   path. Use `{ type: 'tool', name: 'create_reminder' }`.

Two one-line edits, but per the bundling rule ship as **two separate
commits** (model swap affects `/chat`+`/reminder`; tool_choice only
`/reminder`).

### 🟡 R16 — History can start with `assistant`; system prompt never sent
`buildMessages()` takes the last 10 D1 rows; when the cut lands on an
assistant row the Anthropic API rejects the request ("first message must be
user") — intermittent 500s that worsen as history grows. Trim leading
assistant rows (or `LIMIT` by *pairs*). At the same time pass the system
prompt: accept `system` in `/chat`, store/forward it, and send it as the
top-level `system` parameter (Anthropic) / system message (Perplexity).
Until then the "keep it brief for a tiny screen" instruction never reaches
the model — replies will routinely blow past the 32 KB / `MAX_CHUNKS` cap.

### 🟡 R17 — Worker endpoints are unauthenticated
Anyone who finds the `workers.dev` URL can spend your Anthropic/Perplexity
credits. Cheapest fix: require a shared-secret header (checked at the top of
`fetch()`), set the same constant in `pkjs/index.js`. Also consider a
scheduled cleanup of old `jobs`/`conversations` rows.

## Confirmed good ✅

| Item | Notes |
|---|---|
| C state machine / ownership discipline | clean FSM, malloc/free pairing correct, ring buffer of turns correct |
| Bitmask chunking + ACK handshake | C and JS sides consistent (sizes, keys, ack-gating) |
| Chat-bubble ScrollLayer UI | careful layout math; only the touch block (R1) is a problem |
| Dictation wrapper incl. debug fake-dictation | correct platform API usage; error mapping sensible |
| `wscript` | correct for SDK builds (just not for CloudPebble — R2) |
| Sanitizer pipeline in `chunker.js` | thorough; also what makes R7 safe today |
| `/pin` endpoint + pin JSON shape | matches Rebble timeline API (PUT `…/v1/user/pins/<id>`, `X-User-Token`) |
| D1 schema | sane; indexes present |
| Timeline service itself | working in 2026 — proven daily by Sports-simplified |

## Recommended execution order

Each line = one standalone change, in this order:

1. **R14** — fix `wrangler.toml` (unblocks Worker deploys)
2. **R15a** — model → `claude-sonnet-4-6` (hard deadline 2026-06-15)
3. **R15b** — `tool_choice` → `{type:'tool', …}`
4. **R1** — guard/remove `touch_service` (unblocks watchapp build)
5. **R8** — `fetch` → XHR in pkjs
6. **R13** — `/reminder` + `/chat` push timeline pins
7. **R6** — remove background worker machinery
8. **R9** — remove Shortcut paths
9. **R5** — fix `time_ms` haptic gate
10. R12 / R16 — memory single-source-of-truth + system prompt (one design change, may ship together as they touch the same contract)
11. R3, R10, R11, R17 — hygiene batch
12. R2 / R4 — tooling migration (no code)

*Review performed 2026-06-11. No code or infrastructure was modified;
only this planning document was updated.*
