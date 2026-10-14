// Mirrors src-tauri/src/hotkeys.rs

import { ALT, CONTROL, comboLabel, hasMainKey, isMouseButton, sameKeys } from "./keys";

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
  /** Computers that receive the keys; empty = all. */
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

/** Kinds offered when creating an action, in menu order. */
export const ACTION_KINDS: { kind: ActionKind; label: string }[] = [
  { kind: "switchToScreen", label: "Ir a una computadora" },
  { kind: "switchInDirection", label: "Pasar hacia un lado" },
  { kind: "switchToNextScreen", label: "Pasar a la siguiente computadora" },
  { kind: "lockCursorToScreen", label: "Fijar el mouse en la pantalla" },
  { kind: "keystroke", label: "Enviar una combinación de teclas" },
  { kind: "keyDown", label: "Mantener apretadas unas teclas" },
  { kind: "keyUp", label: "Soltar unas teclas" },
  { kind: "restartServer", label: "Reconectar todas las computadoras" },
];

export const DIRECTION_LABELS: Record<Direction, string> = { left: "a la izquierda", right: "a la derecha", up: "arriba", down: "abajo" };
export const LOCK_LABELS: Record<LockMode, string> = { toggle: "Alternar", on: "Fijar", off: "Soltar" };

export const sendsKeys = (k: ActionKind) => k === "keystroke" || k === "keyDown" || k === "keyUp";
const sendsMouse = (k: ActionKind) => k === "mouseDown" || k === "mouseUp" || k === "mousebutton";

export function newAction(kind: ActionKind, computers: string[]): Action {
  return { kind, keys: [], computers: [], target: kind === "switchToScreen" ? (computers[0] ?? "") : "", direction: "right", lockMode: "toggle", onRelease: false };
}

export function describeAction(a: Action): string {
  const to = (list: string[]) => (list.length ? ` a ${list.join(", ")}` : " a todas");
  let text: string;
  switch (a.kind) {
    case "switchToScreen":
      text = a.target ? `Ir a ${a.target}` : "Ir a…";
      break;
    case "switchInDirection":
      text = `Pasar ${DIRECTION_LABELS[a.direction]}`;
      break;
    case "switchToNextScreen":
      text = "Pasar a la siguiente";
      break;
    case "lockCursorToScreen":
      text = { toggle: "Fijar o soltar el mouse", on: "Fijar el mouse", off: "Soltar el mouse" }[a.lockMode];
      break;
    case "keystroke":
      text = `Enviar ${a.keys.length ? comboLabel(a.keys) : "…"}${to(a.computers)}`;
      break;
    case "keyDown":
      text = `Mantener ${a.keys.length ? comboLabel(a.keys) : "…"}${to(a.computers)}`;
      break;
    case "keyUp":
      text = `Soltar ${a.keys.length ? comboLabel(a.keys) : "…"}${to(a.computers)}`;
      break;
    case "restartServer":
      text = "Reconectar todo";
      break;
    default:
      text = `Botón del mouse (${comboLabel(a.keys)})`;
  }
  return a.onRelease ? `${text} (al soltar)` : text;
}

/** Problems that would stop the hotkey from working, mirroring the backend checks. */
export function hotkeyProblems(hk: Hotkey, computers: string[], all: Hotkey[]): string[] {
  const out: string[] = [];
  if (!hasMainKey(hk.keys)) out.push("Falta la combinación que lo activa.");
  else if (hk.keys.some(isMouseButton)) out.push("Deskflow todavía ignora los atajos con botones del mouse.");
  if (!hk.actions.length) out.push("No hace nada todavía: agregale una acción.");
  for (const a of hk.actions) {
    if (sendsKeys(a.kind) && !hasMainKey(a.keys)) out.push("Falta elegir qué teclas enviar.");
    if (a.kind === "switchToScreen" && !computers.includes(a.target)) out.push("Elegí a qué computadora ir.");
    if (a.computers.some((c) => !computers.includes(c))) out.push("Envía teclas a una computadora que ya no está.");
    if (sendsMouse(a.kind)) out.push("Las acciones de mouse vienen de la app clásica y no se pueden editar acá.");
  }
  if (hasMainKey(hk.keys) && all.filter((o) => sameKeys(o.keys, hk.keys)).length > 1) out.push("Otro atajo usa la misma combinación.");
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
  title: string;
  text: string;
  /** `server` is this computer, which must never receive keys meant for the others. */
  build: (computers: string[], server: string) => Hotkey[];
}

export const PRESETS: Preset[] = [
  {
    id: "ctrlAltDel",
    title: "Ctrl + Alt + Fin → Ctrl + Alt + Supr",
    text: "Para iniciar sesión en Windows desde acá: Ctrl+Alt+Supr no se puede mandar directo.",
    build: (computers, server) => {
      const others = computers.filter((c) => c !== server);
      if (!others.length) return [];
      return [{ keys: [CONTROL, ALT, KEY_END], actions: [{ ...newAction("keystroke", computers), keys: CTRL_ALT_DEL, computers: others }] }];
    },
  },
  {
    id: "arrows",
    title: "Ctrl + Alt + flechas",
    text: "Pasar a la pantalla de al lado sin mover el mouse.",
    build: () =>
      ARROWS.map(([direction, key]) => ({
        keys: [CONTROL, ALT, key],
        actions: [{ ...newAction("switchInDirection", []), direction }],
      })),
  },
  {
    id: "numbers",
    title: "Ctrl + Alt + 1, 2, 3…",
    text: "Ir directo a cada computadora.",
    build: (computers) =>
      computers.slice(0, 9).map((name, i) => ({
        keys: [CONTROL, ALT, 0x31 + i],
        actions: [{ ...newAction("switchToScreen", computers), target: name }],
      })),
  },
  {
    id: "lock",
    title: "Ctrl + Alt + L",
    text: "Fijar o soltar el mouse en la pantalla actual.",
    build: () => [{ keys: [CONTROL, ALT, 0x4c], actions: [newAction("lockCursorToScreen", [])] }],
  },
];

/** Adds presets, skipping combos that are already taken. */
export function addPreset(current: Hotkey[], preset: Preset, computers: string[], server = ""): Hotkey[] {
  const fresh = preset.build(computers, server).filter((h) => !current.some((c) => sameKeys(c.keys, h.keys)));
  return [...current, ...fresh];
}
