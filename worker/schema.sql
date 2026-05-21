-- PWAI D1 schema
-- Run: wrangler d1 execute pebbleai --file=schema.sql

CREATE TABLE IF NOT EXISTS users (
  token      TEXT PRIMARY KEY,
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
  preferred_provider TEXT DEFAULT 'perplexity'
);

CREATE TABLE IF NOT EXISTS conversations (
  id         INTEGER PRIMARY KEY AUTOINCREMENT,
  token      TEXT NOT NULL,
  role       TEXT NOT NULL,
  content    TEXT NOT NULL,
  provider   TEXT NOT NULL,
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS jobs (
  id         TEXT PRIMARY KEY,
  token      TEXT NOT NULL,
  provider   TEXT NOT NULL,
  prompt     TEXT NOT NULL,
  status     TEXT NOT NULL,
  reply      TEXT,
  error      TEXT,
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS pins (
  pin_id     TEXT PRIMARY KEY,
  token      TEXT NOT NULL,
  title      TEXT NOT NULL,
  body       TEXT,
  pin_time   TEXT NOT NULL,
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_conv_token  ON conversations(token);
CREATE INDEX IF NOT EXISTS idx_jobs_token  ON jobs(token);
CREATE INDEX IF NOT EXISTS idx_jobs_status ON jobs(status);
