// Entry point: wires the API client, stream list, live view, scene editor and event feed.

import { api } from './api_client.js';
import { startEventFeed } from './event_feed.js';
import { SceneEditor } from './scene_editor.js';

// MediaMTX serves the annotated stream "<stream_id>" over WebRTC on this port (docker/compose.yaml).
const MEDIAMTX_WEBRTC_PORT = 8889;

const state = { streamId: null, pendingScene: null };

function reportError(context, error) {
  console.error(`[${context}]`, error);
  api.reportClientError({ context, message: String(error?.message ?? error) }).catch(() => {});
}

window.addEventListener('error', (event) => reportError('window', event.error ?? event.message));
window.addEventListener('unhandledrejection', (event) => reportError('promise', event.reason));

document.addEventListener('DOMContentLoaded', async () => {
  const editor = new SceneEditor(document.getElementById('scene-canvas'), (scene) => {
    state.pendingScene = scene;
    document.getElementById('save-scene').disabled = false;
  });

  document.querySelectorAll('[data-tool]').forEach((button) =>
    button.addEventListener('click', () => editor.setTool(button.dataset.tool)));

  document.getElementById('save-scene').addEventListener('click', async (event) => {
    try {
      await api.putScene(state.streamId, state.pendingScene);
      event.target.disabled = true;
    } catch (error) {
      reportError('save_scene', error);
      alert(`Saving the scene failed: ${error.message}`);
    }
  });

  startEventFeed(document.getElementById('event-feed'), (status) => {
    document.getElementById('system-info').dataset.feed = status;
  });

  try {
    const info = await api.systemInfo();
    document.getElementById('system-info').textContent =
      `v${info.version} · runtime ${info.runtime} · backend ${info.backend}`;
    await loadStreams(editor);
  } catch (error) {
    reportError('startup', error);
    document.getElementById('system-info').textContent = `backend unavailable: ${error.message}`;
  }
});

async function loadStreams(editor) {
  const { streams } = await api.listStreams();
  const list = document.getElementById('stream-list');
  list.replaceChildren();
  for (const stream of streams) {
    const item = document.createElement('li');
    item.textContent = `${stream.id} (${stream.state})`;
    item.addEventListener('click', () => selectStream(stream.id, item, editor));
    list.append(item);
  }
}

async function selectStream(streamId, item, editor) {
  state.streamId = streamId;
  document.querySelectorAll('#stream-list li').forEach((li) => li.setAttribute('aria-selected', li === item));
  document.getElementById('viewer-title').textContent = streamId;

  const liveView = document.getElementById('live-view');
  liveView.src = `${location.protocol}//${location.hostname}:${MEDIAMTX_WEBRTC_PORT}/${encodeURIComponent(streamId)}/`;
  liveView.hidden = false;

  try {
    editor.setScene(await api.getScene(streamId));
  } catch (error) {
    reportError('load_scene', error);
  }
}
