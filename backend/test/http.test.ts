import assert from 'node:assert/strict';
import { after, before, test } from 'node:test';
import { Logger, type INestApplication } from '@nestjs/common';
import { create_app } from '../src/app.module.js';

let app: INestApplication;
let base: string;
const access_lines: string[] = [];
before(async () => {
  Logger.overrideLogger({
    log() {}, error(message) { access_lines.push(String(message)); },
    warn(message) { access_lines.push(String(message)); },
    debug(message) { access_lines.push(String(message)); },
  });
  app = await create_app({
    port: 3000, player_base_url: 'http://192.168.138.217:8889',
    cors_origins: ['http://localhost:5173'],
  });
  app.use('/api/v1/fail-test', () => { throw new Error('private-token-super-secret'); });
  await app.listen(0, '127.0.0.1');
  base = await app.getUrl();
});
after(async () => { await app?.close(); });

test('exposes process liveness on the documented route', async () => {
  const response = await fetch(`${base}/api/v1/health`);
  assert.equal(response.status, 200);
  assert.deepEqual(await response.json(), { status: 'ok' });
});

test('temporary line sessions save edits, clear on reload and reject stale tabs', async () => {
  const url = `${base}/api/v1/streams/board43/live-line`;
  const send = (path: string, method: string, body: unknown) => fetch(`${url}${path}`, {
    method, headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body),
  });
  assert.equal((await send('/session', 'POST', { session_id: 'page_first' })).status, 201);
  const line = [[0.1, 0.5], [0.9, 0.5]];
  const saved = await send('', 'PUT', { session_id: 'page_first', line });
  assert.equal(saved.status, 200);
  assert.deepEqual((await saved.json()).line, line);
  for (const invalid of [[], [[0, 0], [0, 0]], [[-1, 0], [1, 1]], [[0, 0], ['1', 1]]]) {
    assert.equal((await send('', 'PUT', { session_id: 'page_first', line: invalid })).status, 400);
  }
  const reloaded = await send('/session', 'POST', { session_id: 'page_reloaded' });
  assert.equal(reloaded.status, 201);
  assert.equal((await reloaded.json()).line, null);
  assert.equal((await send('', 'PUT', { session_id: 'page_first', line })).status, 409);
  assert.equal((await send('', 'PUT', { session_id: 'page_reloaded', line })).status, 200);
  const cleared = await send('', 'PUT', { session_id: 'page_reloaded', line: null });
  assert.equal(cleared.status, 200);
  assert.equal((await cleared.json()).line, null);
});

test('allows temporary line writes in configured CORS preflight', async () => {
  const response = await fetch(`${base}/api/v1/streams/board43/live-line`, {
    method: 'OPTIONS', headers: {
      Origin: 'http://localhost:5173', 'Access-Control-Request-Method': 'PUT',
      'Access-Control-Request-Headers': 'content-type',
    },
  });
  assert.match(response.headers.get('access-control-allow-methods') ?? '', /PUT/);
});

test('serves live-view metadata independent of caller Host header', async () => {
  const response = await fetch(`${base}/api/v1/streams/board43/live-view`, {
    headers: { Host: 'attacker.invalid', Origin: 'http://localhost:5173' },
  });
  assert.equal(response.status, 200);
  assert.equal(response.headers.get('access-control-allow-origin'), 'http://localhost:5173');
  assert.deepEqual(await response.json(), {
    stream_id: 'board43', mode: 'raw', protocol: 'webrtc',
    player_url: 'http://192.168.138.217:8889/relay',
  });
});

test('unknown streams use the repository 404 envelope', async () => {
  const response = await fetch(`${base}/api/v1/streams/unknown/live-view`);
  assert.equal(response.status, 404);
  assert.deepEqual(await response.json(), {
    error: { code: 'not_found', message: "stream 'unknown' does not exist" },
  });
});

test('AI query selects annotated playback and malformed queries return 400', async () => {
  const response = await fetch(`${base}/api/v1/streams/board43/live-view?mode=ai`);
  assert.equal(response.status, 200);
  assert.equal((await response.json()).mode, 'ai');
  for (const query of ['mode=unknown', 'mode=raw&mode=ai']) {
    const invalid = await fetch(`${base}/api/v1/streams/board43/live-view?${query}`);
    assert.equal(invalid.status, 400);
    assert.equal((await invalid.json()).error.code, 'invalid_argument');
  }
});

