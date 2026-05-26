/**
 * PWAI — Cloudflare Worker  (Critical-1: WebSocket push via Durable Object)
 *
 * What changed vs previous version:
 *   - JobSocket Durable Object: holds one WebSocket connection per jobId.
 *     When a job completes, processJob / processReminderJob calls
 *     notifySocket() which wakes the Durable Object and pushes the result
 *     over the live WebSocket. The phone receives the reply instantly
 *     instead of waiting up to 15 s for a poll cycle.
 *   - GET /ws?jobId=<id>  — WebSocket upgrade endpoint. The phone calls
 *     this immediately after POST /chat returns jobId. Times out after 90 s
 *     if no reply arrives (network drop safety net).
 *   - All existing HTTP routes are unchanged so old polling clients still work
 *     during the transition period.
 *
 * Bindings required in wrangler.toml (see wrangler.toml):
 *   DB            — D1 database
 *   JOB_SOCKET    — Durable Object binding (class JobSocket)
 *   PERPLEXITY_API_KEY, ANTHROPIC_API_KEY  — secrets
 */

// ── Durable Object: one instance per jobId ────────────────────────────────────
export class JobSocket {
  constructor(state) {
    this.state  = state;
    this.socket = null;          // the live WebSocket, if any
    this.result = null;          // cached result in case job finishes before WS connects
  }

  async fetch(request) {
    const url    = new URL(request.url);
    const action = url.searchParams.get('action');

    // ── WebSocket upgrade from the phone ──────────────────────────────────
    if (action === 'connect') {
      if (request.headers.get('Upgrade') !== 'websocket')
        return new Response('Expected WebSocket upgrade', { status: 426 });

      const pair = new WebSocketPair();
      const [client, server] = Object.values(pair);
      this.state.acceptWebSocket(server);
      this.socket = server;

      // If the job already finished before the WS connected, push immediately.
      if (this.result) {
        server.send(JSON.stringify(this.result));
        server.close(1000, 'done');
        this.socket = null;
      } else {
        // 90-second safety timeout — phone must handle close without payload.
        setTimeout(() => {
          if (this.socket) {
            try { this.socket.send(JSON.stringify({ status: 'timeout' })); } catch (_) {}
            try { this.socket.close(1000, 'timeout'); } catch (_) {}
            this.socket = null;
          }
        }, 90_000);
      }

      return new Response(null, { status: 101, webSocket: client });
    }

    // ── Push from the Worker when a job completes ─────────────────────────
    if (action === 'notify') {
      const result = await request.json().catch(() => null);
      if (!result) return new Response('Bad payload', { status: 400 });
      this.result = result;
      if (this.socket) {
        try {
          this.socket.send(JSON.stringify(result));
          this.socket.close(1000, 'done');
        } catch (_) { /* socket may have already closed */ }
        this.socket = null;
      }
      return new Response('ok');
    }

    return new Response('Unknown action', { status: 400 });
  }

  // Required by Cloudflare when using state.acceptWebSocket()
  webSocketMessage() {}
  webSocketClose()   { this.socket = null; }
  webSocketError()   { this.socket = null; }
}

// ── Helper: notify a JobSocket Durable Object ─────────────────────────────────
async function notifySocket(env, jobId, result) {
  try {
    const id  = env.JOB_SOCKET.idFromName(jobId);
    const obj = env.JOB_SOCKET.get(id);
    await obj.fetch(
      new Request(`https://internal/ws?action=notify`, {
        method:  'POST',
        headers: { 'Content-Type': 'application/json' },
        body:    JSON.stringify(result)
      })
    );
  } catch (_) { /* non-fatal — HTTP polling still works as fallback */ }
}

