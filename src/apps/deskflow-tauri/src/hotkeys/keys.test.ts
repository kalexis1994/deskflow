import { describe, expect, it } from "vitest";
import { ALT, CONTROL, META, SHIFT, comboLabel, hasMainKey, keyLabel, sameKeys, sequenceFromEvent, type KeyEventLike } from "./keys";

const ev = (key: string, code: string, mods: Partial<KeyEventLike> = {}): KeyEventLike => ({
  key,
  code,
  ctrlKey: false,
  altKey: false,
  shiftKey: false,
  metaKey: false,
  ...mods,
});

describe("sequenceFromEvent", () => {
  it("records modifiers plus the key as Qt codes", () => {
    expect(sequenceFromEvent(ev("ArrowLeft", "ArrowLeft", { ctrlKey: true, altKey: true }))).toEqual([CONTROL, ALT, 0x01000012]);
    expect(sequenceFromEvent(ev("F5", "F5", { metaKey: true }))).toEqual([META, 0x01000034]);
  });

  it("uses the physical key for letters and digits", () => {
    // Ctrl+Alt+Q on a Spanish layout types "@"
    expect(sequenceFromEvent(ev("@", "KeyQ", { ctrlKey: true, altKey: true }))).toEqual([CONTROL, ALT, 0x51]);
    expect(sequenceFromEvent(ev("!", "Digit1", { shiftKey: true }))).toEqual([SHIFT, 0x31]);
    expect(sequenceFromEvent(ev("1", "Numpad1"))).toEqual([0x31]);
  });

  it("keeps layout characters for everything else", () => {
    expect(sequenceFromEvent(ev("ñ", "Semicolon", { ctrlKey: true }))).toEqual([CONTROL, 0xd1]);
    expect(sequenceFromEvent(ev(",", "Comma", { ctrlKey: true }))).toEqual([CONTROL, 0x2c]);
    expect(sequenceFromEvent(ev("Enter", "NumpadEnter"))).toEqual([0x01000005]);
    expect(sequenceFromEvent(ev(" ", "Space", { ctrlKey: true }))).toEqual([CONTROL, 0x20]);
  });

  it("ignores lone modifiers and unusable keys", () => {
    expect(sequenceFromEvent(ev("Control", "ControlLeft", { ctrlKey: true }))).toBeNull();
    expect(sequenceFromEvent(ev("AltGraph", "AltRight"))).toBeNull();
    expect(sequenceFromEvent(ev("Dead", "BracketLeft"))).toBeNull();
  });
});

describe("labels", () => {
  it("shows friendly keycaps", () => {
    expect(comboLabel([CONTROL, ALT, 0x01000012])).toBe("Ctrl + Alt + ←");
    expect(keyLabel(0x51)).toBe("Q");
    expect(keyLabel(0x01000026)).toBe("Bloq Despl");
    expect(keyLabel(0x01000030 + 11)).toBe("F12");
    expect(keyLabel(1)).toBe("Botón 1 del mouse");
  });

  it("compares combos regardless of modifier order", () => {
    expect(sameKeys([ALT, CONTROL, 0x41], [CONTROL, ALT, 0x41])).toBe(true);
    expect(sameKeys([CONTROL, 0x41], [ALT, 0x41])).toBe(false);
    expect(hasMainKey([CONTROL, ALT])).toBe(false);
  });
});
