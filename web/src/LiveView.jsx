import { useEffect, useState } from 'react';
import { fetch_live_view } from './api_client.js';

export default function LiveView() {
  const [attempt, setAttempt] = useState(0);
  const [state, setState] = useState({ loading: true, player_url: null, error: null });

  useEffect(() => {
    const controller = new AbortController();
    let active = true;
    setState({ loading: true, player_url: null, error: null });
    fetch_live_view({ signal: controller.signal })
      .then(dto => {
        if (!active) return;
        const url = new URL(dto.player_url);
        url.searchParams.set('autoplay', 'true');
        url.searchParams.set('muted', 'true');
        setState({ loading: false, player_url: url.href, error: null });
      })
      .catch(error => {
        if (!active || error.name === 'AbortError') return;
        console.error('[live_view] Could not load player metadata', error);
        setState({ loading: false, player_url: null, error: error.message });
      });
    return () => { active = false; controller.abort(); };
  }, [attempt]);

  if (state.loading) return <div className="live-view-message" role="status">Đang kết nối live view…</div>;
  if (state.error) {
    return (
      <div className="live-view-message" role="alert">
        <p>{state.error}</p>
        <button className="action-btn" onClick={() => setAttempt(value => value + 1)}>Thử lại</button>
      </div>
    );
  }
  return (
    <iframe
      id="live-view"
      title="Board 43 — Luồng Gốc"
      allow="autoplay; fullscreen"
      allowFullScreen
      src={state.player_url}
    />
  );
}
