import assert from 'node:assert/strict';
import { test } from 'node:test';
import { video_rect, normalized_pointer, outward_arrow } from './line_geometry.js';

test('maps pointers to the video content and excludes letterbox bars', () => {
  const area = video_rect(1000, 800, 16 / 9);
  assert.equal(area.width, 1000);
  assert.equal(area.height, 562.5);
  assert.deepEqual(normalized_pointer(500, 400, area), [0.5, 0.5]);
  assert.equal(normalized_pointer(500, 0, area), null);
  assert.deepEqual(normalized_pointer(-50, 900, area, true), [0, 1]);
  const tall = video_rect(1000, 400, 16 / 9);
  assert.equal(tall.height, 400);
  assert.ok(tall.left > 0);
});
test('outward arrow points down for left-to-right and up when reversed', () => {
  const arrow = outward_arrow([[0.1, 0.5], [0.9, 0.5]], 1000, 562.5);
  assert.equal(arrow.start[0], arrow.end[0]);
  assert.ok(arrow.end[1] > arrow.start[1]);
  const flipped = outward_arrow([[0.9, 0.5], [0.1, 0.5]], 1000, 562.5);
  assert.ok(flipped.end[1] < flipped.start[1]);
  assert.equal(outward_arrow([[0.5, 0.5], [0.5, 0.5]], 1000, 562.5), null);
});
