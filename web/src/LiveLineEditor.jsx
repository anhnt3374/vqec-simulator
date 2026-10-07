import { useEffect, useRef, useState } from 'react';
import { normalized_pointer, outward_arrow, video_rect } from './line_geometry.js';

const MAX_ZONE_VERTICES = 32;

export default function LiveLineEditor({ line, set_line, disabled, kind = 'line', read_only = false }) {
  const is_zone = kind === 'zone';
  const overlay = useRef(null);
  const dragging = useRef(null);
  const [rect, set_rect] = useState({ left: 0, top: 0, width: 1, height: 1 });
  useEffect(() => {
    const observer = new ResizeObserver(entries => {
      const { width, height } = entries[0].contentRect;
      set_rect(video_rect(width, height));
    });
    observer.observe(overlay.current);
    return () => observer.disconnect();
  }, []);
  const get_point = (event, clamp = false) => {
    const area = overlay.current.getBoundingClientRect();
    return normalized_pointer(event.clientX - area.left, event.clientY - area.top, rect, clamp);
  };
  const pointer_down = event => {
    if (read_only || disabled || event.button !== 0) return;
    const point = get_point(event);
    if (!point) return;
    const handle = event.target.closest('[data-endpoint]');
    if (handle) {
      dragging.current = Number(handle.dataset.endpoint);
      event.currentTarget.setPointerCapture(event.pointerId);
    } else if (line.length < (is_zone ? MAX_ZONE_VERTICES : 2)) {
      set_line([...line, point]);
    }
  };
  const arrow = is_zone ? null : outward_arrow(line, rect.width, rect.height);
  const pixel = point => [point[0] * rect.width, point[1] * rect.height];
  return (
    <div ref={overlay} className="line-editor-layer" data-testid={read_only ? 'zone-overlay' : is_zone ? 'zone-editor' : 'line-editor'}>
      <svg style={{ left: rect.left, top: rect.top, width: rect.width, height: rect.height,
          pointerEvents: read_only ? 'none' : undefined, cursor: read_only ? 'default' : undefined }}
        viewBox={`0 0 ${rect.width} ${rect.height}`} role="img" aria-label={is_zone ? 'Vùng vẽ zone trên video AI' : 'Vùng vẽ line trên video AI'}
        onPointerDown={pointer_down} onPointerMove={event => {
          if (read_only || dragging.current === null || disabled) return;
          const point = get_point(event, true);
          if (point) set_line(line.map((value, index) => index === dragging.current ? point : value));
        }} onPointerUp={() => { dragging.current = null; }}
        onPointerCancel={() => { dragging.current = null; }}>
        {!is_zone && <defs><marker id="outward-arrow" viewBox="0 0 10 10" refX="9" refY="5"
          markerWidth="7" markerHeight="7" orient="auto-start-reverse">
          <path d="M 0 0 L 10 5 L 0 10 z" fill="#ffce54" />
        </marker></defs>}
        {is_zone && line.length > 0 && (line.length >= 3
          ? <polygon points={line.map(point => pixel(point).join(',')).join(' ')}
            fill="rgba(255, 165, 0, 0.15)" stroke="#ffa500" strokeWidth="3" strokeDasharray="8 4" />
          : <polyline points={line.map(point => pixel(point).join(',')).join(' ')}
            fill="none" stroke="#ffa500" strokeWidth="3" />)}
        {!is_zone && line.length === 2 && <line x1={pixel(line[0])[0]} y1={pixel(line[0])[1]}
          x2={pixel(line[1])[0]} y2={pixel(line[1])[1]} stroke="#ffce54" strokeWidth="3" strokeDasharray="8 4" />}
        {arrow && <><line x1={arrow.start[0]} y1={arrow.start[1]} x2={arrow.end[0]} y2={arrow.end[1]}
          stroke="#ffce54" strokeWidth="3" markerEnd="url(#outward-arrow)" />
          <text x={arrow.end[0] + 10} y={arrow.end[1]} fill="#ffce54" fontSize="15">Ra ngoài</text></>}
        {!read_only && line.map((point, index) => <g key={index} data-endpoint={index}>
          <circle cx={pixel(point)[0]} cy={pixel(point)[1]} r="10" fill={is_zone ? '#ffa500' : '#ffce54'} stroke="#111" strokeWidth="2" />
          <text x={pixel(point)[0] + 14} y={pixel(point)[1] - 12} fill={is_zone ? '#ffa500' : '#ffce54'} fontSize="14">{is_zone ? index + 1 : index === 0 ? 'A' : 'B'}</text>
        </g>)}
      </svg>
    </div>
  );
}
