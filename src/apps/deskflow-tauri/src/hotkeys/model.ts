// Mirrors src-tauri/src/hotkeys.rs

import { t, type Key } from "../i18n";
import { ALT, CONTROL, comboLabel, hasMainKey, isMouseButton, keyLabel, sameKeys } from "./keys";

export type ActionKind =
  | "keyDown"
  | "keyUp"
  | "keystroke"
  | "switchToScreen"
  | "switchInDirection"
  | "switchToNextScreen"
  | "lockCursorToScreen"
  | "restartServer"
  | "mouseDown"
  | "mouseUp"
  | "mousebutton";

export type Direction = "left" | "right" | "up" | "down";
export type LockMode = "toggle" | "on" | "off";

export interface Action {
  kind: ActionKind;
  keys: number[];
  /** Where the keys may go; they only go to the one with the mouse. Empty = any. */
  computers: string[];
  target: string;
  direction: Direction;
  lockMode: LockMode;
  onRelease: boolean;
}

export interface Hotkey {
  keys: number[];
  actions: Action[];
}

/** Kinds offered when creating an action, in menu order, with the key of their name. */
export const ACTION_KINDS: { kind: ActionKind; label: Key }[] = [
  { kind: "switchToScreen", label: "hotkeys.kind.switchToScreen" },
  { kind: "switchInDirection", label: "hotkeys.kind.switchInDirection" },
  { kind: "switchToNextScreen", label: "hotkeys.kind.switchToNextScreen" },
  { kind: "lockCursorToScreen", label: "hotkeys.kind.lockCursorToScreen" },
  { kind: "keystroke", label: "hotkeys.kind.keystroke" },
  { kind: "keyDown", label: "hotkeys.kind.keyDown" },
  { kind: "keyUp", label: "hotkeys.kind.keyUp" },
  { kind: "restartServer", label: "hotkeys.kind.restartServer" },
];

/** Keys of the names of each direction and lock mode, translated when shown. */
export const DIRECTION_LABELS: Record<Direction, Key> = {
  left: "hotkeys.direction.left",
  right: "hotkeys.direction.right",
  up: "hotkeys.direction.up",
  down: "hotkeys.direction.down",
};
export const LOCK_LABELS: Record<LockMode, Key> = { toggle: "hotkeys.lock.toggle", on: "hotkeys.lock.on", off: "hotkeys.lock.off" };

const SWITCH_TEXT: Record<Direction, Key> = {
  left: "hotkeys.action.switch.left",
  right: "hotkeys.action.switch.right",
  up: "hotkeys.action.switch.up",
  down: "hotkeys.action.switch.down",
};
const LOCK_TEXT: Record<LockMode, Key> = { toggle: "hotkeys.action.lock.toggle", on: "hotkeys.action.lock.on", off: "hotkeys.action.lock.off" };

export const sendsKeys = (k: ActionKind) => k === "keystroke" || k === "keyDown" || k === "keyUp";
const sendsMouse = (k: ActionKind) => k === "mouseDown" || k === "mouseUp" || k === "mousebutton";

export function newAction(kind: ActionKind, computers: string[]): Action {
  return { kind, keys: [], computers: [], target: kind === "switchToScreen" ? (computers[0] ?? "") : "", direction: "right", lockMode: "toggle", onRelease: false };
}

export function describeAction(a: Action): string {
  const keys = a.keys.length ? comboLabel(a.keys) : "…";
  const to = (action: string) => (a.computers.length ? t("hotkeys.action.onlyOn", { action, computers: a.computers.join(", ") }) : action);
  let text: string;
  switch (a.kind) {
    case "switchToScreen":
      text = a.target ? t("hotkeys.action.goTo", { name: a.target }) : t("hotkeys.action.goToUnset");
      break;
    case "switchInDirection":
      text = t(SWITCH_TEXT[a.direction]);
      break;
    case "switchToNextScreen":
      text = t("hotkeys.action.switchToNext");
      break;
    case "lockCursorToScreen":
      text = t(LOCK_TEXT[a.lockMode]);
      break;
    case "keystroke":
      text = to(t("hotkeys.action.keystroke", { keys }));
      break;
    case "keyDown":
      text = to(t("hotkeys.action.keyDown", { keys }));
      break;
    case "keyUp":
      text = to(t("hotkeys.action.keyUp", { keys }));
      break;
    case "restartServer":
      text = t("hotkeys.action.restartServer");
      break;
    default:
      text = t("hotkeys.action.mouseButton", { keys: comboLabel(a.keys) });
  }
  return a.onRelease ? t("hotkeys.action.onRelease", { action: text }) : text;
}

