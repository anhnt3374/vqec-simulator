import { BadRequestException } from '@nestjs/common';

export type Point = [number, number];
export type LiveZone = Point[];
const MAX_ZONE_VERTICES = 32;
const GEOMETRY_EPSILON = 1e-7;

function orientation(a: Point, b: Point, c: Point): number {
  return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
}
function on_segment(a: Point, b: Point, p: Point): boolean {
  return Math.abs(orientation(a, b, p)) <= GEOMETRY_EPSILON &&
    p[0] >= Math.min(a[0], b[0]) - GEOMETRY_EPSILON && p[0] <= Math.max(a[0], b[0]) + GEOMETRY_EPSILON &&
    p[1] >= Math.min(a[1], b[1]) - GEOMETRY_EPSILON && p[1] <= Math.max(a[1], b[1]) + GEOMETRY_EPSILON;
}
function intersects(a: Point, b: Point, c: Point, d: Point): boolean {
  const o1 = orientation(a, b, c), o2 = orientation(a, b, d);
  const o3 = orientation(c, d, a), o4 = orientation(c, d, b);
  return (o1 * o2 < 0 && o3 * o4 < 0) || on_segment(a, b, c) || on_segment(a, b, d) ||
    on_segment(c, d, a) || on_segment(c, d, b);
}
export function parse_zone(value: unknown): LiveZone | null {
  if (value === null) return null;
  if (!Array.isArray(value) || value.length < 3 || value.length > MAX_ZONE_VERTICES || value.some(point =>
    !Array.isArray(point) || point.length !== 2 || point.some(coordinate =>
      typeof coordinate !== 'number' || !Number.isFinite(coordinate) || coordinate < 0 || coordinate > 1))) {
    throw new BadRequestException('zone must contain 3..32 normalized [x,y] vertices or null');
  }
  const zone = value as LiveZone;
  let twice_area = 0;
  for (let i = 0; i < zone.length; ++i) {
    const a = zone[i], b = zone[(i + 1) % zone.length];
    twice_area += a[0] * b[1] - b[0] * a[1];
    for (let j = i + 1; j < zone.length; ++j) {
      if (Math.hypot(a[0] - zone[j][0], a[1] - zone[j][1]) <= GEOMETRY_EPSILON) {
        throw new BadRequestException('zone vertices must differ');
      }
      if (j === i + 1 || (i === 0 && j === zone.length - 1)) continue;
      if (intersects(a, b, zone[j], zone[(j + 1) % zone.length])) {
        throw new BadRequestException('zone edges must not intersect');
      }
    }
    // Adjacent collinear edges that double back overlap and do not define a simple polygon.
    const c = zone[(i + 2) % zone.length];
    if (on_segment(a, b, c) || on_segment(b, c, a)) {
      throw new BadRequestException('zone edges must not overlap');
    }
  }
  if (Math.abs(twice_area) <= GEOMETRY_EPSILON) throw new BadRequestException('zone must have nonzero area');
  return zone.map(point => [...point]);
}