// ── Main Worker ───────────────────────────────────────────────────────────────
export default {
  async fetch(request, env, ctx) {
    const url    = new URL(request.url);
    const path   = url.pathname;
    const method = request.method;

    // ── WebSocket upgrade: GET /ws?jobId=<id> ─────────────────────────────
    if (method === 'GET' && path === '/ws') {
      const jobId = url.searchParams.get('jobId');
      if (!jobId) return jsonRes({ error: 'jobId required' }, 400);
      if (request.headers.get('Upgrade') !== 'websocket')
        return jsonRes({ error: 'WebSocket upgrade required' }, 426);

      const id  = env.JOB_SOCKET.idFromName(jobId);
      const obj = env.JOB_SOCKET.get(id);
      // Forward the original WS upgrade request to the Durable Object.
      return obj.fetch(
        new Request(`https://internal/ws?action=connect`, {
          method:  'GET',
          headers: request.headers
        })
      );
    }

    // ── POST /register ────────────────────────────────────────────────────
    if (method === 'POST' && path === '/register') {
      const body = await request.json().catch(() => ({}));
      if (!body.token) return jsonRes({ error: 'token required' }, 400);
      await env.DB.prepare(
        `INSERT INTO users (token)
         VALUES (?)
         ON CONFLICT(token) DO UPDATE SET updated_at = CURRENT_TIMESTAMP`
      ).bind(body.token).run();
      return jsonRes({ ok: true });
    }

    // ── POST /chat ────────────────────────────────────────────────────────
    if (method === 'POST' && path === '/chat') {
      const body = await request.json().catch(() => ({}));
      const { token, prompt, provider } = body;
      if (!token || !prompt || !provider)
        return jsonRes({ error: 'token, prompt and provider required' }, 400);
      const jobId = crypto.randomUUID();
      await env.DB.prepare(
        `INSERT INTO jobs (id, token, provider, prompt, status) VALUES (?, ?, ?, ?, 'pending')`
      ).bind(jobId, token, provider, prompt).run();
      await env.DB.prepare(
        `INSERT INTO conversations (token, role, content, provider) VALUES (?, 'user', ?, ?)`
      ).bind(token, prompt, provider).run();
      // processJob calls notifySocket when done — no polling needed.
      ctx.waitUntil(processJob(env, jobId, token, provider, prompt));
      return jsonRes({ jobId, status: 'pending' });
    }

    // ── GET /status/:jobId  (HTTP polling fallback — unchanged) ───────────
    if (method === 'GET' && path.startsWith('/status/')) {
      const jobId = decodeURIComponent(path.split('/status/')[1] || '');
      if (!jobId) return jsonRes({ error: 'jobId required' }, 400);
      const row = await env.DB.prepare(
        `SELECT id, status, reply, error FROM jobs WHERE id = ?`
      ).bind(jobId).first();
      if (!row) return jsonRes({ error: 'job not found' }, 404);
      return jsonRes(row);
    }

    // ── POST /pin ─────────────────────────────────────────────────────────
    if (method === 'POST' && path === '/pin') {
      const body = await request.json().catch(() => ({}));
      const { token, title, bodyText, pinTime } = body;
      if (!token || !title || !pinTime)
        return jsonRes({ error: 'token, title and pinTime required' }, 400);
      const result = await pushPin(token, title, bodyText || '', pinTime);
      if (result.error) return jsonRes({ error: result.error }, 502);
      await env.DB.prepare(
        `INSERT OR REPLACE INTO pins (pin_id, token, title, body, pin_time) VALUES (?, ?, ?, ?, ?)`
      ).bind(result.pinId, token, title, bodyText || '', pinTime).run();
      return jsonRes({ ok: true, pinId: result.pinId });
    }

    // ── POST /reminder ────────────────────────────────────────────────────
    if (method === 'POST' && path === '/reminder') {
      const body = await request.json().catch(() => ({}));
      const { token, text, dueDate, dueTime } = body;
      if (!token || !text)
        return jsonRes({ error: 'token and text required' }, 400);
      const jobId = crypto.randomUUID();
      await env.DB.prepare(
        `INSERT INTO jobs (id, token, provider, prompt, status) VALUES (?, ?, 'claude', ?, 'pending')`
      ).bind(jobId, token, text).run();
      ctx.waitUntil(processReminderJob(env, jobId, token, text, dueDate || null, dueTime || null));
      return jsonRes({ jobId, status: 'pending' });
    }

    return jsonRes({ error: 'not found' }, 404);
  }
};

