import assert from 'node:assert/strict';
import { test } from 'node:test';
import { NotFoundException } from '@nestjs/common';
import { LiveViewService } from '../src/live_view.service.js';

test('returns raw browser playback metadata with the configured public host', () => {
  const service = new LiveViewService({
    port: 3000, player_base_url: 'http://192.168.138.217:8889', cors_origins: [],
  });
  assert.deepEqual(service.get_live_view('board43'), {
    stream_id: 'board43', mode: 'raw', protocol: 'webrtc',
    player_url: 'http://192.168.138.217:8889/relay',
  });
});

test('preserves a reverse-proxy base path', () => {
  const service = new LiveViewService({
    port: 3000, player_base_url: 'https://video.example.com/media', cors_origins: [],
  });
  assert.equal(service.get_live_view('board43').player_url, 'https://video.example.com/media/relay');
});

test('rejects unknown stream IDs', () => {
  const service = new LiveViewService({ port: 3000, player_base_url: 'http://host:8889', cors_origins: [] });
  assert.throws(() => service.get_live_view('unknown'), NotFoundException);
});
