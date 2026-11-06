import { describe, expect, it } from "vitest";
import { canChangeChannel, isAsking, isWorking, progressLine, searchLine, whatHappens, type UpdatePhase, type UpdateState } from "./describe";

const state = (phase: UpdatePhase, dismissed = false): UpdateState => ({ ...phase, current: "0.1.8", channel: "stable", dismissed });
const available: UpdatePhase = { kind: "available", version: "0.1.9", notes: null, error: null };

describe("the question", () => {
  it("is asked until it is answered", () => {
    expect(isAsking(state(available))).toBe(true);
    expect(isAsking(state(available, true))).toBe(false);
  });

  it("isn't asked with nothing to install", () => {
    expect(isAsking(state({ kind: "idle" }))).toBe(false);
    expect(isAsking(state({ kind: "upToDate" }))).toBe(false);
    expect(isAsking(state({ kind: "failed", message: "x" }))).toBe(false);
  });

  it("warns who loses what before anyone says yes", () => {
    expect(whatHappens("server")).toContain("otras computadoras");
    expect(whatHappens("client")).toContain("Esta computadora");
    expect(whatHappens("none")).toContain("se detiene");
    for (const mode of ["server", "client", "none"] as const) expect(whatHappens(mode)).toContain("administrador");
  });
});

describe("while it installs", () => {
  it("says how far along it is", () => {
    expect(progressLine(state({ kind: "downloading", version: "0.1.9", percent: 40 }))).toBe("Bajando la versión 0.1.9 (40%)…");
    expect(progressLine(state({ kind: "downloading", version: "0.1.9", percent: null }))).toBe("Bajando la versión 0.1.9…");
    expect(progressLine(state({ kind: "installing", version: "0.1.9" }))).toBe("Instalando la versión 0.1.9…");
  });

  it("knows it is working", () => {
    expect(isWorking(state({ kind: "installing", version: "0.1.9" }))).toBe(true);
    expect(isWorking(state(available))).toBe(false);
  });
});

describe("the channel", () => {
  it("waits for what is running", () => {
    expect(canChangeChannel(state({ kind: "idle" }))).toBe(true);
    expect(canChangeChannel(state(available))).toBe(true);
    expect(canChangeChannel(state({ kind: "checking" }))).toBe(false);
    expect(canChangeChannel(state({ kind: "downloading", version: "0.1.9", percent: 1 }))).toBe(false);
    expect(canChangeChannel(state({ kind: "installing", version: "0.1.9" }))).toBe(false);
  });
});

describe("the search by hand", () => {
  it("answers next to the version", () => {
    expect(searchLine(state({ kind: "checking" }))).toBe("Buscando…");
    expect(searchLine(state({ kind: "upToDate" }))).toBe("Está al día");
    expect(searchLine(state({ kind: "idle" }))).toBeNull();
  });

  it("keeps a version it was told not to install in sight", () => {
    expect(searchLine(state(available, true))).toBe("Hay una versión nueva: 0.1.9");
    expect(searchLine(state(available))).toBeNull();
  });
});
