# PWAI — Pebble Wrist AI

Chat with Claude & Perplexity directly from your Pebble smartwatch.
Features timeline pins, background notifications, persistent memory, and provider switching on-wrist.

## Repo structure

```
appinfo.json          — Pebble app manifest
src/main.c            — Watch app (C): UI, chunked reply assembly, provider toggle
pkjs/index.js         — PebbleKit JS bridge (phone): HTTP relay, polling, token registration
worker/
  wrangler.toml       — Cloudflare Worker config
  schema.sql          — D1 SQLite schema (users / jobs / conversations / pins)
  src/index.js        — Worker: /register /chat /status /pin routes + Claude & Perplexity
```

## Setup checklist

### 1 — Cloudflare Worker
```bash
cd worker
npm install
wrangler d1 create pebbleai              # copy the database_id into wrangler.toml
wrangler d1 execute pebbleai --file=schema.sql
wrangler secret put PERPLEXITY_API_KEY
wrangler secret put ANTHROPIC_API_KEY
npm run deploy                           # note your *.workers.dev URL
```

### 2 — PebbleKit JS
Edit `pkjs/index.js` — replace `API_BASE` with your deployed Worker URL.

### 3 — Pebble project
Build `src/main.c` with `appinfo.json` using the Pebble SDK or CloudPebble (Rebble).

**Buttons:**
- **UP** — switch to Perplexity
- **DOWN** — switch to Claude
- **SELECT** — send starter prompt

## What's coming (Phase 2)
- Background worker wake-on-ping flow
- Quick-prompt MenuLayer on the watch
- Reminder scheduling from the watch
- Durable Object alarms for reliable async task-ready pings
