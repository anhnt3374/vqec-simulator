// Run inside an Ubuntu Docker browser-test container; requires a running stack.
const assert = require('node:assert/strict');
const { chromium } = require(process.env.PLAYWRIGHT_MODULE || 'playwright');

const web_url = process.env.LIVE_VIEW_WEB_URL || 'http://127.0.0.1:8000';

(async () => {
  const browser = await chromium.launch({ headless: true, args: ['--no-sandbox'] });
  const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
  const errors = [];
  page.on('pageerror', error => errors.push(error.message));
  try {
    const response = await page.request.get(`${web_url}/api/v1/streams/board43/live-view`);
    assert.equal(response.status(), 200);
    const metadata = await response.json();
    assert.equal(metadata.mode, 'raw');
    const player_origin = new URL(metadata.player_url).origin;
    await page.goto(web_url, { waitUntil: 'domcontentloaded' });
    await page.locator('#live-view').waitFor();
    assert.equal(await page.getByRole('button', { name: /Luồng AI/ }).isDisabled(), true);
    await page.frameLocator('#live-view').locator('video').waitFor();
    const frame = page.frames().find(frame => frame.url().startsWith(player_origin));
    await frame.waitForFunction(() => {
      const video = document.querySelector('video');
      return video && video.readyState >= 2 && video.videoWidth > 0 &&
        video.getVideoPlaybackQuality().totalVideoFrames >= 10;
    }, undefined, { timeout: 30000 });
    const stats = await frame.locator('video').evaluate(video => ({
      width: video.videoWidth, height: video.videoHeight, time: video.currentTime,
      decoded_frames: video.getVideoPlaybackQuality().totalVideoFrames,
    }));
    console.log('VIDEO_PLAYING', JSON.stringify(stats));
    if (process.env.LIVE_VIEW_SCREENSHOT) {
      await page.screenshot({ path: process.env.LIVE_VIEW_SCREENSHOT });
    }

    let failed_once = false;
    const api_pattern = '**/api/v1/streams/board43/live-view';
    await page.route(api_pattern, route => {
      if (!failed_once) {
        failed_once = true;
        return route.fulfill({ status: 503, body: '{}' });
      }
      return route.continue();
    });
    await page.reload({ waitUntil: 'domcontentloaded' });
    await page.getByRole('alert').waitFor();
    await page.getByRole('button', { name: 'Thử lại' }).click();
    await page.locator('#live-view').waitFor();
    await page.unroute(api_pattern);
    console.log('API_RETRY_PASS');

    // Mount the real component using Vite modules, then unmount during its fetch.
    // This checks effect cleanup without adding a frontend test framework.
    let received;
    const intercepted = new Promise(resolve => { received = resolve; });
    await page.route(api_pattern, route => received(route));
    await page.evaluate(async () => {
      const [{ default: ReactDOM }, { default: React }, { default: LiveView }] = await Promise.all([
        import('/node_modules/.vite/deps/react-dom_client.js'),
        import('/node_modules/.vite/deps/react.js'),
        import('/src/LiveView.jsx'),
      ]);
      const container = document.createElement('div');
      document.body.append(container);
      window.live_view_test_root = ReactDOM.createRoot(container);
      window.live_view_test_root.render(React.createElement(LiveView));
    });
    const route = await intercepted;
    const aborted = page.waitForEvent('requestfailed', {
      predicate: request => request.url().endsWith('/api/v1/streams/board43/live-view'),
      timeout: 5000,
    });
    await page.evaluate(() => window.live_view_test_root.unmount());
    const request = await aborted;
    assert.match(request.failure().errorText, /ABORTED|CANCELLED/i);
    await route.abort().catch(() => {});
    await page.unroute(api_pattern);
    console.log('COMPONENT_UNMOUNT_ABORT_PASS');
    assert.deepEqual(errors, []);
  } finally {
    await browser.close();
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
