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
