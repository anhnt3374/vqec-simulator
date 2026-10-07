import assert from 'node:assert/strict';
import { test } from 'node:test';
import type { ArgumentsHost } from '@nestjs/common';
import { HttpErrorFilter } from '../src/http_error.filter.js';

test('unexpected server errors hide internal secrets and use the error contract', () => {
  let status: number | undefined;
  let body: unknown;
  const response = {
    status(value: number) { status = value; return this; },
    json(value: unknown) { body = value; },
  };
  const host = { switchToHttp: () => ({ getResponse: () => response }) } as ArgumentsHost;
  new HttpErrorFilter().catch(new Error('private-stack-and-secret'), host);
  assert.equal(status, 500);
  assert.deepEqual(body, { error: { code: 'internal', message: 'Internal server error' } });
});
