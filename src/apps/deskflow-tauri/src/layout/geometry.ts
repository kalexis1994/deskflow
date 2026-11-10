// Pure geometry for the free-form screen layout. Mirrors src-tauri/src/layout.rs,
// which turns the same rectangles into the links the core uses.

export interface Rect {
  x: number;
  y: number;
  w: number;
  h: number;
}

/** A monitor: x, y, width and height in pixels, in the coordinates of its computer's whole desktop. */
export type Monitor = [number, number, number, number];

export interface Screen extends Rect {
  name: string;
  /** The computer's monitors as it last reported them; the rectangle stands for the box around them. */
  monitors?: Monitor[];
}

export const DEFAULT_W = 160;
export const DEFAULT_H = 90;
export const MIN_SIZE = 40;
/** Edges closer than this (layout units) count as touching. Same as the backend. */
export const EPS = 0.5;

export const right = (r: Rect) => r.x + r.w;
export const bottom = (r: Rect) => r.y + r.h;

export function overlaps(a: Rect, b: Rect): boolean {
  return a.x < right(b) - EPS && b.x < right(a) - EPS && a.y < bottom(b) - EPS && b.y < bottom(a) - EPS;
}

export function overlapsAny(r: Rect, others: Rect[]): boolean {
  return others.some((o) => overlaps(r, o));
}

export type Side = "right" | "left" | "up" | "down";

/** A shared stretch of edge between two screens, in layout coordinates. */
export interface Contact {
  a: string;
  b: string;
  /** "v" = vertical segment (a's right touches b's left), "h" = horizontal (a's bottom touches b's top). */
  orientation: "v" | "h";
  x1: number;
  y1: number;
  x2: number;
  y2: number;
}

/** The box around a computer's monitors, in pixels; null without any. */
export function monitorBounds(monitors: Monitor[] | undefined): Rect | null {
  if (!monitors?.length) return null;
  const x = Math.min(...monitors.map((m) => m[0]));
  const y = Math.min(...monitors.map((m) => m[1]));
  const w = Math.max(...monitors.map((m) => m[0] + m[2])) - x;
  const h = Math.max(...monitors.map((m) => m[1] + m[3])) - y;
  return w > 0 && h > 0 ? { x, y, w, h } : null;
}

/** Width over height of a computer's monitors together; null when they aren't known. */
export function realRatio(s: Screen): number | null {
  const b = monitorBounds(s.monitors);
  return b ? b.w / b.h : null;
}

/** Where each monitor sits in its screen, as fractions of the screen (0 to 1). */
export function monitorRects(s: Screen): Rect[] {
  const b = monitorBounds(s.monitors);
  if (!b) return [];
  return s.monitors!.map((m) => ({ x: (m[0] - b.x) / b.w, y: (m[1] - b.y) / b.h, w: m[2] / b.w, h: m[3] / b.h }));
}

/** The screen with these monitors, its height following their proportion. */
export function withMonitors<T extends Screen>(s: T, monitors: Monitor[]): T {
  const b = monitorBounds(monitors);
  if (!b) return { ...s, monitors };
  return { ...s, monitors, h: Math.max(MIN_SIZE, Math.round(s.w / (b.w / b.h))) };
}

/**
 * The parts of a side that have a monitor on them, along the side in layout units
 * (y for right and left, x for up and down). All of it when the monitors aren't known.
 * Same as `covered` in src-tauri/src/layout.rs.
 */
export function covered(s: Screen, side: Side): [number, number][] {
  const vertical = side === "right" || side === "left";
  const whole: [number, number] = vertical ? [s.y, bottom(s)] : [s.x, right(s)];
  const b = monitorBounds(s.monitors);
  if (!b) return [whole];
  const sx = s.w / b.w;
  const sy = s.h / b.h;
  const parts = s
    .monitors!.filter((m) =>
      side === "right" ? m[0] + m[2] === b.x + b.w : side === "left" ? m[0] === b.x : side === "down" ? m[1] + m[3] === b.y + b.h : m[1] === b.y,
    )
    .map((m): [number, number] =>
      vertical ? [s.y + (m[1] - b.y) * sy, s.y + (m[1] + m[3] - b.y) * sy] : [s.x + (m[0] - b.x) * sx, s.x + (m[0] + m[2] - b.x) * sx],
    )
    .sort((p, q) => p[0] - q[0]);
  const merged: [number, number][] = [];
  for (const [lo, hi] of parts) {
    const last = merged[merged.length - 1];
    if (last && lo <= last[1] + EPS) last[1] = Math.max(last[1], hi);
    else merged.push([lo, hi]);
  }
  return merged;
}

