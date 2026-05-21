# PWAI — Pebble Wrist AI

Chat with Claude & Perplexity directly from your Pebble smartwatch.
Voice input · chat-bubble UI · timeline pins · background notifications · persistent memory.

## v0.2 highlights
- Full C rewrite: FSM state machine, bitmask chunk tracking, ChunkAck handshake
- Voice dictation (SELECT to talk)
- Chat-bubble ScrollLayer UI (blue = you, orange = AI)
- Spinner with elapsed-seconds counter + BACK-to-cancel
- **Provider toggle on idle screen: UP = Perplexity · DOWN = Claude**
- Production-grade sanitizer: strips Markdown, curly quotes, reasoning tokens
- Cloudflare Worker backend with async job polling + Timeline pin support

## Repo layout
```
appinfo.json          — Pebble app manifest
wscript               — Waf multi-file build
src/c/
  main.c              — App coordinator
  state.c/h           — State machine + 8-turn conversation ring
  transport.c/h       — AppMessage (bitmask chunking + ACK)
  dictation.c/h       — DictationSession voice input
  ui_idle.c/h         — Idle screen  (SELECT=talk, UP/DOWN=provider)
  ui_spinner.c/h      — Thinking screen with elapsed timer
  ui_response.c/h     — Chat-bubble scroll view
  message_keys.h      — Wire constants
src/pkjs/
  index.js            — Phone bridge → Worker → poll → chunk delivery
  chunker.js          — ACK-gated chunker + Pebble sanitizer pipeline
  config.js           — localStorage config (font, system prompt, provider)
worker/               — Cloudflare Worker (deploy separately)
  wrangler.toml
  schema.sql
  src/index.js
```

## Quick-start

### 1 · Deploy the Cloudflare Worker
```bash
cd worker
npm install
wrangler d1 create pebbleai           # copy database_id into wrangler.toml
wrangler d1 execute pebbleai --file=schema.sql
wrangler secret put PERPLEXITY_API_KEY
wrangler secret put ANTHROPIC_API_KEY
npm run deploy                         # note your *.workers.dev URL
```

### 2 · Point the JS bridge at your Worker
Edit `src/pkjs/index.js` line ~18:
```js
var API_BASE = 'https://your-worker.your-subdomain.workers.dev';
```

### 3 · Build & install on watch
Import the repo into [CloudPebble](https://cloudpebble.net) (Rebble), build, install via Bluetooth.

## On-watch controls
| Button | Idle | Response |
|--------|------|----------|
| SELECT | Start voice input | Start follow-up |
| UP     | Switch to **Perplexity** | Scroll up |
| DOWN   | Switch to **Claude** | Scroll down |
| BACK   | Exit app | Return to idle |

## Next steps (v0.3 roadmap)
- [ ] Cloudflare Worker `worker/` folder (D1 DB, `/chat`, `/status`, `/pin`, `/register`)
- [ ] Clay config page (Worker URL, system prompt, font size)
- [ ] Timeline pin push from Worker when async task completes
- [ ] Background worker polling (wake app on pin arrival)
