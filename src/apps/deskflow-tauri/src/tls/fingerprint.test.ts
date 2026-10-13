import { describe, expect, it } from "vitest";
import { fingerprintArt, fingerprintArtBody, fingerprintColumns } from "./fingerprint";

const SERVER = "4577ebbf2317c8d371fc408d2b72810c237473f774260d158bbb9eb6fe549dc1";

describe("fingerprint art", () => {
  it("draws the same 19x11 frame as the Qt GUI", () => {
    const lines = fingerprintArt(SERVER).split("\n");
    expect(lines).toHaveLength(11);
    expect(lines.every((l) => [...l].length === 19)).toBe(true);
    expect(lines[0]).toBe("╔═════════════════╗");
    expect(lines[10]).toBe("╚═════════════════╝");
  });

  it("marks the start in the middle and the end somewhere", () => {
    const art = fingerprintArt(SERVER);
    expect(art.split("\n")[5][9]).toBe("S");
    expect(art.match(/E/g)).toHaveLength(1);
  });

  it("is deterministic and differs between digests", () => {
    expect(fingerprintArt(SERVER)).toBe(fingerprintArt(SERVER.toUpperCase()));
    expect(fingerprintArt(SERVER)).not.toBe(fingerprintArt("00".repeat(32)));
  });

  it("walks like the C++ version for a known digest", () => {
    // all-zero digest: every step goes up-left, so the worm ends in the top-left corner
    const lines = fingerprintArt("00".repeat(32)).split("\n");
    expect(lines[1][1]).toBe("E");
    expect(lines[5][9]).toBe("S");
    // from the centre (8,4): diagonally to (4,0), then along the top edge to (0,0)
    // (line = row + 1, character = column + 1 because of the frame)
    expect(lines[4][8]).toBe("."); // (7,3)
    expect(lines[2][6]).toBe("."); // (5,1)
    expect(lines[1].slice(2, 6)).toBe("...."); // (1..4,0)
    expect(lines[2][2]).toBe(" "); // never visited
  });
});

describe("fingerprint art body", () => {
  it("is the art without its frame", () => {
    const body = fingerprintArtBody(SERVER).split("\n");
    expect(body).toHaveLength(9);
    expect(body.every((l) => l.length === 17)).toBe(true);
    expect(body[4][8]).toBe("S");
  });
});

describe("fingerprint columns", () => {
  it("prints colon-separated upper-case rows of 24", () => {
    expect(fingerprintColumns(SERVER).split("\n")).toEqual([
      "45:77:EB:BF:23:17:C8:D3:",
      "71:FC:40:8D:2B:72:81:0C:",
      "23:74:73:F7:74:26:0D:15:",
      "8B:BB:9E:B6:FE:54:9D:C1",
    ]);
  });
});
