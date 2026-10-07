export async function fetch_live_view({ signal, base_url = import.meta.env?.VITE_API_BASE_URL ?? '', mode = 'raw' } = {}) {
  if (mode !== 'raw' && mode !== 'ai') throw new Error('Invalid live-view mode.');
  const query = mode === 'ai' ? '?mode=ai' : '';
  const response = await fetch(`${base_url}/api/v1/streams/board43/live-view${query}`, { signal });
  if (!response.ok) throw new Error(`Không lấy được live view (HTTP ${response.status}).`);
  const dto = await response.json();
  let url;
  try { url = new URL(dto?.player_url); }
  catch { throw new Error('Invalid live-view metadata.'); }
  if (dto?.stream_id !== 'board43' || dto.mode !== mode || dto.protocol !== 'webrtc' ||
      !['http:', 'https:'].includes(url.protocol) || url.username || url.password) {
    throw new Error('Invalid live-view metadata.');
  }
  return dto;
}

async function send_live_line(path, method, body, { signal, keepalive = false,
  base_url = import.meta.env?.VITE_API_BASE_URL ?? '' } = {}) {
  const response = await fetch(`${base_url}/api/v1/streams/board43/live-line${path}`, {
    method, headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body), signal, keepalive,
  });
  if (!response.ok) {
    if (response.status === 409) throw new Error('Một trang khác đang điều khiển line. Reload trang để tiếp tục.');
    throw new Error(`Không lưu được line (HTTP ${response.status}).`);
  }
  return response.json();
}
export function start_line_session(session_id, options) {
  return send_live_line('/session', 'POST', { session_id }, options);
}
export function save_live_line(session_id, line, options) {
  return send_live_line('', 'PUT', { session_id, line }, options);
}

export async function save_live_zone(session_id, zone, { signal, keepalive = false,
  base_url = import.meta.env?.VITE_API_BASE_URL ?? '' } = {}) {
  const response = await fetch(`${base_url}/api/v1/streams/board43/live-zone`, {
    method: 'PUT', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ session_id, zone }), signal, keepalive,
  });
  if (!response.ok) {
    if (response.status === 409) throw new Error('Một trang khác đang điều khiển zone. Reload trang để tiếp tục.');
    const dto = await response.json().catch(() => null);
    throw new Error(dto?.error?.message ?? `Không lưu được zone (HTTP ${response.status}).`);
  }
  return response.json();
}
