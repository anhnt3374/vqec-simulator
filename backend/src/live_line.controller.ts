import { Body, Controller, Param, Post, Put } from '@nestjs/common';
import { LiveLineService, type LiveLineDto } from './live_line.service.js';

@Controller('streams')
export class LiveLineController {
  constructor(private readonly live_line: LiveLineService) {}

  @Post(':stream_id/live-line/session')
  start_session(@Param('stream_id') stream_id: string, @Body() body: unknown): Promise<LiveLineDto> {
    return this.live_line.start_session(stream_id, body);
  }

  @Put(':stream_id/live-line')
  save_line(@Param('stream_id') stream_id: string, @Body() body: unknown): Promise<LiveLineDto> {
    return this.live_line.save_line(stream_id, body);
  }

  @Put(':stream_id/live-zone')
  save_zone(@Param('stream_id') stream_id: string, @Body() body: unknown): Promise<LiveLineDto> {
    return this.live_line.save_zone(stream_id, body);
  }

}