function intersect(lo: number, hi: number, a: [number, number][], b: [number, number][]): [number, number][] {
  const out: [number, number][] = [];
  for (const [a0, a1] of a) {
    for (const [b0, b1] of b) {
      const s = Math.max(lo, a0, b0);
      const e = Math.min(hi, a1, b1);
      if (e - s > EPS) out.push([s, e]);
    }
  }
  return out.sort((p, q) => p[0] - q[0]);
}

/**
 * Every place two screens touch where both have a monitor. Each pair is reported once
 * (a is left of / above b), once per stretch.
 */
export function contacts(screens: Screen[]): Contact[] {
  const out: Contact[] = [];
  for (const a of screens) {
    for (const b of screens) {
      if (a === b) continue;
      if (Math.abs(right(a) - b.x) < EPS) {
        const lo = Math.max(a.y, b.y);
        const hi = Math.min(bottom(a), bottom(b));
        for (const [s, e] of intersect(lo, hi, covered(a, "right"), covered(b, "left")))
          out.push({ a: a.name, b: b.name, orientation: "v", x1: b.x, y1: s, x2: b.x, y2: e });
      }
      if (Math.abs(bottom(a) - b.y) < EPS) {
        const lo = Math.max(a.x, b.x);
        const hi = Math.min(right(a), right(b));
        for (const [s, e] of intersect(lo, hi, covered(a, "down"), covered(b, "up")))
          out.push({ a: a.name, b: b.name, orientation: "h", x1: s, y1: b.y, x2: e, y2: b.y });
      }
    }
  }
  return out;
}

/** For one screen, which neighbours it reaches and through which side. */
export function neighbours(screens: Screen[], name: string): { name: string; side: Side }[] {
  const out: { name: string; side: Side }[] = [];
  const add = (n: { name: string; side: Side }) => {
    if (!out.some((o) => o.name === n.name && o.side === n.side)) out.push(n);
  };
  for (const c of contacts(screens)) {
    if (c.a === name) add({ name: c.b, side: c.orientation === "v" ? "right" : "down" });
    if (c.b === name) add({ name: c.a, side: c.orientation === "v" ? "left" : "up" });
  }
  return out;
}

/** Screens the cursor can never reach starting from `server`. */
export function unreachable(screens: Screen[], server: string): string[] {
  const all = contacts(screens);
  const seen = new Set([server]);
  const queue = [server];
  while (queue.length) {
    const n = queue.shift()!;
    for (const c of all) {
      const other = c.a === n ? c.b : c.b === n ? c.a : null;
      if (other && !seen.has(other)) {
        seen.add(other);
        queue.push(other);
      }
    }
  }
  return screens.filter((s) => !seen.has(s.name)).map((s) => s.name);
}

export function bounds(rects: Rect[]): Rect {
  if (rects.length === 0) return { x: 0, y: 0, w: DEFAULT_W, h: DEFAULT_H };
  const x = Math.min(...rects.map((r) => r.x));
  const y = Math.min(...rects.map((r) => r.y));
  return { x, y, w: Math.max(...rects.map(right)) - x, h: Math.max(...rects.map(bottom)) - y };
}

// ---- snapping ---------------------------------------------------------------------------

export interface Guide {
  orientation: "v" | "h";
  /** x for vertical guides, y for horizontal ones. */
  at: number;
}

interface Candidate {
  value: number;
  guide: number;
  touch: boolean;
}

function best(current: number, candidates: Candidate[], threshold: number): Candidate | null {
  let pick: Candidate | null = null;
  let pickDist = Infinity;
  for (const c of candidates) {
    const d = Math.abs(c.value - current);
    // touching beats aligning when both are equally close
    const score = d - (c.touch ? 0.01 : 0);
    if (d <= threshold && score < pickDist) {
      pick = c;
      pickDist = score;
    }
  }
  return pick;
}

