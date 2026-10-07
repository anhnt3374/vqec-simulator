import { Inject, Injectable, NotFoundException } from '@nestjs/common';
import { APP_CONFIG, type AppConfig } from './config.js';

export interface LiveViewDto {
  stream_id: string;
  mode: 'raw';
  protocol: 'webrtc';
  player_url: string;
}

@Injectable()
export class LiveViewService {
  constructor(@Inject(APP_CONFIG) private readonly config: AppConfig) {}

  get_live_view(stream_id: string): LiveViewDto {
    if (stream_id !== 'board43') throw new NotFoundException(`stream '${stream_id}' does not exist`);
    return {
      stream_id, mode: 'raw', protocol: 'webrtc',
      player_url: `${this.config.player_base_url}/relay`,
    };
  }
}
