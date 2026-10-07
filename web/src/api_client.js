export async function fetch_live_view({ signal, base_url = import.meta.env?.VITE_API_BASE_URL ?? '' } = {}) {
  const response = await fetch(`${base_url}/api/v1/streams/board43/live-view`, { signal });
  if (!response.ok) throw new Error(`Không lấy được live view (HTTP ${response.status}).`);
  const dto = await response.json();
  let url;
  try { url = new URL(dto?.player_url); }
  catch { throw new Error('Invalid live-view metadata.'); }
  if (dto?.stream_id !== 'board43' || dto.mode !== 'raw' || dto.protocol !== 'webrtc' ||
      !['http:', 'https:'].includes(url.protocol) || url.username || url.password) {
    throw new Error('Invalid live-view metadata.');
  }
  return dto;
}
