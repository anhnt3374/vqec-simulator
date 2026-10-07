import { Catch, HttpException, type ArgumentsHost, type ExceptionFilter } from '@nestjs/common';

export const HTTP_FAILURE = Symbol('HTTP_FAILURE');

export function error_code(status: number): string {
  const codes: Record<number, string> = {
    400: 'invalid_argument', 401: 'unauthorized', 403: 'forbidden',
    404: 'not_found', 409: 'already_exists', 503: 'unavailable', 504: 'timeout',
  };
  return codes[status] ?? 'internal';
}

@Catch()
export class HttpErrorFilter implements ExceptionFilter {
  catch(exception: unknown, host: ArgumentsHost): void {
    const status = exception instanceof HttpException ? exception.getStatus() : 500;
    const message = status >= 500 ? 'Internal server error' :
      exception instanceof HttpException ? exception.message : 'Request failed';
    const response = host.switchToHttp().getResponse();
    if (status >= 500) {
      // Keep diagnostic type/source location, never the arbitrary error message:
      // messages from dependencies can contain credentials and tokens.
      const cause = exception instanceof Error ? exception.constructor.name : typeof exception;
      const location = exception instanceof Error
        ? exception.stack?.match(/(?:file:\/\/)?((?:\/app\/|\/workspace\/backend\/)[A-Za-z0-9_./-]+:\d+:\d+)/)?.[1]
        : undefined;
      response[HTTP_FAILURE] = `cause=${/^[A-Za-z_][A-Za-z0-9_]*$/.test(cause) ? cause : 'Error'} location=${location ?? 'unknown'}`;
    }
    response.status(status).json({
      error: { code: error_code(status), message },
    });
  }
}
