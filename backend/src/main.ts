import { Logger } from '@nestjs/common';
import { create_app } from './app.module.js';
import { load_config } from './config.js';

const logger = new Logger('Bootstrap');
try {
  const config = load_config(process.env);
  const app = await create_app(config);
  app.enableShutdownHooks();
  process.once('SIGTERM', () => logger.log('[backend] shutdown requested signal=SIGTERM'));
  process.once('SIGINT', () => logger.log('[backend] shutdown requested signal=SIGINT'));
  await app.listen(config.port, '0.0.0.0');
  logger.log(`[backend] vqec-web-api 0.1.0 listening address=0.0.0.0 port=${config.port} stream=board43 mode=raw`);
} catch (error) {
  logger.error(`[backend] startup failed code=invalid_configuration ${error instanceof Error ? error.message : 'unknown error'}`);
  process.exitCode = 1;
}
