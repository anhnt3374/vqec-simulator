import { BadRequestException, Inject, Injectable, NotFoundException } from '@nestjs/common';
import { APP_CONFIG, type AppConfig } from './config.js';

export interface LiveViewDto {
  stream_id: string;
  mode: 'raw' | 'ai';
  protocol: 'webrtc';
  player_url: string;
}

@Injectable()
export class LiveViewService {
  constructor(@Inject(APP_CONFIG) private readonly config: AppConfig) {}

  get_live_view(stream_id: string, mode: unknown = 'raw'): LiveViewDto {
    if (stream_id !== 'board43') throw new NotFoundException(`stream '${stream_id}' does not exist`);
    if (mode !== 'raw' && mode !== 'ai') throw new BadRequestException('mode must be raw or ai');
    return {
      stream_id, mode, protocol: 'webrtc',
      player_url: `${this.config.player_base_url}/${mode === 'ai' ? 'ai' : 'relay'}`,
    };
  }
}
