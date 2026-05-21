/*
 * PWAI v0.2 — PebbleKit JS bridge
 *
 * Routes to a Cloudflare Worker (/chat) instead of a local server.
 * Provider is toggled from the watch (UP=Perplexity, DOWN=Claude).
 * Async job polling: Worker returns jobId; JS polls /status/:jobId every 15s.
 * Registers the Pebble timeline token with the Worker on launch (/register)
 * so the Worker can push Timeline pins independently.
 *
 * ACTION REQUIRED: replace API_BASE with your deployed Cloudflare Worker URL.
 */

var chunker = require('./chunker');
var config  = require('./config');

// --- CONFIGURE THIS ---
var API_BASE = 'https://YOUR-WORKER.YOUR-SUBDOMAIN.workers.dev';
// ----------------------

var POLL_INTERVAL_MS = 15000;

var conversation    = null;
var inflight        = false;
var activeJobId     = null;
var pollTimer       = null;
var currentProvider = 'perplexity';

function buildConversation(systemPrompt) {
  return [{ role: 'system', content: systemPrompt || '' }];
}

function resetConversation() {
  var cfg = config.load();
  conversation = buildConversation(cfg.systemPrompt);
}

function stopPolling() {
  if (pollTimer !== null) { clearInterval(pollTimer); pollTimer = null; }
}

function startPolling(jobId) {
  stopPolling();
  activeJobId = jobId;
  pollTimer = setInterval(function () {
    if (!activeJobId) { stopPolling(); return; }
    fetch(API_BASE + '/status/' + encodeURIComponent(activeJobId), { method: 'GET' })
      .then(function (r) { return r.json(); })
      .then(function (data) {
        if (!data || !data.status) return;
        if (data.status === 'ready') {
          stopPolling(); activeJobId = null; inflight = false;
          var reply = data.reply || '(no content)';
          if (conversation) conversation.push({ role: 'assistant', content: reply });
          chunker.sendChunked(reply, {
            onComplete: function () {},
            onError: function () { if (conversation) conversation.pop(); }
          });
        } else if (data.status === 'error') {
          stopPolling(); activeJobId = null; inflight = false;
          if (conversation) conversation.pop();
          chunker.sendErrorCode(7 /* ERR_SERVER_ERROR */);
        }
      })
      .catch(function () { /* network hiccup — keep polling */ });
  }, POLL_INTERVAL_MS);
}

function handleUserMessage(text) {
  if (inflight) { chunker.sendErrorCode(12 /* ERR_BUSY */); return; }
  if (!API_BASE || API_BASE.indexOf('YOUR-WORKER') !== -1) {
    chunker.sendErrorCode(5 /* ERR_BAD_API_KEY — worker not configured */); return;
  }
  if (!conversation) resetConversation();
  conversation.push({ role: 'user', content: text });
  inflight = true;

  Pebble.getTimelineToken(function (token) {
    fetch(API_BASE + '/chat', {
      method:  'POST',
      headers: { 'Content-Type': 'application/json' },
      body:    JSON.stringify({
        token:    token,
        prompt:   text,
        provider: currentProvider,
        history:  conversation.slice(0, -1)
      })
    })
    .then(function (r) { if (!r.ok) throw new Error('HTTP ' + r.status); return r.json(); })
    .then(function (data) {
      if (!data || !data.jobId) { inflight = false; conversation.pop(); chunker.sendErrorCode(7); return; }
      startPolling(data.jobId);
    })
    .catch(function () {
      inflight = false; conversation.pop(); chunker.sendErrorCode(4 /* ERR_SERVER_UNREACHABLE */);
    });
  }, function () {
    inflight = false; conversation.pop(); chunker.sendErrorCode(1 /* ERR_PHONE_DISCONNECTED */);
  });
}

function registerToken() {
  Pebble.getTimelineToken(function (token) {
    fetch(API_BASE + '/register', {
      method:  'POST',
      headers: { 'Content-Type': 'application/json' },
      body:    JSON.stringify({ token: token })
    }).catch(function () {});
  }, function () { console.log('PWAI: timeline token unavailable'); });
}

Pebble.addEventListener('ready', function () {
  console.log('PWAI pkjs ready');
  resetConversation();
  registerToken();
  var cfg = config.load();
  Pebble.sendAppMessage({ FontSize: config.fontSizeCode(cfg.fontSize) });
});

Pebble.addEventListener('appmessage', function (e) {
  var p = e.payload || {};
  if (typeof p.Provider === 'number') {
    currentProvider = p.Provider === 1 ? 'claude' : 'perplexity';
    console.log('PWAI: provider =>', currentProvider);
    return;
  }
  if (typeof p.UserMessage === 'string') {
    handleUserMessage(p.UserMessage);
  } else if (p.ResetConversation) {
    resetConversation();
  } else if (p.CancelInflight) {
    inflight = false;
    stopPolling(); activeJobId = null;
    if (conversation && conversation.length > 1) conversation.pop();
  }
  // ChunkAck handled inside chunker.sendChunked's own listener
});
