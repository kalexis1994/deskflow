import { describe, expect, it } from "vitest";
import {
  aspectLabel,
  contacts,
  isValidName,
  neighbours,
  nextFreeName,
  overlaps,
  placeNear,
  resize,
  resolveOverlap,
  snapMove,
  unreachable,
  withMonitors,
  monitorRects,
  type Screen,
} from "./geometry";

const s = (name: string, x: number, y: number, w = 160, h = 90): Screen => ({ name, x, y, w, h });

describe("contacts", () => {
  it("finds shared edges once per pair", () => {
    const screens = [s("a", 0, 0), s("b", 160, 45), s("c", 0, 90)];
    expect(contacts(screens)).toEqual([
      { a: "a", b: "b", orientation: "v", x1: 160, y1: 45, x2: 160, y2: 90 },
      { a: "a", b: "c", orientation: "h", x1: 0, y1: 90, x2: 160, y2: 90 },
      { a: "c", b: "b", orientation: "v", x1: 160, y1: 90, x2: 160, y2: 135 },
    ]);
    expect(neighbours(screens, "b")).toEqual([
      { name: "a", side: "left" },
      { name: "c", side: "left" },
    ]);
  });

  it("ignores corners that only meet at a point", () => {
    expect(contacts([s("a", 0, 0), s("b", 160, 90)])).toEqual([]);
    expect(unreachable([s("a", 0, 0), s("b", 160, 90)], "a")).toEqual(["b"]);
  });

  it("follows chains for reachability", () => {
    const screens = [s("srv", 0, 0), s("mid", 160, 0), s("far", 320, 0), s("lost", 1000, 0)];
    expect(unreachable(screens, "srv")).toEqual(["lost"]);
  });
});

describe("snapMove", () => {
  const others = [s("a", 0, 0)];

  it("sticks to the right edge of a neighbour and aligns tops", () => {
    const r = snapMove({ x: 166, y: 4, w: 160, h: 90 }, others, 10);
    expect([r.x, r.y]).toEqual([160, 0]);
    expect(r.guides).toEqual([
      { orientation: "v", at: 160 },
      { orientation: "h", at: 0 },
    ]);
  });

  it("prefers touching over aligning at equal distance", () => {
    // x = 80 is 80 away from both aligning with a (x = 0) and touching its right edge (x = 160)
    const r = snapMove({ x: 80, y: 200, w: 160, h: 90 }, others, 80);
    expect([r.x, r.y]).toEqual([160, 200]);
  });

  it("leaves far positions alone", () => {
    const r = snapMove({ x: 500, y: 300, w: 160, h: 90 }, others, 10);
    expect([r.x, r.y, r.guides.length]).toEqual([500, 300, 0]);
  });
});

describe("resolveOverlap", () => {
  it("pushes to the nearest free side", () => {
    expect(resolveOverlap({ x: 150, y: 0, w: 160, h: 90 }, [s("a", 0, 0)])).toEqual({ x: 160, y: 0, w: 160, h: 90 });
    expect(resolveOverlap({ x: 10, y: 80, w: 160, h: 90 }, [s("a", 0, 0)])).toEqual({ x: 10, y: 90, w: 160, h: 90 });
  });

  it("keeps a position that already fits", () => {
    const r = { x: 400, y: 0, w: 160, h: 90 };
    expect(resolveOverlap(r, [s("a", 0, 0)])).toBe(r);
  });
});

describe("resize", () => {
  const start = { x: 0, y: 0, w: 160, h: 90 };

  it("keeps the aspect ratio on corners", () => {
    const { rect } = resize(start, "se", 160, 0, [], false, 0);
    expect(rect).toEqual({ x: 0, y: 0, w: 320, h: 180 });
  });

  it("resizes freely on edges or with shift", () => {
    expect(resize(start, "e", 40, 999, [], false, 0).rect).toEqual({ x: 0, y: 0, w: 200, h: 90 });
    expect(resize(start, "se", 40, 10, [], true, 0).rect).toEqual({ x: 0, y: 0, w: 200, h: 100 });
  });

  it("snaps a moving edge to a neighbour's edge", () => {
    const { rect, guides } = resize(start, "s", 0, 7, [s("b", 160, 0, 100, 100)], false, 5);
    expect(rect.h).toBe(100);
    expect(guides).toEqual([{ orientation: "h", at: 100 }]);
  });

  it("never goes below the minimum size", () => {
    expect(resize(start, "w", 500, 0, [], false, 0).rect.w).toBe(40);
  });
});

