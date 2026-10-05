// Thin wrapper over the REST API (docs/rest_api.md). Every call goes through request() so errors
// are reported in one consistent way.

const API_BASE = '/api/v1';

export class ApiError extends Error {
  constructor(status, code, message) {
    super(message);
    this.status = status;
    this.code = code;
  }
}

async function request(method, path, body) {
  const options = { method, headers: { Accept: 'application/json' } };
  if (body !== undefined) {
    options.headers['Content-Type'] = 'application/json';
    options.body = JSON.stringify(body);
  }
  const response = await fetch(`${API_BASE}${path}`, options);
  const payload = response.status === 204 ? null : await response.json().catch(() => null);
  if (!response.ok) {
    const error = payload?.error ?? {};
    throw new ApiError(response.status, error.code ?? 'unknown', error.message ?? response.statusText);
  }
  return payload;
}

export const api = {
  systemInfo: () => request('GET', '/system/info'),
  listStreams: () => request('GET', '/streams'),
  getScene: (streamId) => request('GET', `/streams/${encodeURIComponent(streamId)}/scene`),
  putScene: (streamId, scene) => request('PUT', `/streams/${encodeURIComponent(streamId)}/scene`, scene),
  listEvents: (params) => request('GET', `/events?${new URLSearchParams(params)}`),
  reportClientError: (details) => request('POST', '/client-errors', details),
};
