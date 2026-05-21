/**
 * PWAI — Cloudflare Worker
 *
 * Routes:
 *   POST /register       — store Pebble timeline token
 *   POST /chat           — queue prompt, return jobId immediately
 *   GET  /status/:jobId  — poll job status
 *   POST /pin            — push Timeline pin to Rebble
 *
 * Bindings: DB (D1), PERPLEXITY_API_KEY, ANTHROPIC_API_KEY (secrets)
 */

export default {
  async fetch(request, env, ctx) {
    const url    = new URL(request.url);
    const path   = url.pathname;
    const method = request.method;

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
      ctx.waitUntil(processJob(env, jobId, token, provider, prompt));
      return jsonRes({ jobId, status: 'pending' });
    }

    if (method === 'GET' && path.startsWith('/status/')) {
      const jobId = decodeURIComponent(path.split('/status/')[1] || '');
      if (!jobId) return jsonRes({ error: 'jobId required' }, 400);
      const row = await env.DB.prepare(
        `SELECT id, status, reply, error FROM jobs WHERE id = ?`
      ).bind(jobId).first();
      if (!row) return jsonRes({ error: 'job not found' }, 404);
      return jsonRes(row);
    }

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

    return jsonRes({ error: 'not found' }, 404);
  }
};

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
  } catch (err) {
    const msg = err && err.message ? err.message : String(err);
    await env.DB.prepare(
      `UPDATE jobs SET status = 'error', error = ?, updated_at = CURRENT_TIMESTAMP WHERE id = ?`
    ).bind(msg, jobId).run();
  }
}

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
