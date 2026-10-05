// Live event list fed by the WebSocket endpoint /ws/v1/events. Reconnects with backoff.

const MAX_ITEMS = 200;
const RECONNECT_MIN_MS = 1000;
const RECONNECT_MAX_MS = 30000;

export function startEventFeed(listElement, onStatus) {
  let delay = RECONNECT_MIN_MS;

  const connect = () => {
    const scheme = location.protocol === 'https:' ? 'wss' : 'ws';
    const socket = new WebSocket(`${scheme}://${location.host}/ws/v1/events`);

    socket.addEventListener('open', () => {
      delay = RECONNECT_MIN_MS;
      onStatus('live');
    });
    socket.addEventListener('message', (message) => {
      try {
        prepend(listElement, JSON.parse(message.data));
      } catch (error) {
        console.error('[event_feed] bad message', error);
      }
    });
    socket.addEventListener('close', () => {
      onStatus(`reconnecting in ${Math.round(delay / 1000)} s`);
      setTimeout(connect, delay);
      delay = Math.min(delay * 2, RECONNECT_MAX_MS);
    });
  };

  connect();
}

function prepend(listElement, event) {
  const item = document.createElement('li');
  const where = event.zone_id ?? event.gate_id ?? '';
  item.textContent = `${event.occurred_at} ${event.stream_id} ${event.type} ${where}`.trim();
  listElement.prepend(item);
  while (listElement.children.length > MAX_ITEMS) {
    listElement.lastElementChild.remove();
  }
}
