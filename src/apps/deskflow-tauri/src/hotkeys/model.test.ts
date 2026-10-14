import { describe, expect, it } from "vitest";
import { ALT, CONTROL } from "./keys";
import { CTRL_ALT_DEL, PRESETS, addPreset, blocking, describeAction, hotkeyProblems, newAction, type Hotkey } from "./model";

const computers = ["escritorio", "laptop"];

describe("hotkey model", () => {
  it("describes actions in plain words", () => {
    expect(describeAction({ ...newAction("switchToScreen", computers) })).toBe("Ir a escritorio");
    expect(describeAction({ ...newAction("keystroke", computers), keys: [CONTROL, 0x43], computers: ["laptop"] })).toBe("Enviar Ctrl + C a laptop");
    expect(describeAction({ ...newAction("lockCursorToScreen", computers), lockMode: "off", onRelease: true })).toBe("Soltar el mouse (al soltar)");
  });

  it("adds presets without duplicating combos", () => {
    const numbers = PRESETS.find((p) => p.id === "numbers")!;
    const arrows = PRESETS.find((p) => p.id === "arrows")!;
    let list = addPreset([], numbers, computers);
    expect(list.map((h) => h.actions[0].target)).toEqual(["escritorio", "laptop"]);
    list = addPreset(list, numbers, computers);
    expect(list).toHaveLength(2);
    expect(addPreset(list, arrows, computers)).toHaveLength(6);
  });

  it("sends Ctrl+Alt+Del only to the other computers, never to this one", () => {
    const preset = PRESETS.find((p) => p.id === "ctrlAltDel")!;
    const [hk] = addPreset([], preset, computers, "escritorio");
    expect(hk.keys).toEqual([CONTROL, ALT, 0x01000011]);
    expect(hk.actions[0]).toMatchObject({ kind: "keystroke", keys: CTRL_ALT_DEL, computers: ["laptop"] });
    expect(describeAction(hk.actions[0])).toBe("Enviar Ctrl + Alt + Supr a laptop");
    expect(addPreset([], preset, ["escritorio"], "escritorio")).toEqual([]);
  });

  it("flags problems and only blocks on real errors", () => {
    const ok: Hotkey = { keys: [CONTROL, ALT, 0x31], actions: [{ ...newAction("switchToScreen", computers), target: "laptop" }] };
    expect(hotkeyProblems(ok, computers, [ok])).toEqual([]);
    expect(blocking(ok, computers)).toBe(false);

    const empty: Hotkey = { keys: [CONTROL, ALT, 0x31], actions: [] };
    expect(hotkeyProblems(empty, computers, [ok, empty])).toEqual([
      "No hace nada todavía: agregale una acción.",
      "Otro atajo usa la misma combinación.",
    ]);
    expect(blocking(empty, computers)).toBe(false);

    const gone: Hotkey = { keys: [CONTROL, 0x41], actions: [{ ...newAction("switchToScreen", computers), target: "viejo" }] };
    expect(blocking(gone, computers)).toBe(true);
    expect(blocking({ keys: [CONTROL, ALT], actions: [] }, computers)).toBe(true);
  });
});
