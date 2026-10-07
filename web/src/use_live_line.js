import { useEffect, useRef, useState } from 'react';
import { save_live_line, save_live_zone, start_line_session } from './api_client.js';

function new_session_id() {
  // getRandomValues also works on the LAN HTTP origin where randomUUID may be unavailable.
  return Array.from(crypto.getRandomValues(new Uint8Array(16)), value => value.toString(16).padStart(2, '0')).join('');
}

export default function use_live_line() {
  const session = useRef(null);
  if (!session.current) session.current = new_session_id();
  const [line, set_line] = useState([]);
  const [saved, set_saved] = useState([]);
  const [zone, set_zone] = useState([]);
  const [saved_zone, set_saved_zone] = useState([]);
  const [ready, set_ready] = useState(false);
  const [busy, set_busy] = useState(false);
  const [error, set_error] = useState(null);
  const [attempt, set_attempt] = useState(0);
  const [message, set_message] = useState('');

  useEffect(() => {
    let active = true;
    set_ready(false);
    set_error(null);
    start_line_session(session.current).then(() => {
      if (active) { set_ready(true); set_line([]); set_saved([]); set_zone([]); set_saved_zone([]); set_message(''); }
    }).catch(error => {
      console.error('[live_line] Session initialization failed', error);
      if (active) set_error(error.message);
    });
    const clear_on_exit = () => {
      Promise.all([save_live_line(session.current, null, { keepalive: true }),
        save_live_zone(session.current, null, { keepalive: true })]).catch(error => {
        console.error('[live_line] Session cleanup failed', error);
      });
    };
    window.addEventListener('pagehide', clear_on_exit);
    return () => { active = false; window.removeEventListener('pagehide', clear_on_exit); };
  }, [attempt]);

  const save_region = async (kind, value) => {
    set_busy(true);
    set_error(null);
    try {
      const dto = kind === 'zone'
        ? await save_live_zone(session.current, value.length ? value : null)
        : await save_live_line(session.current, value.length === 2 ? value : null);
      if (kind === 'zone') {
        set_saved_zone(dto.zone ?? []);
        set_zone(dto.zone ?? []);
        set_message(dto.zone ? 'Đã lưu zone tạm. Người ở trong zone đủ 5 giây sẽ có bbox cam.' : 'Đã xóa zone.');
      } else {
        set_saved(dto.line ?? []);
        set_line(dto.line ?? []);
        set_message(dto.line ? 'Đã lưu line tạm. Mũi tên chỉ chiều ra ngoài.' : 'Đã xóa line.');
      }
      return true;
    } catch (error) {
      console.error(`[live_${kind}] Could not save geometry`, error);
      set_error(error.message);
      return false;
    } finally { set_busy(false); }
  };
  return { line, set_line: value => { set_line(value); set_message(''); set_error(null); }, saved,
    zone, set_zone: value => { set_zone(value); set_message(''); set_error(null); }, saved_zone,
    ready, busy, error, message, save: (value = line) => save_region('line', value),
    save_zone: (value = zone) => save_region('zone', value),
    retry: () => set_attempt(value => value + 1) };
}
