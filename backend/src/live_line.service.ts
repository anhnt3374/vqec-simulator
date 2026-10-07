import { BadRequestException, ConflictException, Inject, Injectable, Logger, NotFoundException, type OnModuleInit } from '@nestjs/common';
import { randomUUID } from 'node:crypto';
import { mkdir, rename, writeFile } from 'node:fs/promises';
import { dirname } from 'node:path';
import { parse_zone, type LiveZone } from './zone_validation.js';
import { APP_CONFIG, type AppConfig } from './config.js';

type Point = [number, number];
export type LiveLine = [Point, Point];
export interface LiveLineDto {
  session_id: string | null; revision: string; line_revision: string; zone_revision: string;
  line: LiveLine | null; zone: LiveZone | null;
}

function session_id(value: unknown): string {
  if (typeof value !== 'string' || !/^[A-Za-z0-9_-]{1,128}$/.test(value)) {
    throw new BadRequestException('session_id must contain 1..128 letters, digits, underscores or hyphens');
  }
  return value;
}

function parse_line(value: unknown): LiveLine | null {
  if (value === null) return null;
  if (!Array.isArray(value) || value.length !== 2 || value.some(point =>
    !Array.isArray(point) || point.length !== 2 || point.some(coordinate =>
      typeof coordinate !== 'number' || !Number.isFinite(coordinate) || coordinate < 0 || coordinate > 1))) {
    throw new BadRequestException('line must contain two normalized [x,y] endpoints or null');
  }
  const line = value as LiveLine;
  if (line[0][0] === line[1][0] && line[0][1] === line[1][1]) {
    throw new BadRequestException('line endpoints must differ');
  }
  return [[...line[0]], [...line[1]]];
}

// One active page owns the stream. Serialize asynchronous writes so a stale request cannot
// overwrite a newer session. The file is an atomic IPC snapshot on a shared tmpfs, not a DB.
@Injectable()
export class LiveLineService implements OnModuleInit {
  private readonly logger = new Logger('LiveLine');
  private state: LiveLineDto = { session_id: null, revision: randomUUID(),
    line_revision: randomUUID(), zone_revision: randomUUID(), line: null, zone: null };
  private pending: Promise<unknown> = Promise.resolve();
  constructor(@Inject(APP_CONFIG) private readonly config: AppConfig) {}

  async onModuleInit(): Promise<void> {
    await this.publish(this.state);
    this.logger.log('[stream=board43] temporary line and zone reset on backend startup');
  }

  private async publish(next: LiveLineDto): Promise<void> {
    const file = this.config.live_line_file;
    if (file) {
      await mkdir(dirname(file), { recursive: true });
      await writeFile(`${file}.next`, JSON.stringify(next), { mode: 0o644 });
      await rename(`${file}.next`, file);
    }
    this.state = next;
  }

  private serialize<T>(action: () => Promise<T>): Promise<T> {
    const result = this.pending.then(action);
    this.pending = result.catch(() => {});
    return result;
  }

  private check_stream(stream: string): void {
    if (stream !== 'board43') throw new NotFoundException(`stream '${stream}' does not exist`);
  }

  start_session(stream: string, body: unknown): Promise<LiveLineDto> {
    this.check_stream(stream);
    const id = session_id((body as { session_id?: unknown } | null)?.session_id);
    return this.serialize(async () => {
      if (this.state.session_id !== id) {
        await this.publish({ session_id: id, revision: randomUUID(),
          line_revision: randomUUID(), zone_revision: randomUUID(), line: null, zone: null });
        this.logger.log('[stream=board43] temporary line and zone reset for new page session');
      }
      return structuredClone(this.state);
    });
  }

  save_line(stream: string, body: unknown): Promise<LiveLineDto> {
    this.check_stream(stream);
    const dto = body as { session_id?: unknown; line?: unknown } | null;
    const id = session_id(dto?.session_id);
    const line = parse_line(dto?.line);
    return this.serialize(async () => {
      if (this.state.session_id !== id) throw new ConflictException('Live line belongs to a newer page; reload this page');
      await this.publish({ ...this.state, revision: randomUUID(), line_revision: randomUUID(), line });
      this.logger.log(`[stream=board43] temporary line ${line ? 'saved' : 'cleared'} gate=live_line`);
      return structuredClone(this.state);
    });
  }

  save_zone(stream: string, body: unknown): Promise<LiveLineDto> {
    this.check_stream(stream);
    const dto = body as { session_id?: unknown; zone?: unknown } | null;
    const id = session_id(dto?.session_id);
    const zone = parse_zone(dto?.zone);
    return this.serialize(async () => {
      if (this.state.session_id !== id) throw new ConflictException('Live zone belongs to a newer page; reload this page');
      await this.publish({ ...this.state, revision: randomUUID(), zone_revision: randomUUID(), zone });
      this.logger.log(`[stream=board43] temporary zone ${zone ? 'saved' : 'cleared'} zone=live_zone vertices=${zone?.length ?? 0}`);
      return structuredClone(this.state);
    });
  }

}
