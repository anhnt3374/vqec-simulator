import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { once } from 'node:events';
import { test } from 'node:test';
import { fetch_live_view } from './api_client.js';

const dto = {
  stream_id: 'board43', mode: 'raw', protocol: 'webrtc',
  player_url: 'http://192.168.138.217:8889/relay',
};

async function with_server(handler, run) {
  const server = createServer(handler);
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  const base_url = `http://127.0.0.1:${server.address().port}`;
  try { await run(base_url); }
  finally { server.closeAllConnections(); await new Promise(resolve => server.close(resolve)); }
}

test('fetches the documented endpoint and returns valid playback metadata', async () => {
  await with_server((request, response) => {
    if (request.url !== '/api/v1/streams/board43/live-view') {
      response.writeHead(404).end(); return;
    }
    response.setHeader('Content-Type', 'application/json');
    response.end(JSON.stringify(dto));
  }, async base_url => {
    assert.deepEqual(await fetch_live_view({ base_url }), dto);
  });
});

test('reports HTTP failure and allows a later successful retry', async () => {
  let attempts = 0;
  await with_server((request, response) => {
    if (++attempts === 1) {
      response.writeHead(503).end(JSON.stringify({ error: { message: 'unavailable' } }));
    } else { response.end(JSON.stringify(dto)); }
  }, async base_url => {
    await assert.rejects(fetch_live_view({ base_url }), /503/);
    assert.deepEqual(await fetch_live_view({ base_url }), dto);
  });
});

for (const bad of [
  { ...dto, protocol: 'rtsp' }, { ...dto, mode: 'ai' },
  { ...dto, stream_id: 'unknown' }, { ...dto, player_url: 'javascript:alert(1)' },
  { ...dto, player_url: 'http://user:secret@host' }, {}, null,
]) {
  test(`rejects invalid playback metadata ${JSON.stringify(bad)}`, async () => {
    await with_server((request, response) => response.end(JSON.stringify(bad)), async base_url => {
      await assert.rejects(fetch_live_view({ base_url }), /metadata/i);
    });
  });
}

test('supports cancellation of an in-flight API request', async () => {
  let received;
  const arrived = new Promise(resolve => { received = resolve; });
  await with_server(() => received(), async base_url => {
    const controller = new AbortController();
    const pending = fetch_live_view({ base_url, signal: controller.signal });
    await Promise.race([arrived, pending]);
    controller.abort();
    await assert.rejects(pending, { name: 'AbortError' });
  });
});

test('requests AI metadata and rejects a stale raw response', async () => {
  let request_url;
  const ai_dto = { ...dto, mode: 'ai', player_url: 'http://192.168.138.217:8889/ai' };
  await with_server((request, response) => {
    request_url = request.url;
    response.end(JSON.stringify(ai_dto));
  }, async base_url => {
    assert.deepEqual(await fetch_live_view({ base_url, mode: 'ai' }), ai_dto);
    assert.equal(request_url, '/api/v1/streams/board43/live-view?mode=ai');
  });
  await with_server((request, response) => response.end(JSON.stringify(dto)), async base_url => {
    await assert.rejects(fetch_live_view({ base_url, mode: 'ai' }), /metadata/i);
  });
});