/** Problems that would stop the hotkey from working, mirroring the backend checks. */
export function hotkeyProblems(hk: Hotkey, computers: string[], all: Hotkey[]): string[] {
  const out: string[] = [];
  if (!hasMainKey(hk.keys)) out.push(t("hotkeys.problem.missingKeys"));
  else if (hk.keys.some(isMouseButton)) out.push(t("hotkeys.problem.mouseIgnored"));
  if (!hk.actions.length) out.push(t("hotkeys.problem.noActions"));
  for (const a of hk.actions) {
    if (sendsKeys(a.kind) && !hasMainKey(a.keys)) out.push(t("hotkeys.problem.missingSendKeys"));
    if (a.kind === "switchToScreen" && !computers.includes(a.target)) out.push(t("hotkeys.problem.chooseTarget"));
    if (a.computers.some((c) => !computers.includes(c))) out.push(t("hotkeys.problem.goneComputer"));
    if (sendsMouse(a.kind)) out.push(t("hotkeys.problem.mouseClassic"));
  }
  if (hasMainKey(hk.keys) && all.filter((o) => sameKeys(o.keys, hk.keys)).length > 1) out.push(t("hotkeys.problem.duplicate"));
  return [...new Set(out)];
}

/** Problems that block saving (the rest are warnings). */
export function blocking(hk: Hotkey, computers: string[]): boolean {
  return (
    !hasMainKey(hk.keys) ||
    hk.actions.some(
      (a) =>
        (sendsKeys(a.kind) && (!hasMainKey(a.keys) || a.keys.some(isMouseButton))) ||
        (a.kind === "switchToScreen" && !computers.includes(a.target)) ||
        a.computers.some((c) => !computers.includes(c)),
    )
  );
}

const ARROWS: [Direction, number][] = [
  ["left", 0x01000012],
  ["right", 0x01000014],
  ["up", 0x01000013],
  ["down", 0x01000015],
];

/** Ctrl+Alt+Del cannot be pressed on the server (its own Windows takes it) nor recorded. */
export const CTRL_ALT_DEL = [CONTROL, ALT, 0x01000007];
const KEY_END = 0x01000011;

export interface Preset {
  id: string;
  /** In the current language, so called when shown. */
  title: () => string;
  text: () => string;
  /** `server` is this computer, which must never receive keys meant for the others. */
  build: (computers: string[], server: string) => Hotkey[];
}

export const PRESETS: Preset[] = [
  {
    id: "ctrlAltDel",
    title: () => `${comboLabel([CONTROL, ALT, KEY_END])} → ${comboLabel(CTRL_ALT_DEL)}`,
    text: () => t("hotkeys.preset.ctrlAltDel.text", { combo: CTRL_ALT_DEL.map(keyLabel).join("+") }),
    build: (computers, server) => {
      const others = computers.filter((c) => c !== server);
      if (!others.length) return [];
      return [{ keys: [CONTROL, ALT, KEY_END], actions: [{ ...newAction("keystroke", computers), keys: CTRL_ALT_DEL, computers: others }] }];
    },
  },
  {
    id: "arrows",
    title: () => t("hotkeys.preset.arrows.title", { combo: comboLabel([CONTROL, ALT]) }),
    text: () => t("hotkeys.preset.arrows.text"),
    build: () =>
      ARROWS.map(([direction, key]) => ({
        keys: [CONTROL, ALT, key],
        actions: [{ ...newAction("switchInDirection", []), direction }],
      })),
  },
  {
    id: "numbers",
    title: () => `${comboLabel([CONTROL, ALT])} + 1, 2, 3…`,
    text: () => t("hotkeys.preset.numbers.text"),
    build: (computers) =>
      computers.slice(0, 9).map((name, i) => ({
        keys: [CONTROL, ALT, 0x31 + i],
        actions: [{ ...newAction("switchToScreen", computers), target: name }],
      })),
  },
  {
    id: "lock",
    title: () => comboLabel([CONTROL, ALT, 0x4c]),
    text: () => t("hotkeys.preset.lock.text"),
    build: () => [{ keys: [CONTROL, ALT, 0x4c], actions: [newAction("lockCursorToScreen", [])] }],
  },
];

/** Adds presets, skipping combos that are already taken. */
export function addPreset(current: Hotkey[], preset: Preset, computers: string[], server = ""): Hotkey[] {
  const fresh = preset.build(computers, server).filter((h) => !current.some((c) => sameKeys(c.keys, h.keys)));
  return [...current, ...fresh];
}
