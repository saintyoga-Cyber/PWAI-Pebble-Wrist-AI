/*
 * PWAI — PebbleKit JS bridge
 * Runs on the phone inside the Pebble / Rebble app.
 *
 * ACTION REQUIRED: replace API_BASE with your deployed Cloudflare Worker URL.
 */

var API_BASE      = 'https://YOUR-WORKER.YOUR-SUBDOMAIN.workers.dev';
var CHUNK_SIZE    = 180;
var POLL_INTERVAL = 15000;

var Keys = {
  KEY_MSG_TYPE:    0,
  KEY_CHUNK_IDX:   1,
  KEY_CHUNK_TOTAL: 2,
  KEY_CHUNK_DATA:  3,
  KEY_JOB_ID:      4,
  KEY_STATUS:      5,
  KEY_PROVIDER:    6,
  KEY_COMMAND:     7,
  KEY_PROMPT:      8,
  KEY_TOKEN:       9
};

var pollTimer   = null;
var activeJobId = null;

function sendStatus(text) {
  try {
    var m = {};
    m[Keys.KEY_MSG_TYPE] = 2;
    m[Keys.KEY_STATUS]   = text.slice(0, 64);
    Pebble.sendAppMessage(m);
  } catch (e) {}
}

function sendChunks(text) {
  var total = Math.max(1, Math.ceil(text.length / CHUNK_SIZE));
  for (var i = 0; i < total; i++) {
    var m = {};
    m[Keys.KEY_MSG_TYPE]    = 0;
    m[Keys.KEY_CHUNK_IDX]   = i;
    m[Keys.KEY_CHUNK_TOTAL] = total;
    m[Keys.KEY_CHUNK_DATA]  = text.slice(i * CHUNK_SIZE, (i + 1) * CHUNK_SIZE);
    Pebble.sendAppMessage(m);
  }
}

function sendPing() {
  try {
    var m = {};
    m[Keys.KEY_MSG_TYPE] = 3;
    Pebble.sendAppMessage(m);
  } catch (e) {}
}

function api(path, method, body) {
  return fetch(API_BASE + path, {
    method:  method || 'GET',
    headers: { 'Content-Type': 'application/json' },
    body:    body ? JSON.stringify(body) : undefined
  }).then(function (r) {
    if (!r.ok) throw new Error('HTTP ' + r.status);
    return r.json();
  });
}

function stopPolling() {
  if (pollTimer !== null) { clearInterval(pollTimer); pollTimer = null; }
}

function startPolling() {
  stopPolling();
  pollTimer = setInterval(function () {
    if (!activeJobId) { stopPolling(); return; }
    api('/status/' + encodeURIComponent(activeJobId), 'GET')
      .then(function (data) {
        if (!data || !data.status) return;
        if (data.status === 'ready') {
          stopPolling();
          sendChunks(data.reply || '(no content)');
          sendPing();
          activeJobId = null;
        } else if (data.status === 'error') {
          stopPolling();
          sendStatus('AI error: ' + (data.error || 'unknown'));
          activeJobId = null;
        }
      })
      .catch(function () {});
  }, POLL_INTERVAL);
}

function registerToken() {
  Pebble.getTimelineToken(
    function (token) {
      api('/register', 'POST', { token: token }).catch(function () {});
    },
    function () { console.log('PWAI: timeline token unavailable'); }
  );
}

Pebble.addEventListener('ready', function () {
  registerToken();
  sendStatus('Bridge ready.');
});

Pebble.addEventListener('appmessage', function (e) {
  var payload  = e.payload || {};
  if (payload[Keys.KEY_COMMAND] !== 1) return;

  var prompt   = payload[Keys.KEY_PROMPT]   || '';
  var provider = payload[Keys.KEY_PROVIDER] === 1 ? 'claude' : 'perplexity';
  if (!prompt) { sendStatus('Empty prompt.'); return; }

  sendStatus('Sending to ' + provider + '...');

  Pebble.getTimelineToken(
    function (token) {
      api('/chat', 'POST', { token: token, provider: provider, prompt: prompt })
        .then(function (data) {
          if (!data || !data.jobId) { sendStatus('No jobId from server.'); return; }
          activeJobId = data.jobId;
          sendStatus('Queued. Waiting...');
          startPolling();
        })
        .catch(function (err) {
          sendStatus('Request failed.');
          console.log('PWAI /chat error:', err && err.message);
        });
    },
    function () { sendStatus('Token unavailable.'); }
  );
});
