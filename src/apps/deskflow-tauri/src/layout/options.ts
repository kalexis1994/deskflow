// Per-computer options. Mirrors src-tauri/src/computer.rs.

import { t, tn } from "../i18n";

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

/** What a key can send, in the order the menus show them. */
export const MODIFIERS: Modifier[] = [...MODIFIER_KEYS, "none"];

const KEY_LABELS: Record<Exclude<ModifierKey, "shift" | "ctrl">, string> = {
  alt: "Alt",
  altgr: "AltGr",
  meta: "Meta",
  super: "Super (⊞ / ⌘)",
};

/** Name of what a key sends; key names are the same in every language, "none" is not. */
export function modifierLabel(m: Modifier): string {
  if (m === "none") return t("layout.keyboard.none");
  // the two whose keycaps change with the language (Strg, Umschalt, Maj)
  if (m === "ctrl") return t("hotkeys.key.ctrl");
  if (m === "shift") return t("hotkeys.key.shift");
  return KEY_LABELS[m];
}

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
  else if (modifiersChanged(o)) out.push(t("layout.badge.modifiers"));
  if (fixesOn(o)) out.push(t("layout.badge.fixes"));
  if (cornersOn(o)) out.push(t("layout.badge.corners"));
  if (o.aliases.length) out.push(tn("layout.badge.aliases", o.aliases.length));
  if (o.xtestXinerama || o.weakX11Focus) out.push(t("layout.badge.linux"));
  return out;
}

const IPV4 = /^(\d{1,3}\.){3}\d{1,3}$/;

/** Same rules as the Qt GUI's AliasValidator and the backend. */
export function aliasError(alias: string, owner: string, taken: { name: string; aliases: string[] }[]): string | null {
  if (!/^[\p{L}\p{N}_.-]{1,255}$/u.test(alias)) return t("layout.name.invalid");
  if (IPV4.test(alias) || alias.includes(":")) return t("layout.alias.ip");
  const lower = alias.toLowerCase();
  for (const other of taken) {
    if (other.name.toLowerCase() === lower) return t(other.name === owner ? "layout.alias.sameName" : "layout.alias.otherName");
    if (other.aliases.some((a) => a.toLowerCase() === lower))
      return other.name === owner ? t("layout.alias.listed") : t("layout.name.aliasOf", { name: other.name });
  }
  return null;
}