// ── Chat job processor ────────────────────────────────────────────────────────

async function processJob(env, jobId, token, provider, prompt) {
  try {
    const hist = await env.DB.prepare(
      `SELECT role, content FROM conversations
       WHERE token = ? AND provider = ?
       ORDER BY id DESC LIMIT 10`
    ).bind(token, provider).all();
    const history = (hist.results || []).reverse();
    const reply = provider === 'claude'
      ? await callClaude(env, history, prompt)
      : await callPerplexity(env, history, prompt);
    await env.DB.prepare(
      `UPDATE jobs SET status = 'ready', reply = ?, updated_at = CURRENT_TIMESTAMP WHERE id = ?`
    ).bind(reply, jobId).run();
    await env.DB.prepare(
      `INSERT INTO conversations (token, role, content, provider) VALUES (?, 'assistant', ?, ?)`
    ).bind(token, reply, provider).run();
    // Push result over WebSocket — phone gets it instantly.
    await notifySocket(env, jobId, { status: 'ready', reply });
  } catch (err) {
    const msg = err && err.message ? err.message : String(err);
    await env.DB.prepare(
      `UPDATE jobs SET status = 'error', error = ?, updated_at = CURRENT_TIMESTAMP WHERE id = ?`
    ).bind(msg, jobId).run();
    await notifySocket(env, jobId, { status: 'error', error: msg });
  }
}

// ── Reminder job processor ────────────────────────────────────────────────────

const REMINDER_TOOL = {
  name: 'create_reminder',
  description: 'Create a reminder in the user\'s iOS Reminders app. Call this with the reminder text and optional due date/time.',
  input_schema: {
    type: 'object',
    properties: {
      title:    { type: 'string', description: 'The reminder title / task text.' },
      due_date: { type: 'string', description: 'ISO 8601 date string (YYYY-MM-DD). Omit if not specified.' },
      due_time: { type: 'string', description: 'HH:MM (24-hour) due time. Omit if not specified.' },
      notes:    { type: 'string', description: 'Optional extra notes.' }
    },
    required: ['title']
  }
};

async function processReminderJob(env, jobId, token, text, dueDate, dueTime) {
  try {
    const userContent = dueDate
      ? `Create a reminder: "${text}" — due ${dueDate}${dueTime ? ' at ' + dueTime : ''}.`
      : `Create a reminder: "${text}"`;

    const res = await fetch('https://api.anthropic.com/v1/messages', {
      method: 'POST',
      headers: {
        'x-api-key':         env.ANTHROPIC_API_KEY,
        'anthropic-version': '2023-06-01',
        'Content-Type':      'application/json'
      },
      body: JSON.stringify({
        model:       'claude-sonnet-4-20250514',
        max_tokens:  256,
        tools:       [REMINDER_TOOL],
        tool_choice: { type: 'required', name: 'create_reminder' },
        messages:    [{ role: 'user', content: userContent }]
      })
    });

    const data = await res.json();

    if (!res.ok) {
      const msg = data.error?.message || 'Claude API error ' + res.status;
      await env.DB.prepare(
        `UPDATE jobs SET status = 'tool_failed', error = ?, updated_at = CURRENT_TIMESTAMP WHERE id = ?`
      ).bind(msg, jobId).run();
      await notifySocket(env, jobId, { status: 'tool_failed', error: msg });
      return;
    }

    const toolBlock = (data.content || []).find(b => b.type === 'tool_use' && b.name === 'create_reminder');

    if (!toolBlock) {
      const fallbackMsg = (data.content || []).map(b => b.text || '').join('').trim()
        || 'Claude did not call create_reminder';
      await env.DB.prepare(
        `UPDATE jobs SET status = 'tool_failed', error = ?, updated_at = CURRENT_TIMESTAMP WHERE id = ?`
      ).bind(fallbackMsg, jobId).run();
      await notifySocket(env, jobId, { status: 'tool_failed', error: fallbackMsg });
      return;
    }

    const input = toolBlock.input || {};
    let confirmation = 'Reminder set: ' + (input.title || text);
    if (input.due_date) {
      confirmation += ' on ' + input.due_date;
      if (input.due_time) confirmation += ' at ' + input.due_time;
    }

    await env.DB.prepare(
      `UPDATE jobs SET status = 'ready', reply = ?, updated_at = CURRENT_TIMESTAMP WHERE id = ?`
    ).bind(confirmation, jobId).run();
    await notifySocket(env, jobId, { status: 'ready', reply: confirmation });

  } catch (err) {
    const msg = err && err.message ? err.message : String(err);
    await env.DB.prepare(
      `UPDATE jobs SET status = 'tool_failed', error = ?, updated_at = CURRENT_TIMESTAMP WHERE id = ?`
    ).bind(msg, jobId).run();
    await notifySocket(env, jobId, { status: 'tool_failed', error: msg });
  }
}

