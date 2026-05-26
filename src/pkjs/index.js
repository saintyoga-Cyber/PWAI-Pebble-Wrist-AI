/*
 * PWAI v0.3 — PebbleKit JS bridge
 *
 * What changed from v0.2:
 *   - Intent classifier from Brain Dump: routes task-like utterances to
 *     sendToReminders() and note-like utterances to sendToNotes() instead
 *     of always sending to the AI chat endpoint.
 *   - sendToReminders(): primary path is Claude tool_use via /reminder;
 *     falls back to iOS Shortcut URL scheme on tool_failed or error.
 *   - sendToNotes(): always uses iOS Shortcut URL scheme (no API available).
 *   - Date/time extractor (extractDueDate, extractTime) from Brain Dump.
 *   - All existing chat/chunker/polling logic is unchanged.
 *
 * iOS Shortcuts required (create once in the Shortcuts app):
 *   • "PebbleReminder" — accepts Text input, creates Reminder from it.
 *     Optional: also accept a "date" param via URL scheme for due date.
 *   • "PebbleNote"     — accepts Text input, creates Note from it.
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

// ============================================================================
// CONVERSATION HELPERS  (unchanged from v0.2)
// ============================================================================

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

// ============================================================================
// POLLING  — extended to handle 'tool_failed' status from /reminder jobs
// ============================================================================

// pendingShortcutArgs is set before a reminder job starts so that if the
// job resolves as tool_failed we can immediately trigger the Shortcut.
var pendingShortcutArgs = null;

function startPolling(jobId, shortcutFallbackArgs) {
  stopPolling();
  activeJobId          = jobId;
  pendingShortcutArgs  = shortcutFallbackArgs || null;

  pollTimer = setInterval(function () {
    if (!activeJobId) { stopPolling(); return; }
    fetch(API_BASE + '/status/' + encodeURIComponent(activeJobId), { method: 'GET' })
      .then(function (r) { return r.json(); })
      .then(function (data) {
        if (!data || !data.status) return;

        if (data.status === 'ready') {
          stopPolling(); activeJobId = null; inflight = false;
          pendingShortcutArgs = null;
          var reply = data.reply || '(no content)';
          if (conversation) conversation.push({ role: 'assistant', content: reply });
          chunker.sendChunked(reply, {
            onComplete: function () {},
            onError: function () { if (conversation) conversation.pop(); }
          });

        } else if (data.status === 'tool_failed') {
          // Claude could not create the Reminder via tool_use.
          // Trigger the iOS Shortcut fallback if we have the args.
          stopPolling(); activeJobId = null; inflight = false;
          var args = pendingShortcutArgs;
          pendingShortcutArgs = null;
          if (args) {
            console.log('PWAI: reminder tool_failed — falling back to Shortcut');
            triggerReminderShortcut(args.text, args.dueDate, args.dueTime);
          } else {
            chunker.sendErrorCode(7 /* ERR_SERVER_ERROR */);
          }

        } else if (data.status === 'error') {
          stopPolling(); activeJobId = null; inflight = false;
          pendingShortcutArgs = null;
          if (conversation) conversation.pop();
          chunker.sendErrorCode(7 /* ERR_SERVER_ERROR */);
        }
      })
      .catch(function () { /* network hiccup — keep polling */ });
  }, POLL_INTERVAL_MS);
}

// ============================================================================
// DATE / TIME EXTRACTION  (ported from Brain Dump)
// ============================================================================

var _NUM_WORDS = {
  'one':1,'two':2,'three':3,'four':4,'five':5,'six':6,
  'seven':7,'eight':8,'nine':9,'ten':10,'eleven':11,'twelve':12,
  'eins':1,'zwei':2,'drei':3,'vier':4,'fünf':5,'funf':5,
  'sechs':6,'sieben':7,'acht':8,'neun':9,'zehn':10,'elf':11,'zwölf':12,'zwolf':12,
  'une':1,'deux':2,'trois':3,'quatre':4,'cinq':5,
  'sept':7,'huit':8,'neuf':9,'dix':10,'onze':11,'douze':12,
  'dos':2,'cuatro':4,'cinco':5,'seis':6,'siete':7,'ocho':8,
  'nueve':9,'diez':10,'once':11,'doce':12
};
var _NUM_PAT = [
  'eleven','twelve','seven','eight','three','four','five','nine','six','ten','two','one',
  'sieben','sechs','neun','zehn','vier','acht','elf','fünf','funf','zwei','drei',
  'eins','zwölf','zwolf',
  'quatre','trois','onze','douze','deux','cinq','sept','huit','neuf','dix','une',
  'cuatro','cinco','siete','ocho','nueve','diez','seis','once','doce','dos'
].join('|');

