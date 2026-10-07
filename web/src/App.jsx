import { useState } from 'react';
import LiveView from './LiveView.jsx';
import LiveLineEditor from './LiveLineEditor.jsx';
import use_live_line from './use_live_line.js';

function Topbar({ mode }) {
  return (
    <header className="topbar">
      <div className="topbar-left">
        <div className="logo">
          <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" className="logo-icon"><polygon points="12 2 2 7 12 12 22 7 12 2"></polygon><polyline points="2 17 12 22 22 17"></polyline><polyline points="2 12 12 17 22 12"></polyline></svg>
          <div className="logo-text">SKYFLOW<br/><small>AI LIVE VIEW</small></div>
        </div>
        <div className="status-badge">
          BOARD 43 &middot; {mode === 'ai' ? 'AI OVERLAY' : 'RAW STREAM'}
        </div>
      </div>
      <div className="topbar-right">
        <div className="stats-top">
          <span>LIVE VIEW: <span className="highlight-cyan">BOARD 43</span></span>
        </div>
        <button className="icon-btn"><svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><path d="M18 8A6 6 0 0 0 6 8c0 7-3 9-3 9h18s-3-2-3-9"></path><path d="M13.73 21a2 2 0 0 1-3.46 0"></path></svg></button>
        <div className="avatar">
          <img src="https://ui-avatars.com/api/?name=Admin&background=0D8ABC&color=fff" alt="User Avatar" />
        </div>
      </div>
    </header>
  );
}

function Sidebar() {
  return (
    <aside className="sidebar">
      <div className="nav-section">
        <div className="nav-title">ĐIỀU HƯỚNG</div>
        <nav>
          <a href="#" className="nav-item active">
            <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><rect x="3" y="3" width="7" height="7"></rect><rect x="14" y="3" width="7" height="7"></rect><rect x="14" y="14" width="7" height="7"></rect><rect x="3" y="14" width="7" height="7"></rect></svg>
            Live View
          </a>
          <a href="#" className="nav-item">
            <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><circle cx="12" cy="12" r="10"></circle><polyline points="12 6 12 12 16 14"></polyline></svg>
            Nhật ký sự kiện
          </a>
          <a href="#" className="nav-item">
            <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><circle cx="12" cy="12" r="3"></circle><path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1 0 2.83 2 2 0 0 1-2.83 0l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-2 2 2 2 0 0 1-2-2v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83 0 2 2 0 0 1 0-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1-2-2 2 2 0 0 1 2-2h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 0-2.83 2 2 0 0 1 2.83 0l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 2-2 2 2 0 0 1 2 2v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 0 2 2 0 0 1 0 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 2 2 2 2 0 0 1-2 2h-.09a1.65 1.65 0 0 0-1.51 1z"></path></svg>
            Cài đặt
          </a>
        </nav>
      </div>
      <div className="sidebar-footer">
        <div className="host-info">
          <span>VQEC SIMULATOR</span>
        </div>
      </div>
    </aside>
  );
}

