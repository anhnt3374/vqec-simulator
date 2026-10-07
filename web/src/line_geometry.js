export const AI_FRAME_ASPECT_RATIO = Number(import.meta.env?.VITE_AI_ASPECT_RATIO ?? 16 / 9);

export function video_rect(width, height, aspect = AI_FRAME_ASPECT_RATIO) {
  const content_width = Math.min(width, height * aspect);
  const content_height = content_width / aspect;
  return { left: (width - content_width) / 2, top: (height - content_height) / 2,
    width: content_width, height: content_height };
}

export function normalized_pointer(x, y, rect, clamp = false) {
  if (rect.width <= 0 || rect.height <= 0) return null;
  const point = [(x - rect.left) / rect.width, (y - rect.top) / rect.height];
  if (!clamp && point.some(value => value < 0 || value > 1)) return null;
  return point.map(value => Math.max(0, Math.min(1, value)));
}

export function outward_arrow(line, width, height) {
  if (line.length !== 2) return null;
  const start = [(line[0][0] + line[1][0]) * width / 2, (line[0][1] + line[1][1]) * height / 2];
  const dx = (line[1][0] - line[0][0]) * width;
  const dy = (line[1][1] - line[0][1]) * height;
  const length = Math.hypot(dx, dy);
  if (length === 0) return null;
  const arrow_length = Math.min(width, height) * 0.08;
  return { start, end: [start[0] - dy / length * arrow_length, start[1] + dx / length * arrow_length] };
}