function disambiguate(h, min, nowHour) {
  if (h === 0 || h > 12) return { h: h, m: min };
  var asAM = (h === 12) ? 0  : h;
  var asPM = (h === 12) ? 12 : h + 12;
  if (nowHour >= asPM) return { h: asAM, m: min };
  if (nowHour >= asAM) return { h: asPM, m: min };
  return ((asPM - nowHour) <= (asAM - nowHour))
    ? { h: asPM, m: min } : { h: asAM, m: min };
}

function _normAP(s) { return s.replace(/[\s.]/g, ''); }

function _inlineAP(t) {
  if (t.indexOf('du matin')  >= 0 || t.indexOf('am morgen')  >= 0 ||
      t.indexOf('de manana') >= 0 || t.indexOf('de mañana') >= 0 ||
      /\b(matin|morgens|morning|mañana|manana|madrugada)\b/.test(t))
    return 'am';
  if (t.indexOf('du soir')     >= 0 || t.indexOf('de la tarde') >= 0 ||
      t.indexOf('de la noche') >= 0 || t.indexOf('am abend')    >= 0 ||
      /\b(soir|abend|abends|evening|noche|tarde|nachmittag)\b/.test(t))
    return 'pm';
  return null;
}

function _withAP(h, min, t, nowHour) {
  if (h >= 13) return { h: h, m: min };
  var ap = _inlineAP(t);
  if (ap === 'am') return { h: (h === 12 ? 0  : h),    m: min };
  if (ap === 'pm') return { h: (h <  12 ? h + 12 : h), m: min };
  return disambiguate(h, min, nowHour);
}