describe("helpers", () => {
  it("places new screens next to an anchor without overlapping", () => {
    const a = s("a", 0, 0);
    expect(placeNear(a, { w: 160, h: 90 }, [a])).toEqual({ x: 160, y: 0, w: 160, h: 90 });
    const b = s("b", 160, 0);
    expect(placeNear(a, { w: 160, h: 90 }, [a, b])).toEqual({ x: -160, y: 0, w: 160, h: 90 });
    expect(overlaps(placeNear(a, { w: 100, h: 100 }, [a, b]), a)).toBe(false);
  });

  it("validates names like the Qt GUI", () => {
    expect(isValidName("mi-pc_2.local")).toBe(true);
    expect(isValidName("año")).toBe(true);
    expect(isValidName("mi pc")).toBe(false);
    expect(isValidName("")).toBe(false);
    expect(nextFreeName([s("computadora-2", 0, 0)])).toBe("computadora-3");
  });

  it("labels common aspect ratios", () => {
    expect(aspectLabel(160, 90)).toBe("16:9");
    expect(aspectLabel(1920, 1200)).toBe("16:10");
    expect(aspectLabel(90, 160)).toBe("9:16");
    expect(aspectLabel(100, 70)).toBe("1.43:1");
  });
});

describe("monitors", () => {
  // a 1080p monitor, and a 1440p one to its right, taller and lower
  const server: Screen = { ...s("server", 0, 0, 200, 100), monitors: [[0, 0, 1920, 1080], [1920, 360, 1920, 1440]] };

  it("joins only where both have a monitor", () => {
    const c = contacts([server, s("laptop", 200, 0, 160, 100)]);
    expect(c).toHaveLength(1);
    expect(c[0].y1).toBeCloseTo(20);
    expect(c[0].y2).toBeCloseTo(100);
  });

  it("joins monitors stacked along a side as one stretch", () => {
    const tall: Screen = { ...s("server", 0, 0, 100, 200), monitors: [[0, 0, 1920, 1080], [0, 1080, 1920, 1080]] };
    expect(contacts([tall, s("laptop", 100, 0, 100, 200)])).toHaveLength(1);
  });

  it("leaves two stretches around a gap, and one neighbour", () => {
    const l: Screen = { ...s("a", 0, 0, 100, 300), monitors: [[0, 0, 100, 100], [0, 200, 100, 100], [-100, 100, 100, 100]] };
    const screens = [l, s("b", 100, 0, 100, 300)];
    expect(contacts(screens)).toHaveLength(2);
    expect(neighbours(screens, "a")).toEqual([{ name: "b", side: "right" }]);
  });

  it("takes the monitors' proportion and places them inside", () => {
    const fitted = withMonitors(s("pc", 0, 0, 380, 90), [[0, 0, 1920, 1080], [1920, 0, 1920, 1080]]);
    expect(fitted.h).toBe(107); // 3840 × 1080
    expect(monitorRects(fitted)).toEqual([
      { x: 0, y: 0, w: 0.5, h: 1 },
      { x: 0.5, y: 0, w: 0.5, h: 1 },
    ]);
  });

  it("keeps the proportion from any handle when it is locked", () => {
    const start = { x: 0, y: 0, w: 200, h: 100 };
    expect(resize(start, "e", 100, 0, [], false, 0, 2).rect).toEqual({ x: 0, y: 0, w: 300, h: 150 });
    expect(resize(start, "s", 0, 50, [], false, 0, 2).rect).toEqual({ x: 0, y: 0, w: 300, h: 150 });
    // Shift doesn't free it
    expect(resize(start, "se", 100, 0, [], true, 0, 2).rect).toEqual({ x: 0, y: 0, w: 300, h: 150 });
  });
});
