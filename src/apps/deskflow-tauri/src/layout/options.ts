// Per-computer options. Mirrors src-tauri/src/computer.rs.

export type Modifier = "shift" | "ctrl" | "alt" | "altgr" | "meta" | "super" | "none";
export type ModifierKey = "shift" | "ctrl" | "alt" | "altgr" | "meta" | "super";

export interface ComputerOptions {
  aliases: string[];
  modifiers: Record<ModifierKey, Modifier>;
  corners: { topLeft: boolean; topRight: boolean; bottomLeft: boolean; bottomRight: boolean };
  cornerSize: number;
  fixCapsLock: boolean;
  fixNumLock: boolean;
  fixScrollLock: boolean;
  xtestXinerama: boolean;
  weakX11Focus: boolean;
}

export const MODIFIER_KEYS: ModifierKey[] = ["shift", "ctrl", "alt", "altgr", "meta", "super"];

export const MODIFIER_LABELS: Record<Modifier, string> = {
  shift: "Shift",
  ctrl: "Ctrl",
  alt: "Alt",
  altgr: "AltGr",
  meta: "Meta",
  super: "Super (⊞ / ⌘)",
  none: "Ninguna",
};

export function defaultOptions(): ComputerOptions {
  return {
    aliases: [],
    modifiers: { shift: "shift", ctrl: "ctrl", alt: "alt", altgr: "altgr", meta: "meta", super: "super" },
    corners: { topLeft: false, topRight: false, bottomLeft: false, bottomRight: false },
    cornerSize: 0,
    fixCapsLock: false,
    fixNumLock: false,
    fixScrollLock: false,
    xtestXinerama: false,
    weakX11Focus: false,
  };
}

export const modifiersChanged = (o: ComputerOptions) => MODIFIER_KEYS.some((k) => o.modifiers[k] !== k);
export const fixesOn = (o: ComputerOptions) => o.fixCapsLock || o.fixNumLock || o.fixScrollLock;
export const cornersOn = (o: ComputerOptions) => Object.values(o.corners).some(Boolean);
export const keyboardCustomized = (o: ComputerOptions) => modifiersChanged(o) || fixesOn(o);
export const advancedCustomized = (o: ComputerOptions) =>
  o.aliases.length > 0 || cornersOn(o) || o.xtestXinerama || o.weakX11Focus;

/** Ctrl and Super swapped, the usual setup for a Mac controlled from a PC (or the reverse). */
export const isMacSwap = (o: ComputerOptions) => o.modifiers.ctrl === "super" && o.modifiers.super === "ctrl";

export function toggleMacSwap(o: ComputerOptions): ComputerOptions {
  const swapped = isMacSwap(o);
  return { ...o, modifiers: { ...o.modifiers, ctrl: swapped ? "ctrl" : "super", super: swapped ? "super" : "ctrl" } };
}

/** Short summary of what is customised, for the badge on a screen. */
export function optionsSummary(o: ComputerOptions): string[] {
  const out: string[] = [];
  if (isMacSwap(o)) out.push("Ctrl ⇄ ⌘");
  else if (modifiersChanged(o)) out.push("Modificadoras cambiadas");
  if (fixesOn(o)) out.push("Correcciones de teclado");
  if (cornersOn(o)) out.push("Esquinas muertas");
  if (o.aliases.length) out.push(o.aliases.length === 1 ? "1 alias" : `${o.aliases.length} alias`);
  if (o.xtestXinerama || o.weakX11Focus) out.push("Ajustes de Linux");
  return out;
}

const IPV4 = /^(\d{1,3}\.){3}\d{1,3}$/;

/** Same rules as the Qt GUI's AliasValidator and the backend. */
export function aliasError(alias: string, owner: string, taken: { name: string; aliases: string[] }[]): string | null {
  if (!/^[\p{L}\p{N}_.-]{1,255}$/u.test(alias)) return "Usá letras, números, punto, guion o guion bajo, sin espacios.";
  if (IPV4.test(alias) || alias.includes(":")) return "Un alias no puede ser una dirección IP.";
  const lower = alias.toLowerCase();
  for (const t of taken) {
    if (t.name.toLowerCase() === lower) return t.name === owner ? "Es el mismo nombre de la computadora." : `Ya es el nombre de otra computadora.`;
    if (t.aliases.some((a) => a.toLowerCase() === lower)) return t.name === owner ? "Ya está en la lista." : `Ya es un alias de ${t.name}.`;
  }
  return null;
}