function extractTime(text, nowHour) {
  var t = text.toLowerCase();
  if (nowHour === undefined) nowHour = new Date().getHours();
  var m;
  m = t.match(/\b(\d{1,2})(?::(\d{2}))?\s*([ap]\.?\s?m\.?)(?!\w)/);
  if (m) {
    var h = parseInt(m[1], 10), min = m[2] ? parseInt(m[2], 10) : 0;
    var ap = _normAP(m[3]);
    if (ap === 'pm' && h < 12) h += 12;
    if (ap === 'am' && h === 12) h = 0;
    return { h: h, m: min };
  }
  m = t.match(new RegExp('\\b(' + _NUM_PAT + ')\\s+([ap]\\.?\\s?m\\.?)(?!\\w)'));
  if (m) {
    var h2 = _NUM_WORDS[m[1]], ap2 = _normAP(m[2]);
    if (ap2 === 'pm' && h2 < 12) h2 += 12;
    if (ap2 === 'am' && h2 === 12) h2 = 0;
    return { h: h2, m: 0 };
  }
  m = t.match(/\b(\d{1,2})h\s?(\d{2})?\b/);
  if (m) return _withAP(parseInt(m[1], 10), m[2] ? parseInt(m[2], 10) : 0, t, nowHour);
  m = t.match(/\b(\d{1,2})\s+(?:heures?|uhr)\b(?:\s+(\d{1,2}))?\b/);
  if (m) return _withAP(parseInt(m[1], 10), m[2] ? parseInt(m[2], 10) : 0, t, nowHour);
  m = t.match(new RegExp('\\b(' + _NUM_PAT + ')\\s+(?:heures?|uhr)\\b'));
  if (m) return _withAP(_NUM_WORDS[m[1]], 0, t, nowHour);
  m = t.match(/\blas\s+(\d{1,2})\b/);
  if (m) {
    var extra = t.indexOf('y media') >= 0 ? 30 : t.indexOf('y cuarto') >= 0 ? 15 : 0;
    return _withAP(parseInt(m[1], 10), extra, t, nowHour);
  }
  m = t.match(new RegExp('\\blas\\s+(' + _NUM_PAT + ')\\b'));
  if (m) {
    var extra2 = t.indexOf('y media') >= 0 ? 30 : t.indexOf('y cuarto') >= 0 ? 15 : 0;
    return _withAP(_NUM_WORDS[m[1]], extra2, t, nowHour);
  }
  m = t.match(/\b([01]?\d|2[0-3]):([0-5]\d)\b/);
  if (m) {
    var h24 = parseInt(m[1], 10), m24 = parseInt(m[2], 10);
    if (h24 >= 13 || m[1].length === 2) return { h: h24, m: m24 };
    return disambiguate(h24, m24, nowHour);
  }
  m = t.match(/\b(\d{1,2})\s*o'?clock\b/);
  if (m) return disambiguate(parseInt(m[1], 10), 0, nowHour);
  m = t.match(new RegExp("\\b(" + _NUM_PAT + ")\\s*o'?clock\\b"));
  if (m) return disambiguate(_NUM_WORDS[m[1]], 0, nowHour);
  m = t.match(/\bat\s+(\d{1,2})\b(?!\s*[:\d])/);
  if (m) return disambiguate(parseInt(m[1], 10), 0, nowHour);
  m = t.match(new RegExp('\\bat\\s+(' + _NUM_PAT + ')\\b'));
  if (m) return disambiguate(_NUM_WORDS[m[1]], 0, nowHour);
  function has() {
    for (var i = 0; i < arguments.length; i++)
      if (t.indexOf(arguments[i]) >= 0) return true;
    return false;
  }
  if (has('at breakfast','for breakfast','beim frühstück','zum frühstück',
          'au petit-déjeuner','en el desayuno')) return { h: 8, m: 0 };
  if (has('this morning','in the morning','heute morgen','am morgen',
          'ce matin','esta mañana','por la mañana')) return { h: 9, m: 0 };
  if (has('this afternoon','in the afternoon','heute nachmittag',
          'cet après-midi','esta tarde')) return { h: 14, m: 0 };
  if (has('this evening','in the evening','heute abend',
          'ce soir','esta noche temprano')) return { h: 18, m: 0 };
  if (has('noon','lunchtime','at lunch','mittag','midi','mediodía')) return { h: 12, m: 0 };
  if (has('midnight','mitternacht','minuit','medianoche')) return { h: 0, m: 0 };
  if (has('tonight','at night','heute nacht','cette nuit','esta noche')) return { h: 20, m: 0 };
  if (has('at dinner','beim abendessen','au dîner','en la cena')) return { h: 19, m: 0 };
  return null;
}

function extractDueDate(text) {
  var t = text.toLowerCase();
  var now = new Date(), d = null;
  if (t.indexOf('tomorrow') >= 0) {
    d = new Date(now); d.setDate(d.getDate() + 1);
  } else if (t.indexOf('today') >= 0 || t.indexOf('tonight') >= 0) {
    d = new Date(now);
  } else {
    var days = ['sunday','monday','tuesday','wednesday','thursday','friday','saturday'];
    for (var i = 0; i < days.length; i++) {
      if (t.indexOf(days[i]) >= 0) {
        d = new Date(now);
        var diff = (i - d.getDay() + 7) % 7 || 7;
        d.setDate(d.getDate() + diff);
        break;
      }
    }
  }
  if (!d && extractTime(text)) d = new Date(now);
  return d ? d.toISOString().split('T')[0] : null;
}

// ============================================================================
// INTENT CLASSIFIER  (ported from Brain Dump)
// ============================================================================

var TASK_SIGNALS = [
  'remind me','add task','add a task','to do','todo',
  "don't forget",'remember to','need to','have to',
  'buy ','call ','email ','schedule','appointment','meeting','pick up','book ',
  'erinner','aufgabe','erinnerung','kaufen','anrufen','nicht vergessen',
  'muss ','termin','treffen','buchen','schick',
  'rappelle','tâche','tache',"n'oublie pas",'rappel',
  'acheter','appeler','rendez-vous','réunion','reunion','réserver',
  'recuérdame','recuerdame','tarea','no olvides','no te olvides',
  'comprar','llamar','cita ','reunión','reservar'
];

var NOTE_SIGNALS = [
  'note','write down','save this','document','jot',
  'record that','add to my notes','log this','idea','memo','keep in mind',
  'notiz','aufschreiben','speichern','idee','festhalten',
  'noter','sauvegarder','idée','conserver',
  'nota','anotar','guardar','apuntar'
];

function isTaskLike(text) {
  var t = text.toLowerCase();
  for (var i = 0; i < TASK_SIGNALS.length; i++)
    if (t.indexOf(TASK_SIGNALS[i]) >= 0) return true;
  return false;
}

function isNoteLike(text) {
  var t = text.toLowerCase();
  for (var i = 0; i < NOTE_SIGNALS.length; i++)
    if (t.indexOf(NOTE_SIGNALS[i]) >= 0) return true;
  return false;
}

/**
 * Classify the utterance into one of: 'reminder', 'note', 'ai'.
 * 'ai' means route to the existing chat/LLM path.
 */
function classifyIntent(text) {
  var t = text.toLowerCase();

  var scores = { ai: 0, reminder: 0, note: 0 };

  var qWords = ['what ','how ','why ','who ','where ','when ','is ','are ','can ',
                'could ','should ','will ','would ',"what's","how's",
                'was ','wie ','warum ','wer ','wo ','wann ','ist ','sind ','kann ',
                'quoi ','comment ','pourquoi ','qui ','où ','quand ',
                'qué ','cómo ','por qué ','quién ','dónde '];
  var aiPhrases = ['explain','tell me','help me','define ','look up','search for',
                   'give me','translate','erkläer','sag mir','explique','dis-moi',
                   'traduis','explica','dime','traduce'];
  if (t.charAt(t.length - 1) === '?') scores.ai += 3;
  if (t.indexOf('ask ') === 0) scores.ai += 2;
  qWords.forEach(function(w)    { if (t.indexOf(w) === 0) scores.ai += 2; });
  aiPhrases.forEach(function(p) { if (t.indexOf(p) >= 0)  scores.ai += 1; });

  TASK_SIGNALS.forEach(function(p) { if (t.indexOf(p) >= 0) scores.reminder += 2; });
  var timeWords = ['tomorrow','tonight','today','monday','tuesday','wednesday',
    'thursday','friday','saturday','sunday','next week','by ','noon','midnight',
    "o'clock",' am',' pm','morgen ','heute ','montag','demain','mañana'];
  timeWords.forEach(function(w) { if (t.indexOf(w) >= 0) scores.reminder += 1; });

  NOTE_SIGNALS.forEach(function(p) { if (t.indexOf(p) >= 0) scores.note += 2; });

  if (t.indexOf('reminder') >= 0 || t.indexOf('remind me') >= 0) scores.reminder += 4;
  if (t.indexOf('note') >= 0 || t.indexOf('jot') >= 0)           scores.note     += 4;

  var best = 'ai', bestScore = scores.ai;
  if (scores.reminder > bestScore) { bestScore = scores.reminder; best = 'reminder'; }
  if (scores.note     > bestScore) {                               best = 'note';     }

  return best;
}

// ============================================================================
// iOS SHORTCUT BRIDGE
// ============================================================================

function pad(n) { return n < 10 ? '0' + n : '' + n; }

/**
 * triggerReminderShortcut: opens the iOS "PebbleReminder" Shortcut.
 * Shortcut setup:
 *   Name: PebbleReminder
 *   - Receive Text from Shortcut Input
 *   - Add New Reminder with [Text Input] due [date parsed from text if present]
 */
function triggerReminderShortcut(text, dueDate, dueTime) {
  var input = text;
  if (dueDate) {
    input += ' (due ' + dueDate;
    if (dueTime) input += ' at ' + pad(dueTime.h) + ':' + pad(dueTime.m);
    input += ')';
  }
  var url = 'shortcuts://run-shortcut?name=' +
            encodeURIComponent('PebbleReminder') +
            '&input=' + encodeURIComponent(input);
  console.log('PWAI: Shortcut URL => ' + url);
  Pebble.openURL(url);
  chunker.sendChunked('Reminder queued via Shortcuts.', {});
}

/**
 * triggerNoteShortcut: opens the iOS "PebbleNote" Shortcut.
 * Shortcut setup:
 *   Name: PebbleNote
 *   - Receive Text from Shortcut Input
 *   - Create Note with name [timestamp] and body [Text Input]
 */
function triggerNoteShortcut(text) {
  var url = 'shortcuts://run-shortcut?name=' +
            encodeURIComponent('PebbleNote') +
            '&input=' + encodeURIComponent(text);
  console.log('PWAI: Note Shortcut URL => ' + url);
  Pebble.openURL(url);
  chunker.sendChunked('Note saved via Shortcuts.', {});
}

// ============================================================================
// DESTINATION HANDLERS
// ============================================================================

/**
 * sendToReminders:
 *   - Claude selected  → POST /reminder → poll for ready/tool_failed
 *                        tool_failed    → triggerReminderShortcut (fallback)
 *   - Perplexity       → triggerReminderShortcut directly
 */
function sendToReminders(text) {
  var dueDate = extractDueDate(text);
  var dueTime = dueDate ? extractTime(text) : null;

  if (currentProvider !== 'claude') {
    triggerReminderShortcut(text, dueDate, dueTime);
    return;
  }

  if (!API_BASE || API_BASE.indexOf('YOUR-WORKER') !== -1) {
    triggerReminderShortcut(text, dueDate, dueTime);
    return;
  }

  if (inflight) { chunker.sendErrorCode(12 /* ERR_BUSY */); return; }
  inflight = true;

  var shortcutArgs = { text: text, dueDate: dueDate, dueTime: dueTime };

  Pebble.getTimelineToken(function (token) {
    var body = { token: token, text: text };
    if (dueDate) body.dueDate = dueDate;
    if (dueTime) body.dueTime = pad(dueTime.h) + ':' + pad(dueTime.m);

    fetch(API_BASE + '/reminder', {
      method:  'POST',
      headers: { 'Content-Type': 'application/json' },
      body:    JSON.stringify(body)
    })
    .then(function (r) { if (!r.ok) throw new Error('HTTP ' + r.status); return r.json(); })
    .then(function (data) {
      if (!data || !data.jobId) {
        inflight = false;
        triggerReminderShortcut(text, dueDate, dueTime);
        return;
      }
      startPolling(data.jobId, shortcutArgs);
    })
    .catch(function () {
      inflight = false;
      triggerReminderShortcut(text, dueDate, dueTime);
    });
  }, function () {
    inflight = false;
    triggerReminderShortcut(text, dueDate, dueTime);
  });
}

/**
 * sendToNotes: always uses the iOS Shortcut (no native API exists).
 */
function sendToNotes(text) {
  triggerNoteShortcut(text);
}

// ============================================================================
// MAIN MESSAGE HANDLER
// ============================================================================

function handleUserMessage(text) {
  var intent = classifyIntent(text);
  console.log('PWAI: intent => ' + intent + ' | provider => ' + currentProvider);

  if (intent === 'reminder') {
    sendToReminders(text);
    return;
  }

  if (intent === 'note') {
    sendToNotes(text);
    return;
  }

  // Default: AI chat path (unchanged from v0.2)
  if (inflight) { chunker.sendErrorCode(12 /* ERR_BUSY */); return; }
  if (!API_BASE || API_BASE.indexOf('YOUR-WORKER') !== -1) {
    chunker.sendErrorCode(5 /* ERR_BAD_API_KEY */); return;
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
      startPolling(data.jobId, null);
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

// ============================================================================
// PEBBLE EVENTS  (unchanged from v0.2)
// ============================================================================

Pebble.addEventListener('ready', function () {
  console.log('PWAI pkjs ready (v0.3)');
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
    pendingShortcutArgs = null;
    if (conversation && conversation.length > 1) conversation.pop();
  }
});
