// Browser regression with mocked API; does not modify a running stream's scene.
const assert = require('node:assert/strict');
const { chromium } = require(process.env.PLAYWRIGHT_MODULE || 'playwright');
(async () => {
  const browser = await chromium.launch({ headless: true, args: ['--no-sandbox'] });
  const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
  let saved_zone = null;
  const errors = [];
  page.on('pageerror', error => errors.push(error.message));
  await page.route('**/api/**', route => {
    const request = route.request();
    const url = new URL(request.url());
    if (url.pathname.endsWith('/live-view')) {
      const mode = url.searchParams.get('mode') || 'raw';
      return route.fulfill({ json: { stream_id: 'board43', mode, protocol: 'webrtc',
        player_url: `http://127.0.0.1:18889/${mode === 'ai' ? 'ai' : 'relay'}` } });
    }
    if (url.pathname.endsWith('/live-zone') && request.method() === 'PUT') {
      saved_zone = request.postDataJSON().zone;
    }
    return route.fulfill({ json: { session_id: 'test', revision: 'test',
      line_revision: 'test', zone_revision: 'test', line: null, zone: saved_zone } });
  });
  await page.route(/\/(ai|relay)\?/, route => route.fulfill({ contentType: 'text/html',
    body: '<html><body style="margin:0;background:black"></body></html>' }));
  try {
    await page.goto(process.env.LIVE_VIEW_WEB_URL || 'http://127.0.0.1:18000', { waitUntil: 'networkidle' });
    await page.getByRole('button', { name: 'Bộ lọc đối tượng' }).click();
    await page.getByRole('button', { name: 'Vẽ zone', exact: true }).click();
    const svg = page.locator('[data-testid="zone-editor"] svg');
    const box = await svg.boundingBox();
    // Fourth point is inside the automatically closed triangle; fifth is on its old edge.
    const points = [[0.2, 0.2], [0.8, 0.2], [0.8, 0.8], [0.6, 0.4], [0.4, 0.4], [0.2, 0.8]];
    for (let index = 0; index < points.length; ++index) {
      const [x, y] = points[index];
      await page.mouse.click(box.x + box.width * x, box.y + box.height * y);
      assert.equal(await svg.locator('[data-endpoint]').count(), index + 1);
      assert.equal(await page.getByTestId('zone-point-count').innerText(), `Đã chọn ${index + 1}/32 điểm`);
    }
    const response = page.waitForResponse(reply => reply.url().endsWith('/live-zone') && reply.request().method() === 'PUT');
    await page.getByRole('button', { name: 'Lưu zone', exact: true }).click();
    assert.equal((await response).status(), 200);
    assert.equal(saved_zone.length, 6);
    for (let index = 0; index < points.length; ++index) {
      assert.ok(Math.abs(saved_zone[index][0] - points[index][0]) < 0.005);
      assert.ok(Math.abs(saved_zone[index][1] - points[index][1]) < 0.005);
    }
    await page.getByRole('button', { name: 'Đóng', exact: true }).click();
    const overlay = page.getByTestId('zone-overlay');
    await overlay.waitFor();
    assert.equal(await overlay.locator('[data-endpoint]').count(), 0);
    const polygon = overlay.locator('polygon');
    assert.equal(await polygon.getAttribute('fill'), 'rgba(255, 165, 0, 0.15)');
    const check_shade = () => page.waitForFunction(expected => {
      const svg = document.querySelector('[data-testid="zone-overlay"] svg');
      const polygon = svg?.querySelector('polygon');
      if (!polygon || svg.viewBox.baseVal.width <= 1) return false;
      const { width, height } = svg.viewBox.baseVal;
      return polygon.points.numberOfItems === expected.length && expected.every(([x, y], index) => {
        const point = polygon.points.getItem(index);
        return Math.abs(point.x / width - x) < 0.005 && Math.abs(point.y / height - y) < 0.005;
      });
    }, saved_zone);
    await check_shade();
    await page.getByRole('button', { name: 'Bộ lọc đối tượng' }).click();
    await page.getByRole('button', { name: 'Vẽ line', exact: true }).click();
    await check_shade();
    const line_svg = page.getByTestId('line-editor').locator('svg');
    const line_box = await line_svg.boundingBox();
    for (const [x, y] of [[0.3, 0.25], [0.7, 0.25]]) {
      await page.mouse.click(line_box.x + line_box.width * x, line_box.y + line_box.height * y);
    }
    assert.equal(await line_svg.locator('[data-endpoint]').count(), 2);
    await page.getByRole('button', { name: /Luồng Gốc/ }).click();
    assert.equal(await overlay.count(), 0);
    await page.getByRole('button', { name: /Luồng AI/ }).click();
    await overlay.waitFor();
    await page.getByRole('button', { name: 'Bộ lọc đối tượng' }).click();
    await page.getByRole('button', { name: 'Vẽ zone', exact: true }).click();
    assert.equal(await overlay.count(), 0);
    assert.equal(await page.getByTestId('zone-editor').locator('polygon').count(), 1);
    const deleted = page.waitForResponse(reply => reply.url().endsWith('/live-zone') && reply.request().method() === 'PUT');
    await page.getByRole('button', { name: 'Xóa zone', exact: true }).click();
    await deleted;
    await page.getByRole('button', { name: 'Đóng', exact: true }).click();
    assert.equal(await overlay.count(), 0);
    assert.deepEqual(errors, []);
    console.log('LIVE_ZONE_POINTS_AND_PERSISTENT_SHADE_PASS');
  } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