function MainContent({ mode, set_mode }) {
  const [filter_open, set_filter_open] = useState(false);
  const [editing, set_editing] = useState(null);
  const line = use_live_line();
  const valid_line = line.line.length === 2 &&
    (line.line[0][0] !== line.line[1][0] || line.line[0][1] !== line.line[1][1]);
  return (
    <main className="content">
      <div className="video-panel">
        <div className="panel-header">
          <div className="tabs">
            <button
              className={`tab${mode === 'raw' ? ' active' : ''}`}
              aria-pressed={mode === 'raw'}
              onClick={() => set_mode('raw')}
            >
              <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><path d="M23 7l-7 5 7 5V7z"></path><rect x="1" y="5" width="15" height="14" rx="2" ry="2"></rect></svg>
              Luồng Gốc (Raw Stream)
            </button>
            <button
              className={`tab${mode === 'ai' ? ' active' : ''}`}
              aria-pressed={mode === 'ai'}
              onClick={() => set_mode('ai')}
            >
              <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><path d="M12 2a2 2 0 0 1 2 2v2a2 2 0 0 1-2 2 2 2 0 0 1-2-2V4a2 2 0 0 1 2-2z"></path><path d="M12 8v14"></path><path d="M8 12h8"></path></svg>
              Luồng AI (AI Overlay View)
              <span className="badge">Person + Recognition</span>
            </button>
          </div>
          <div className="actions">
            <button className="action-btn" aria-expanded={filter_open} onClick={() => set_filter_open(value => !value)}>
              <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><line x1="4" y1="21" x2="4" y2="14"></line><line x1="4" y1="10" x2="4" y2="3"></line><line x1="12" y1="21" x2="12" y2="12"></line><line x1="12" y1="8" x2="12" y2="3"></line><line x1="20" y1="21" x2="20" y2="16"></line><line x1="20" y1="12" x2="20" y2="3"></line><line x1="1" y1="14" x2="7" y2="14"></line><line x1="9" y1="8" x2="15" y2="8"></line><line x1="17" y1="16" x2="23" y2="16"></line></svg>
              Bộ lọc đối tượng
            </button>
            {filter_open && <div className="object-filter-menu">
              <button className="action-btn" onClick={() => {
                set_mode('ai'); set_editing('line'); set_filter_open(false);
              }}>Vẽ line</button>
              <button className="action-btn" onClick={() => {
                set_mode('ai'); set_editing('zone'); set_filter_open(false);
              }}>Vẽ zone</button>
            </div>}
            <button className="icon-btn-square">
              <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><polyline points="15 3 21 3 21 9"></polyline><polyline points="9 21 3 21 3 15"></polyline><line x1="21" y1="3" x2="14" y2="10"></line><line x1="3" y1="21" x2="10" y2="14"></line></svg>
            </button>
          </div>
        </div>

        {editing === 'line' && mode === 'ai' && <div className="line-toolbar">
          <span>Chọn 2 điểm trên video, kéo A/B để chỉnh. Mũi tên chỉ chiều ra ngoài.</span>
          <div className="line-toolbar-actions">
            <button className="action-btn" disabled={!line.ready || line.busy || !valid_line}
              onClick={() => line.set_line([...line.line].reverse())}>Đảo chiều</button>
            <button className="action-btn" disabled={!line.ready || line.busy}
              onClick={() => line.set_line([])}>Vẽ lại</button>
            <button className="action-btn" disabled={!line.ready || line.busy || !valid_line}
              onClick={() => line.save()}>Lưu line</button>
            <button className="action-btn" disabled={!line.ready || line.busy}
              onClick={() => line.save([])}>Xóa line</button>
            <button className="action-btn" disabled={line.busy}
              onClick={() => { set_editing(null); line.set_line(line.saved); }}>Đóng</button>
          </div>
          <small>Bấm Lưu line để áp dụng. Line và zone chỉ lưu tạm; reload trang sẽ xóa cả hai.</small>
          {line.message && <span role="status">{line.message}</span>}
          {line.error && <div role="alert">{line.error}
            {!line.ready && <button className="action-btn" onClick={line.retry}>Thử lại line</button>}
          </div>}
        </div>}
        {editing === 'zone' && mode === 'ai' && <div className="line-toolbar">
          <span>Chọn 3–32 điểm. Sau điểm thứ 3, tiếp tục bấm vào vị trí trống để thêm điểm; kéo các đỉnh để chỉnh.</span>
          <span data-testid="zone-point-count">Đã chọn {line.zone.length}/32 điểm</span>
          <div className="line-toolbar-actions">
            <button className="action-btn" disabled={!line.ready || line.busy || !line.zone.length}
              onClick={() => line.set_zone(line.zone.slice(0, -1))}>Bỏ điểm cuối</button>
            <button className="action-btn" disabled={!line.ready || line.busy}
              onClick={() => line.set_zone([])}>Vẽ lại</button>
            <button className="action-btn" disabled={!line.ready || line.busy || line.zone.length < 3}
              onClick={() => line.save_zone()}>Lưu zone</button>
            <button className="action-btn" disabled={!line.ready || line.busy}
              onClick={() => line.save_zone([])}>Xóa zone</button>
            <button className="action-btn" disabled={line.busy}
              onClick={() => { set_editing(null); line.set_zone(line.saved_zone); }}>Đóng</button>
          </div>
          <small>Bấm Lưu zone để áp dụng. Bbox cam sau khi ở trong zone đủ 5 giây; bbox đỏ được ưu tiên. Reload sẽ xóa line và zone.</small>
          {line.message && <span role="status">{line.message}</span>}
          {line.error && <div role="alert">{line.error}
            {!line.ready && <button className="action-btn" onClick={line.retry}>Thử lại zone</button>}
          </div>}
        </div>}
        <div className="video-container">
          <LiveView key={mode} mode={mode} />
          {mode === 'ai' && editing !== 'zone' && line.saved_zone.length >= 3 &&
            <LiveLineEditor kind="zone" line={line.saved_zone} read_only />}
          {editing === 'line' && mode === 'ai' && <LiveLineEditor line={line.line} set_line={line.set_line}
            disabled={!line.ready || line.busy} />}
          {editing === 'zone' && mode === 'ai' && <LiveLineEditor kind="zone" line={line.zone}
            set_line={line.set_zone} disabled={!line.ready || line.busy} />}

          <div className="video-overlay">
            <div className="corner-tl"></div>
            <div className="corner-tr"></div>
            <div className="corner-bl"></div>
            <div className="corner-br"></div>
          </div>
        </div>

        <div className="panel-footer">
          <div className="camera-selector">
            <button className="selector-btn">
              <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><path d="M23 7l-7 5 7 5V7z"></path><rect x="1" y="5" width="15" height="14" rx="2" ry="2"></rect></svg>
              Board 43 — Luồng HD
              <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" className="chevron"><polyline points="6 9 12 15 18 9"></polyline></svg>
            </button>
          </div>
          <div className="frame-stats">
            <span className="highlight-cyan">{mode === 'ai' ? 'Luồng AI · Person + Recognition' : 'Luồng Gốc'}</span>
          </div>
        </div>
      </div>
    </main>
  );
}

export default function App() {
  const [mode, set_mode] = useState('raw');
  return (
    <div className="app-container">
      <Topbar mode={mode} />
      <div className="main-layout">
        <Sidebar />
        <MainContent mode={mode} set_mode={set_mode} />
      </div>
    </div>
  );
}