/**
 * Snaps a screen being moved to the edges of the others: touching them on any side,
 * or lining up with their edges or centres. `threshold` is in layout units.
 */
export function snapMove(r: Rect, others: Rect[], threshold: number): { x: number; y: number; guides: Guide[] } {
  const xs: Candidate[] = [];
  const ys: Candidate[] = [];
  for (const o of others) {
    xs.push(
      { value: right(o), guide: right(o), touch: true },
      { value: o.x - r.w, guide: o.x, touch: true },
      { value: o.x, guide: o.x, touch: false },
      { value: right(o) - r.w, guide: right(o), touch: false },
      { value: o.x + o.w / 2 - r.w / 2, guide: o.x + o.w / 2, touch: false },
    );
    ys.push(
      { value: bottom(o), guide: bottom(o), touch: true },
      { value: o.y - r.h, guide: o.y, touch: true },
      { value: o.y, guide: o.y, touch: false },
      { value: bottom(o) - r.h, guide: bottom(o), touch: false },
      { value: o.y + o.h / 2 - r.h / 2, guide: o.y + o.h / 2, touch: false },
    );
  }
  const bx = best(r.x, xs, threshold);
  const by = best(r.y, ys, threshold);
  const guides: Guide[] = [];
  if (bx) guides.push({ orientation: "v", at: bx.guide });
  if (by) guides.push({ orientation: "h", at: by.guide });
  return { x: Math.round(bx ? bx.value : r.x), y: Math.round(by ? by.value : r.y), guides };
}

/**
 * Nearest position to `r` where it overlaps nothing, trying every spot flush against
 * another screen. Returns null when there is none (the caller should revert).
 */
export function resolveOverlap(r: Rect, others: Rect[]): Rect | null {
  if (!overlapsAny(r, others)) return r;
  const candidates: Rect[] = [];
  for (const o of others) {
    candidates.push(
      { ...r, x: right(o) },
      { ...r, x: o.x - r.w },
      { ...r, y: bottom(o) },
      { ...r, y: o.y - r.h },
    );
  }
  let pick: Rect | null = null;
  let pickDist = Infinity;
  for (const c of candidates) {
    if (overlapsAny(c, others)) continue;
    const d = Math.hypot(c.x - r.x, c.y - r.y);
    if (d < pickDist) {
      pick = c;
      pickDist = d;
    }
  }
  return pick;
}

export type Handle = "n" | "s" | "e" | "w" | "ne" | "nw" | "se" | "sw";

/**
 * New rectangle while dragging a resize handle by (dx, dy) from `start`.
 * Corner handles keep the aspect ratio unless `free`; moving edges snap to the
 * edges of other screens and stop where they would run into one.
 */
