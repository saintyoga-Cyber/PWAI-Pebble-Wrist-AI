// UTF-8-safe text chunker + ack-gated AppMessage sender.
// Must stay in sync with C-side CHUNK_SIZE / MAX_CHUNKS in src/c/message_keys.h.

var CHUNK_SIZE = 2048;
var MAX_CHUNKS = 16;

function stripHarmonyTokens(text) {
  if (typeof text !== 'string') return text;
  if (text.indexOf('<|') === -1) return text;
  var finalMatch = text.match(/<\|channel\|>final[^<]*<\|message\|>([\s\S]*?)(?:<\|end\|>|<\|return\|>|<\|channel\|>|$)/);
  if (finalMatch) return finalMatch[1].replace(/<\|[^|]*\|>/g, '').trim();
  var lastMessage = text.lastIndexOf('<|message|>');
  if (lastMessage !== -1) text = text.substring(lastMessage + '<|message|>'.length);
  return text.replace(/<\|[^|]*\|>/g, '').trim();
}

function unwrapJsonContent(text) {
  if (typeof text !== 'string') return text;
  var trimmed = text.trim();
  if (trimmed.length < 2 || trimmed[0] !== '{' || trimmed[trimmed.length - 1] !== '}') return text;
  try {
    var obj = JSON.parse(trimmed);
    if (obj && typeof obj === 'object') {
      if (typeof obj.content  === 'string') return obj.content;
      if (typeof obj.response === 'string') return obj.response;
    }
  } catch (e) {}
  return text;
}

function stripMarkdown(text) {
  if (typeof text !== 'string') return text;
  return text
    .replace(/```[a-zA-Z0-9_+-]*\n?([\s\S]*?)```/g, '$1')
    .replace(/`([^`\n]+)`/g, '$1')
    .replace(/\*\*([^*\n]+)\*\*/g, '$1')
    .replace(/__([^_\n]+)__/g, '$1')
    .replace(/(^|[^*])\*([^*\n]+)\*(?!\*)/g, '$1$2')
    .replace(/(^|[^_\w])_([^_\n]+)_(?!_)/g, '$1$2')
    .replace(/\[([^\]]+)\]\([^)]*\)/g, '$1')
    .replace(/^#{1,6}[ \t]+/gm, '')
    .replace(/^>[ \t]?/gm, '')
    .replace(/^[ \t]*[-*+][ \t]+/gm, '')
    .replace(/^[ \t]*\d+\.[ \t]+/gm, '');
}

function sanitizeForPebble(text) {
  if (typeof text !== 'string') return text;
  text = stripHarmonyTokens(text);
  text = unwrapJsonContent(text);
  text = stripMarkdown(text);
  return text
    .replace(/[\u2010-\u2015\u2212]/g, '-')
    .replace(/[\u2018\u2019\u201A\u201B]/g, "'")
    .replace(/[\u201C-\u201F]/g, '"')
    .replace(/\u2026/g, '...')
    .replace(/[\u2022\u2023\u25CF\u25E6]/g, '*')
    .replace(/[^\x09\x0A\x20-\x7E]/g, '')
    .trim();
}

function splitUtf8(text, maxBytes) {
  var chunks = [];
  var bytes  = unescape(encodeURIComponent(text));
  var i = 0;
  while (i < bytes.length) {
    var end = Math.min(i + maxBytes, bytes.length);
    while (end > i && end < bytes.length) {
      var code = bytes.charCodeAt(end);
      if ((code & 0xC0) !== 0x80) break;
      end--;
    }
    chunks.push(decodeURIComponent(escape(bytes.substring(i, end))));
    i = end;
  }
  return chunks;
}

function sendChunked(text, callbacks) {
  var onComplete = (callbacks && callbacks.onComplete) || function () {};
  var onError    = (callbacks && callbacks.onError)    || function () {};
  var ERR_RESPONSE_TOO_LARGE = 9;
  var ERR_TRANSPORT_FAILED   = 11;

  var chunks = splitUtf8(sanitizeForPebble(text), CHUNK_SIZE);
  if (chunks.length === 0) chunks = [''];
  if (chunks.length > MAX_CHUNKS) {
    sendErrorCode(ERR_RESPONSE_TOO_LARGE);
    onError(ERR_RESPONSE_TOO_LARGE);
    return;
  }

  var idx = 0, ackListener = null, done = false, retries = 0;
  var MAX_RETRIES = 3;
  var BACKOFF_MS  = [250, 500, 1000];

  function cleanup() {
    done = true;
    if (ackListener) {
      try { Pebble.removeEventListener('appmessage', ackListener); } catch (e) {}
      ackListener = null;
    }
  }

  function sendChunk(i) {
    var payload = {
      ResponseChunkIndex: i,
      ResponseChunkTotal: chunks.length,
      ResponseChunkText:  chunks[i]
    };
    Pebble.sendAppMessage(payload, function () {}, function () {
      if (retries < MAX_RETRIES) {
        setTimeout(function () { sendChunk(i); }, BACKOFF_MS[retries]);
        retries++;
      } else {
        cleanup();
        sendErrorCode(ERR_TRANSPORT_FAILED);
        onError(ERR_TRANSPORT_FAILED);
      }
    });
  }

  ackListener = function (e) {
    if (done) return;
    var ackIdx = e.payload && e.payload.ChunkAck;
    if (typeof ackIdx !== 'number' || ackIdx !== idx) return;
    retries = 0; idx++;
    if (idx >= chunks.length) { done = true; onComplete(); cleanup(); }
    else sendChunk(idx);
  };
  Pebble.addEventListener('appmessage', ackListener);
  sendChunk(0);
}

function sendErrorCode(code) {
  Pebble.sendAppMessage({ ErrorCode: code });
}

module.exports = {
  CHUNK_SIZE: CHUNK_SIZE, MAX_CHUNKS: MAX_CHUNKS,
  splitUtf8: splitUtf8, sanitizeForPebble: sanitizeForPebble,
  stripHarmonyTokens: stripHarmonyTokens, unwrapJsonContent: unwrapJsonContent,
  stripMarkdown: stripMarkdown, sendChunked: sendChunked, sendErrorCode: sendErrorCode
};
