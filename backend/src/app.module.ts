import 'reflect-metadata';
import { Module, type DynamicModule, type INestApplication } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import { APP_CONFIG, type AppConfig } from './config.js';
import { LiveViewService } from './live_view.service.js';
import { LiveViewController } from './live_view.controller.js';
import { LiveLineController } from './live_line.controller.js';
import { LiveLineService } from './live_line.service.js';
import { HealthController } from './health.controller.js';
import { HttpErrorFilter } from './http_error.filter.js';
import { log_http_request } from './http_logging.middleware.js';

@Module({})
class AppModule {
  static register(config: AppConfig): DynamicModule {
    return {
      module: AppModule,
      controllers: [HealthController, LiveViewController, LiveLineController],
      providers: [{ provide: APP_CONFIG, useValue: config }, LiveViewService, LiveLineService],
    };
  }
}

export async function create_app(config: AppConfig): Promise<INestApplication> {
  const app = await NestFactory.create(AppModule.register(config));
  app.setGlobalPrefix('api/v1');
  app.use(log_http_request);
  app.enableCors({ origin: config.cors_origins, methods: ['GET', 'POST', 'PUT'] });
  app.useGlobalFilters(new HttpErrorFilter());
  return app;
}
