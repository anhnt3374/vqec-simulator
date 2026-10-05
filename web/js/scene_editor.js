// Draws and edits zones (polygons) and gates (lines) over the video.
// All coordinates are normalized [0, 1] relative to the frame, matching the backend contract.

const VERTEX_RADIUS_PX = 4;
const GATE_ARROW_PX = 14;

export class SceneEditor {
  constructor(canvas, onChange) {
    this.canvas = canvas;
    this.context = canvas.getContext('2d');
    this.onChange = onChange;
    this.scene = { zones: [], gates: [] };
    this.tool = null;
    this.draft = [];

    canvas.addEventListener('click', (event) => this.handleClick(event));
    canvas.addEventListener('dblclick', () => this.finishZone());
    new ResizeObserver(() => this.render()).observe(canvas);
  }

  setScene(scene) {
    this.scene = structuredClone(scene);
    this.render();
  }

  setTool(tool) {
    this.tool = tool;
    this.draft = [];
    this.render();
  }

  handleClick(event) {
    if (!this.tool) {
      return;
    }
    const rect = this.canvas.getBoundingClientRect();
    this.draft.push([(event.clientX - rect.left) / rect.width, (event.clientY - rect.top) / rect.height]);
    if (this.tool === 'gate' && this.draft.length === 2) {
      this.scene.gates.push({ id: nextId('gate', this.scene.gates), line: this.draft });
      this.commit();
    }
    this.render();
  }

  finishZone() {
    if (this.tool === 'zone' && this.draft.length >= 3) {
      this.scene.zones.push({ id: nextId('zone', this.scene.zones), polygon: this.draft });
      this.commit();
    }
  }

  commit() {
    this.draft = [];
    this.tool = null;
    this.onChange(structuredClone(this.scene));
    this.render();
  }

  render() {
    const { canvas, context } = this;
    canvas.width = canvas.clientWidth;
    canvas.height = canvas.clientHeight;
    context.clearRect(0, 0, canvas.width, canvas.height);
    const toPx = ([x, y]) => [x * canvas.width, y * canvas.height];
    const style = getComputedStyle(document.documentElement);

    context.fillStyle = style.getPropertyValue('--zone');
    for (const zone of this.scene.zones) {
      context.beginPath();
      zone.polygon.map(toPx).forEach(([x, y], i) => (i ? context.lineTo(x, y) : context.moveTo(x, y)));
      context.closePath();
      context.fill();
    }

    context.strokeStyle = style.getPropertyValue('--gate');
    context.lineWidth = 2;
    for (const gate of this.scene.gates) {
      drawGate(context, toPx(gate.line[0]), toPx(gate.line[1]));
    }

    context.fillStyle = '#fff';
    for (const [x, y] of this.draft.map(toPx)) {
      context.beginPath();
      context.arc(x, y, VERTEX_RADIUS_PX, 0, 2 * Math.PI);
      context.fill();
    }
  }
}

// Line plus an arrow showing the "forward" direction: the right-hand side of start -> end.
function drawGate(context, [x1, y1], [x2, y2]) {
  context.beginPath();
  context.moveTo(x1, y1);
  context.lineTo(x2, y2);
  const midX = (x1 + x2) / 2;
  const midY = (y1 + y2) / 2;
  const length = Math.hypot(x2 - x1, y2 - y1) || 1;
  const normalX = -(y2 - y1) / length;
  const normalY = (x2 - x1) / length;
  context.moveTo(midX, midY);
  context.lineTo(midX + normalX * GATE_ARROW_PX, midY + normalY * GATE_ARROW_PX);
  context.stroke();
}

function nextId(prefix, items) {
  let index = items.length + 1;
  while (items.some((item) => item.id === `${prefix}_${index}`)) {
    index += 1;
  }
  return `${prefix}_${index}`;
}
