import assert from 'node:assert/strict';
import { test } from 'node:test';
import { load_config } from '../src/config.js';

const env = { MEDIAMTX_PUBLIC_URL: 'http://192.168.138.217:8889' };

test('uses configured public player base and default listen port', () => {
  assert.deepEqual(load_config(env), {
    port: 3000,
    player_base_url: 'http://192.168.138.217:8889',
    cors_origins: [],
  });
});

test('normalizes HTTPS base path and explicit frontend origins', () => {
  assert.deepEqual(load_config({
    PORT: '3001', MEDIAMTX_PUBLIC_URL: 'https://video.example.com/media/',
    WEB_ORIGINS: 'http://localhost:5173, http://192.168.138.217:5173',
  }), {
    port: 3001, player_base_url: 'https://video.example.com/media',
    cors_origins: ['http://localhost:5173', 'http://192.168.138.217:5173'],
  });
});

for (const port of ['0', '65536', '-1', '3.5', '3000abc', '']) {
  test(`rejects invalid port ${JSON.stringify(port)}`, () => {
    assert.throws(() => load_config({ ...env, PORT: port }), /PORT/);
  });
}

for (const base of ['', 'invalid', 'rtsp://host/relay', 'http://user:pass@host',
  'http://host?key=secret', 'http://host#fragment']) {
  test(`rejects invalid public player base ${JSON.stringify(base)}`, () => {
    assert.throws(() => load_config({ MEDIAMTX_PUBLIC_URL: base }), /MEDIAMTX_PUBLIC_URL/);
  });
}

test('requires public player base', () => {
  assert.throws(() => load_config({}), /MEDIAMTX_PUBLIC_URL/);
});

test('rejects non-origin values and wildcard CORS', () => {
  for (const origin of ['*', 'http://host/path', 'rtsp://host', 'http://user:pass@host']) {
    assert.throws(() => load_config({ ...env, WEB_ORIGINS: origin }), /WEB_ORIGINS/);
  }
});
