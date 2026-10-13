import { describe, expect, it } from "vitest";
import { aliasError, defaultOptions, isMacSwap, keyboardCustomized, optionsSummary, toggleMacSwap } from "./options";

describe("options", () => {
  it("toggles the Mac Ctrl/Cmd swap", () => {
    const o = toggleMacSwap(defaultOptions());
    expect(o.modifiers.ctrl).toBe("super");
    expect(o.modifiers.super).toBe("ctrl");
    expect(isMacSwap(o)).toBe(true);
    expect(optionsSummary(o)).toEqual(["Ctrl ⇄ ⌘"]);
    expect(toggleMacSwap(o)).toEqual(defaultOptions());
  });

  it("summarises customisations", () => {
    const o = { ...defaultOptions(), aliases: ["a", "b"], fixCapsLock: true };
    o.corners = { ...o.corners, topLeft: true };
    expect(optionsSummary(o)).toEqual(["Correcciones de teclado", "Esquinas muertas", "2 alias"]);
    expect(keyboardCustomized(o)).toBe(true);
    expect(optionsSummary(defaultOptions())).toEqual([]);
  });

  it("validates aliases against every name and alias", () => {
    const taken = [
      { name: "pc", aliases: ["pc.lan"] },
      { name: "mac", aliases: [] },
    ];
    expect(aliasError("pc-old", "pc", taken)).toBeNull();
    expect(aliasError("pc.lan", "pc", taken)).toBe("Ya está en la lista.");
    expect(aliasError("PC.LAN", "mac", taken)).toBe("Ya es un alias de pc.");
    expect(aliasError("mac", "pc", taken)).toBe("Ya es el nombre de otra computadora.");
    expect(aliasError("192.168.1.2", "pc", taken)).toBe("Un alias no puede ser una dirección IP.");
    expect(aliasError("mi pc", "pc", taken)).not.toBeNull();
  });
});
