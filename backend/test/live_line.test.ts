import assert from 'node:assert/strict';
import { test } from 'node:test';
import { mkdtemp, readFile, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { LiveLineService } from '../src/live_line.service.js';

test('publishes edits atomically and resets the worker snapshot on backend restart', async () => {
  const dir = await mkdtemp(join(tmpdir(), 'vqec-line-test-'));
  const config = { port: 3000, player_base_url: 'http://host:8889', cors_origins: [],
    live_line_file: join(dir, 'line.json') };
  try {
    const service = new LiveLineService(config);
    await service.onModuleInit();
    await service.start_session('board43', { session_id: 'first' });
    const line = [[0.1, 0.4], [0.9, 0.5]];
    await service.save_line('board43', { session_id: 'first', line });
    assert.deepEqual(JSON.parse(await readFile(config.live_line_file, 'utf8')).line, line);
    await service.save_zone('board43', { session_id: 'first', zone: [[0, 0], [1, 0], [0, 1]] });
    assert.equal(JSON.parse(await readFile(config.live_line_file, 'utf8')).zone.length, 3);
    // New session races an old save: serialization must leave the worker reset.
    const reset = service.start_session('board43', { session_id: 'second' });
    const stale = service.save_line('board43', { session_id: 'first', line });
    await reset;
    await assert.rejects(stale, /newer page/);
    assert.equal(JSON.parse(await readFile(config.live_line_file, 'utf8')).line, null);
    await service.save_line('board43', { session_id: 'second', line });
    await new LiveLineService(config).onModuleInit();
    const snapshot = JSON.parse(await readFile(config.live_line_file, 'utf8'));
    assert.equal(snapshot.line, null);
    assert.equal(snapshot.zone, null);
    assert.equal(snapshot.session_id, null);
  } finally { await rm(dir, { recursive: true, force: true }); }
});
