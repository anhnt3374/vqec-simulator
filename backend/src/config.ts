export interface AppConfig {
  port: number;
  player_base_url: string;
  cors_origins: string[];
}

export const APP_CONFIG = Symbol('APP_CONFIG');

function parse_http_url(value: string, key: string): URL {
  let url: URL;
  try { url = new URL(value); }
  catch { throw new Error(`${key}: expected an absolute HTTP(S) URL`); }
  if (!['http:', 'https:'].includes(url.protocol) || url.username || url.password ||
      url.search || url.hash) {
    throw new Error(`${key}: expected HTTP(S) without credentials, query or fragment`);
  }
  return url;
}

export function load_config(env: NodeJS.ProcessEnv): AppConfig {
  const raw_port = env.PORT ?? '3000';
  const port = Number(raw_port);
  if (!/^\d+$/.test(raw_port) || !Number.isInteger(port) || port < 1 || port > 65535) {
    throw new Error('PORT: expected an integer from 1 to 65535');
  }
  const base = parse_http_url(env.MEDIAMTX_PUBLIC_URL ?? '', 'MEDIAMTX_PUBLIC_URL');
  const cors_origins = (env.WEB_ORIGINS ?? '').split(',').map(value => value.trim()).filter(Boolean)
    .map(value => {
      const url = parse_http_url(value, 'WEB_ORIGINS');
      if (url.pathname !== '/') throw new Error('WEB_ORIGINS: expected HTTP(S) origins without paths');
      return url.origin;
    });
  return { port, player_base_url: base.href.replace(/\/+$/, ''), cors_origins };
}
