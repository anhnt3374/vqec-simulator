// Run in the Docker browser image against WebUI + backend. No camera is required.
// LIVE_LINE_SNAPSHOT optionally verifies the real backend → worker IPC file.
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
  let last_saved;
  const save = async () => {
    const reply = page.waitForResponse(response => response.url().endsWith('/live-line') && response.request().method() === 'PUT');
    await page.getByRole('button', { name: 'Lưu line', exact: true }).click();
    const response = await reply;
    assert.equal(response.status(), 200);
    last_saved = await response.json();
    if (process.env.LIVE_LINE_SNAPSHOT) assert.deepEqual((await snapshot()).line, last_saved.line);
    await page.getByRole('button', { name: 'Lưu line', exact: true }).waitFor({ state: 'visible' });
  };
  try {
    // Isolate editor interaction from camera availability; the API remains real.
    await page.route(/\/(ai|relay)\?/, route => route.fulfill({
      contentType: 'text/html', body: '<html><body style="margin:0;background:black"></body></html>',
    }));
    await page.goto(web_url, { waitUntil: 'networkidle' });
    await page.getByRole('button', { name: 'Bộ lọc đối tượng' }).click();
    await page.getByRole('button', { name: 'Vẽ line', exact: true }).click();
    assert.equal(await page.getByRole('button', { name: /Luồng AI/ }).getAttribute('aria-pressed'), 'true');
    const editor = page.locator('[data-testid="line-editor"] svg');
    await editor.waitFor();
    await page.waitForFunction(() => !document.querySelector('.line-toolbar button:nth-child(2)')?.disabled);
    let box = await editor.boundingBox();
    await page.mouse.click(box.x + box.width * 0.2, box.y + box.height * 0.4);
    await page.mouse.click(box.x + box.width * 0.8, box.y + box.height * 0.4);
    assert.equal(await editor.locator('[data-endpoint]').count(), 2);
    await save();
    assert.ok(Math.abs(last_saved.line[0][0] - 0.2) < 0.005);
    assert.ok(Math.abs(last_saved.line[1][1] - 0.4) < 0.005);
    const first_line = structuredClone(last_saved.line);
    const handle = await editor.locator('[data-endpoint="1"] circle').boundingBox();
    await page.mouse.move(handle.x + handle.width / 2, handle.y + handle.height / 2);
    await page.mouse.down();
    await page.mouse.move(box.x + box.width * 0.7, box.y + box.height * 0.6, { steps: 8 });
    await page.mouse.up();
    await save();
    assert.ok(Math.abs(last_saved.line[1][1] - 0.6) < 0.005);
    assert.notDeepEqual(last_saved.line, first_line);
    const edited = structuredClone(last_saved.line);
    await page.getByRole('button', { name: 'Đảo chiều', exact: true }).click();
    await save();
    assert.deepEqual(last_saved.line, edited.reverse());
    await page.setViewportSize({ width: 1100, height: 850 });
    await page.getByRole('button', { name: /Luồng Gốc/ }).click();
    assert.equal(await page.locator('[data-testid="line-editor"]').count(), 0);
    await page.getByRole('button', { name: /Luồng AI/ }).click();
    assert.equal(await editor.locator('[data-endpoint]').count(), 2);
    const deleted_reply = page.waitForResponse(response => response.url().endsWith('/live-line') && response.request().method() === 'PUT');
    await page.getByRole('button', { name: 'Xóa line', exact: true }).click();
    assert.equal((await (await deleted_reply).json()).line, null);
    await page.waitForFunction(() => document.querySelectorAll('[data-endpoint]').length === 0);
    box = await editor.boundingBox();
    await page.mouse.click(box.x + box.width * 0.2, box.y + box.height * 0.5);
    await page.mouse.click(box.x + box.width * 0.8, box.y + box.height * 0.5);
    await save();
    const old_session = last_saved.session_id;
    const reload_reply = page.waitForResponse(response => response.url().endsWith('/live-line/session') && response.request().method() === 'POST');
    await page.reload({ waitUntil: 'networkidle' });
    const reloaded = await (await reload_reply).json();
    assert.equal(reloaded.line, null);
    assert.notEqual(reloaded.session_id, old_session);
    if (process.env.LIVE_LINE_SNAPSHOT) assert.equal((await snapshot()).line, null);
    await page.getByRole('button', { name: 'Bộ lọc đối tượng' }).click();
    await page.getByRole('button', { name: 'Vẽ line', exact: true }).click();
    assert.equal(await editor.locator('[data-endpoint]').count(), 0);
    assert.deepEqual(errors, []);
    console.log('LIVE_LINE_DRAW_EDIT_REVERSE_DELETE_RELOAD_PASS');
  } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
