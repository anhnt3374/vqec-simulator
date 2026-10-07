import { Controller, Get, Param, Query } from '@nestjs/common';
import { LiveViewService, type LiveViewDto } from './live_view.service.js';

@Controller('streams')
export class LiveViewController {
  constructor(private readonly live_view: LiveViewService) {}

  @Get(':stream_id/live-view')
  get_live_view(@Param('stream_id') stream_id: string, @Query('mode') mode?: string | string[]): LiveViewDto {
    return this.live_view.get_live_view(stream_id, mode);
  }
}
