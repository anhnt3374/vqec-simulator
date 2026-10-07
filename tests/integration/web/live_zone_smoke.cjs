// Run in the Docker browser image against a real WebUI + backend; camera iframe is stubbed.
const assert = require('node:assert/strict');
const { readFile } = require('node:fs/promises');
const { chromium } = require(process.env.PLAYWRIGHT_MODULE || 'playwright');
const web_url = process.env.LIVE_VIEW_WEB_URL || 'http://127.0.0.1:8000';

(async () => {
  const browser = await chromium.launch({ headless: true, args: ['--no-sandbox'] });
  const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
  const errors = [];
  page.on('pageerror', error => errors.push(error.message));
  const snapshot = async () => JSON.parse(await readFile(process.env.LIVE_LINE_SNAPSHOT, 'utf8'));
  const open_editor = async kind => {
    await page.getByRole('button', { name: 'Bộ lọc đối tượng' }).click();
    await page.getByRole('button', { name: `Vẽ ${kind}`, exact: true }).click();
    const svg = page.locator(`[data-testid="${kind}-editor"] svg`);
    await svg.waitFor();
    return svg;
  };
  const click_points = async (svg, points) => {
    const box = await svg.boundingBox();
    for (const point of points) await page.mouse.click(box.x + box.width * point[0], box.y + box.height * point[1]);
  };
  const send = async (kind, name, expected = 200) => {
    const reply = page.waitForResponse(response => response.url().endsWith(`/live-${kind}`) && response.request().method() === 'PUT');
    await page.getByRole('button', { name, exact: true }).click();
    const response = await reply;
    assert.equal(response.status(), expected);
    return response.json();
  };
  try {
    await page.route(/\/(ai|relay)\?/, route => route.fulfill({
      contentType: 'text/html', body: '<html><body style="margin:0;background:black"></body></html>',
    }));
    await page.goto(web_url, { waitUntil: 'networkidle' });
    const line_svg = await open_editor('line');
    await click_points(line_svg, [[0.1, 0.5], [0.9, 0.5]]);
    const line = await send('line', 'Lưu line');
    const zone_svg = await open_editor('zone');
    assert.equal(await page.getByRole('button', { name: 'Lưu zone', exact: true }).isDisabled(), true);
    await click_points(zone_svg, [[0.2, 0.2], [0.8, 0.2], [0.8, 0.8], [0.2, 0.8], [0.1, 0.5]]);
    await page.getByRole('button', { name: 'Bỏ điểm cuối', exact: true }).click();
    assert.equal(await zone_svg.locator('[data-endpoint]').count(), 4);
    const saved = await send('zone', 'Lưu zone');
    assert.equal(saved.zone.length, 4);
    assert.deepEqual(saved.line, line.line);
    assert.equal(saved.line_revision, line.line_revision);
    const handle = await zone_svg.locator('[data-endpoint="0"] circle').boundingBox();
    const box = await zone_svg.boundingBox();
    await page.mouse.move(handle.x + handle.width / 2, handle.y + handle.height / 2);
    await page.mouse.down();
    await page.mouse.move(box.x + box.width * 0.25, box.y + box.height * 0.25, { steps: 8 });
    await page.mouse.up();
    const edited = await send('zone', 'Lưu zone');
    assert.ok(Math.abs(edited.zone[0][0] - 0.25) < 0.005);
    assert.ok(Math.abs(edited.zone[0][1] - 0.25) < 0.005);
    assert.equal(edited.line_revision, line.line_revision);
    if (process.env.LIVE_LINE_SNAPSHOT) assert.deepEqual((await snapshot()).zone, edited.zone);
    await page.getByRole('button', { name: 'Vẽ lại', exact: true }).click();
    await click_points(zone_svg, [[0.2, 0.2], [0.8, 0.8], [0.2, 0.8], [0.8, 0.2]]);
    await send('zone', 'Lưu zone', 400);
    await page.getByRole('alert').waitFor();
    if (process.env.LIVE_LINE_SNAPSHOT) assert.deepEqual((await snapshot()).zone, edited.zone);
    const deleted = await send('zone', 'Xóa zone');
    assert.equal(deleted.zone, null);
    assert.deepEqual(deleted.line, line.line);
    await page.waitForFunction(() => document.querySelectorAll('[data-endpoint]').length === 0);
    await click_points(zone_svg, [[0.2, 0.2], [0.8, 0.2], [0.8, 0.8], [0.2, 0.8]]);
    await send('zone', 'Lưu zone');
    await page.getByRole('button', { name: /Luồng Gốc/ }).click();
    assert.equal(await page.locator('[data-testid="zone-editor"]').count(), 0);
    await page.getByRole('button', { name: /Luồng AI/ }).click();
    assert.equal(await zone_svg.locator('[data-endpoint]').count(), 4);
    const reset_reply = page.waitForResponse(response => response.url().endsWith('/live-line/session') && response.request().method() === 'POST');
    await page.reload({ waitUntil: 'networkidle' });
    const reset = await (await reset_reply).json();
    assert.equal(reset.line, null);
    assert.equal(reset.zone, null);
    if (process.env.LIVE_LINE_SNAPSHOT) {
      const current = await snapshot();
      assert.equal(current.line, null);
      assert.equal(current.zone, null);
    }
    assert.deepEqual(errors, []);
    console.log('LIVE_ZONE_DRAW_EDIT_UNDO_INVALID_DELETE_RELOAD_PASS');
  } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