// ── AI helpers ────────────────────────────────────────────────────────────────

async function callPerplexity(env, history, prompt) {
  const messages = buildMessages(history, prompt);
  const res = await fetch('https://api.perplexity.ai/chat/completions', {
    method: 'POST',
    headers: {
      'Authorization': 'Bearer ' + env.PERPLEXITY_API_KEY,
      'Content-Type': 'application/json'
    },
    body: JSON.stringify({ model: 'sonar', messages })
  });
  const data = await res.json();
  if (!res.ok) throw new Error(data.error?.message || 'Perplexity error ' + res.status);
  return data.choices?.[0]?.message?.content || '(no content)';
}

async function callClaude(env, history, prompt) {
  const messages = buildMessages(history, prompt);
  const res = await fetch('https://api.anthropic.com/v1/messages', {
    method: 'POST',
    headers: {
      'x-api-key':         env.ANTHROPIC_API_KEY,
      'anthropic-version': '2023-06-01',
      'Content-Type':      'application/json'
    },
    body: JSON.stringify({
      model:      'claude-sonnet-4-20250514',
      max_tokens: 800,
      messages
    })
  });
  const data = await res.json();
  if (!res.ok) throw new Error(data.error?.message || 'Claude error ' + res.status);
  return (data.content || []).map(b => b.text || '').join('');
}

// ── Timeline helpers ──────────────────────────────────────────────────────────

async function pushPin(token, title, body, pinTime) {
  const pinId = crypto.randomUUID();
  const pin = {
    id: pinId,
    time: pinTime,
    layout: {
      type: 'genericPin',
      title,
      body,
      tinyIcon: 'system://images/NOTIFICATION_FLAG'
    },
    actions: [{ title: 'Open PWAI', type: 'openWatchApp', launchCode: 1 }]
  };
  const res = await fetch(
    `https://timeline-api.rebble.io/v1/user/pins/${encodeURIComponent(pinId)}`,
    {
      method: 'PUT',
      headers: { 'X-User-Token': token, 'Content-Type': 'application/json' },
      body: JSON.stringify(pin)
    }
  );
  if (!res.ok) {
    const txt = await res.text();
    return { error: 'Rebble ' + res.status + ': ' + txt };
  }
  return { pinId };
}

function buildMessages(history, prompt) {
  const msgs = history.map(r => ({ role: r.role, content: r.content }));
  if (!msgs.length || msgs[msgs.length - 1].role !== 'user')
    msgs.push({ role: 'user', content: prompt });
  return msgs;
}

function jsonRes(data, status = 200) {
  return new Response(JSON.stringify(data), {
    status,
    headers: { 'Content-Type': 'application/json' }
  });
}