export function resize(
  start: Rect,
  handle: Handle,
  dx: number,
  dy: number,
  others: Rect[],
  free: boolean,
  threshold: number,
  /** The proportion it must keep from any handle, as one of known monitors does. */
  locked: number | null = null,
): {
  rect: Rect;
  guides: Guide[];
} {
  const west = handle.includes("w");
  const east = handle.includes("e");
  const north = handle.includes("n");
  const south = handle.includes("s");

  let x1 = start.x;
  let y1 = start.y;
  let x2 = right(start);
  let y2 = bottom(start);
  if (west) x1 = Math.min(x1 + dx, x2 - MIN_SIZE);
  if (east) x2 = Math.max(x2 + dx, x1 + MIN_SIZE);
  if (north) y1 = Math.min(y1 + dy, y2 - MIN_SIZE);
  if (south) y2 = Math.max(y2 + dy, y1 + MIN_SIZE);

  const guides: Guide[] = [];
  const xEdges = others.flatMap((o) => [o.x, right(o)]);
  const yEdges = others.flatMap((o) => [o.y, bottom(o)]);
  const snap = (v: number, edges: number[], orientation: "v" | "h") => {
    let pick = v;
    let dist = threshold;
    for (const e of edges) {
      if (Math.abs(e - v) <= dist) {
        pick = e;
        dist = Math.abs(e - v);
      }
    }
    if (pick !== v) guides.push({ orientation, at: pick });
    return pick;
  };

  const corner = (west || east) && (north || south);
  if (locked && !corner) {
    // a side alone: the other axis follows, from the top or left
    if (west) x1 = snap(x1, xEdges, "v");
    if (east) x2 = snap(x2, xEdges, "v");
    if (north) y1 = snap(y1, yEdges, "h");
    if (south) y2 = snap(y2, yEdges, "h");
    if (west || east) y2 = y1 + Math.max(MIN_SIZE, x2 - x1) / locked;
    else x2 = x1 + Math.max(MIN_SIZE, y2 - y1) * locked;
  } else if (corner && (!free || locked)) {
    // keep the aspect: follow whichever axis moved more, snapping that one
    const ratio = locked ?? start.w / start.h;
    let w = x2 - x1;
    let h = y2 - y1;
    if (Math.abs(w - start.w) / start.w >= Math.abs(h - start.h) / start.h) {
      if (west) x1 = snap(x1, xEdges, "v");
      else x2 = snap(x2, xEdges, "v");
      w = Math.max(MIN_SIZE, x2 - x1);
      h = w / ratio;
    } else {
      if (north) y1 = snap(y1, yEdges, "h");
      else y2 = snap(y2, yEdges, "h");
      h = Math.max(MIN_SIZE, y2 - y1);
      w = h * ratio;
    }
    if (west) x1 = x2 - w;
    else x2 = x1 + w;
    if (north) y1 = y2 - h;
    else y2 = y1 + h;
  } else {
    if (west) x1 = snap(x1, xEdges, "v");
    if (east) x2 = snap(x2, xEdges, "v");
    if (north) y1 = snap(y1, yEdges, "h");
    if (south) y2 = snap(y2, yEdges, "h");
  }

  const rect = { x: Math.round(x1), y: Math.round(y1), w: Math.round(x2 - x1), h: Math.round(y2 - y1) };
  return { rect, guides };
}

/**
 * Free spot for a new screen of the given size next to `anchor`: right, left,
 * below or above it, then to the right of everything.
 */
export function placeNear(anchor: Rect | null, size: { w: number; h: number }, others: Rect[]): Rect {
  if (anchor) {
    const tries: Rect[] = [
      { x: right(anchor), y: anchor.y + (anchor.h - size.h) / 2, ...size },
      { x: anchor.x - size.w, y: anchor.y + (anchor.h - size.h) / 2, ...size },
      { x: anchor.x + (anchor.w - size.w) / 2, y: bottom(anchor), ...size },
      { x: anchor.x + (anchor.w - size.w) / 2, y: anchor.y - size.h, ...size },
    ].map((r) => ({ ...r, x: Math.round(r.x), y: Math.round(r.y) }));
    const free = tries.find((r) => !overlapsAny(r, others));
    if (free) return free;
  }
  const b = bounds(others);
  return { x: right(b), y: anchor ? anchor.y : b.y, ...size };
}

/** Same rule as the Qt GUI and the backend: letters, digits, `_`, `.`, `-`. */
export function isValidName(name: string): boolean {
  return /^[\p{L}\p{N}_.-]{1,255}$/u.test(name);
}

export function nextFreeName(screens: Screen[], base = "computadora"): string {
  const taken = new Set(screens.map((s) => s.name.toLowerCase()));
  for (let i = 2; ; i++) {
    const n = `${base}-${i}`;
    if (!taken.has(n)) return n;
  }
}

/** Human-readable aspect ratio, e.g. "16:9", or "1.42:1" when nothing common fits. */
export function aspectLabel(w: number, h: number): string {
  const ratio = w / h;
  const common: [string, number][] = [
    ["16:9", 16 / 9],
    ["16:10", 16 / 10],
    ["21:9", 21 / 9],
    ["32:9", 32 / 9],
    ["4:3", 4 / 3],
    ["3:2", 3 / 2],
    ["5:4", 5 / 4],
    ["1:1", 1],
    ["9:16", 9 / 16],
    ["10:16", 10 / 16],
    ["3:4", 3 / 4],
  ];
  const hit = common.find(([, r]) => Math.abs(r - ratio) / r < 0.02);
  return hit ? hit[0] : `${ratio.toFixed(2)}:1`;
}
