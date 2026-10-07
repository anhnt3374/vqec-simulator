import { Logger } from '@nestjs/common';
import type { ServerResponse } from 'node:http';
import { HTTP_FAILURE, error_code } from './http_error.filter.js';

const logger = new Logger('HTTP');

// Runs before routing so rejected/unregistered routes also produce access lines.
export function log_http_request(
  request: { method: string; path: string; originalUrl?: string },
  response: ServerResponse & { [HTTP_FAILURE]?: string },
  next: () => void,
): void {
  const started = performance.now();
  const method = request.method;
  const path = (request.originalUrl ?? request.path).split('?')[0];
  response.once('finish', () => {
    const line = `[http] ${method} ${path} status=${response.statusCode} duration_ms=${(performance.now() - started).toFixed(1)}`;
    if (response.statusCode >= 500) logger.error(`${line} code=${error_code(response.statusCode)} ${response[HTTP_FAILURE] ?? 'request failed'}`);
    else if (response.statusCode >= 400) logger.warn(`${line} code=${error_code(response.statusCode)} input rejected`);
    else logger.debug(line);
  });
  next();
}