test('unknown routes also use the repository error envelope', async () => {
  const response = await fetch(`${base}/api/v1/missing`);
  assert.equal(response.status, 404);
  const body = await response.json() as { error: { code: string; message: string } };
  assert.equal(body.error.code, 'not_found');
  assert.equal(typeof body.error.message, 'string');
});

test('does not grant CORS to an unconfigured frontend', async () => {
  const response = await fetch(`${base}/api/v1/health`, {
    headers: { Origin: 'http://untrusted.example' },
  });
  assert.equal(response.headers.get('access-control-allow-origin'), null);
});

test('logs unknown-route access exactly once and excludes query credentials', async () => {
  const start = access_lines.length;
  const response = await fetch(`${base}/api/v1/unregistered?token=super-secret`);
  await response.text();
  assert.equal(response.status, 404);
  const lines = access_lines.slice(start).filter(line => line.startsWith('[http]'));
  assert.equal(lines.length, 1);
  assert.match(lines[0], /GET \/api\/v1\/unregistered status=404 duration_ms=/);
  assert.doesNotMatch(lines[0], /super-secret|token=/);
});

test('logs unexpected error cause once without leaking secrets in response or logs', async () => {
  const start = access_lines.length;
  const response = await fetch(`${base}/api/v1/fail-test`);
  assert.equal(response.status, 500);
  assert.deepEqual(await response.json(), {
    error: { code: 'internal', message: 'Internal server error' },
  });
  const lines = access_lines.slice(start).filter(line => line.startsWith('[http]'));
  assert.equal(lines.length, 1);
  assert.match(lines[0], /cause=Error/);
  assert.match(lines[0], /location=.*http\.test/);
  assert.doesNotMatch(lines[0], /private-token|super-secret/);
});

test('zone saves coexist with line and session reload clears both', async () => {
  const base_path = `${base}/api/v1/streams/board43`;
  const send = (path: string, body: unknown, method = 'PUT') => fetch(`${base_path}${path}`, {
    method, headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body),
  });
  await send('/live-line/session', { session_id: 'zone_page' }, 'POST');
  const line = [[0.1, 0.5], [0.9, 0.5]];
  const zone = [[0.2, 0.2], [0.8, 0.2], [0.8, 0.8], [0.2, 0.8]];
  const saved_line = await (await send('/live-line', { session_id: 'zone_page', line })).json();
  const response = await send('/live-zone', { session_id: 'zone_page', zone });
  assert.equal(response.status, 200);
  const saved_zone = await response.json();
  assert.deepEqual(saved_zone.zone, zone);
  assert.deepEqual(saved_zone.line, line);
  assert.equal(saved_zone.line_revision, saved_line.line_revision);
  const line_edit = await (await send('/live-line', { session_id: 'zone_page', line: [...line].reverse() })).json();
  assert.deepEqual(line_edit.zone, zone);
  assert.equal(line_edit.zone_revision, saved_zone.zone_revision);
  const clear = await (await send('/live-zone', { session_id: 'zone_page', zone: null })).json();
  assert.equal(clear.zone, null);
  assert.deepEqual(clear.line, [...line].reverse());
  await send('/live-zone', { session_id: 'zone_page', zone });
  const reset = await (await send('/live-line/session', { session_id: 'new_zone_page' }, 'POST')).json();
  assert.equal(reset.line, null);
  assert.equal(reset.zone, null);
  assert.equal((await send('/live-zone', { session_id: 'zone_page', zone })).status, 409);
});

test('zone rejects malformed degenerate and self-intersecting polygons', async () => {
  const url = `${base}/api/v1/streams/board43`;
  const send = (zone: unknown) => fetch(`${url}/live-zone`, {
    method: 'PUT', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ session_id: 'valid_zone_page', zone }),
  });
  await fetch(`${url}/live-line/session`, { method: 'POST', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ session_id: 'valid_zone_page' }) });
  for (const zone of [[], [[0, 0], [1, 1]], [[0, 0], [0.5, 0.5], [1, 1]],
    [[0, 0], [1, 0], [1, 0], [0, 1]], [[0, 0], [1, 1], [0, 1], [1, 0]],
    [[0, 0], [1.1, 0], [0, 1]], [[0, 0], ['1', 0], [0, 1]], Array(33).fill([0.5, 0.5])]) {
    assert.equal((await send(zone)).status, 400);
  }
  // Concave polygons are valid too.
  assert.equal((await send([[0.1, 0.1], [0.9, 0.1], [0.5, 0.4], [0.9, 0.9], [0.1, 0.9]])).status, 200);
});
