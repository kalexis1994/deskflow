// Qt key codes, which is how hotkeys are stored (see src-tauri/src/hotkeys.rs).
// Values from Qt's qnamespace.h.

export const SHIFT = 0x02000000;
export const CONTROL = 0x04000000;
export const ALT = 0x08000000;
export const META = 0x10000000;
const KEYPAD = 0x20000000;
const KEY_F1 = 0x01000030;

export const MODIFIERS = [CONTROL, ALT, SHIFT, META] as const;

export const isModifier = (k: number) => k === SHIFT || k === CONTROL || k === ALT || k === META;
export const isMouseButton = (k: number) => k > 0 && k < 0x20;

/** Browser `KeyboardEvent.key` names to Qt keys. */
const NAMED: Record<string, number> = {
  " ": 0x20,
  Escape: 0x01000000,
  Tab: 0x01000001,
  Backspace: 0x01000003,
  Enter: 0x01000004,
  Insert: 0x01000006,
  Delete: 0x01000007,
  Pause: 0x01000008,
  PrintScreen: 0x01000009,
  Clear: 0x0100000b,
  Home: 0x01000010,
  End: 0x01000011,
  ArrowLeft: 0x01000012,
  ArrowUp: 0x01000013,
  ArrowRight: 0x01000014,
  ArrowDown: 0x01000015,
  PageUp: 0x01000016,
  PageDown: 0x01000017,
  CapsLock: 0x01000024,
  NumLock: 0x01000025,
  ScrollLock: 0x01000026,
  ContextMenu: 0x01000055,
  Help: 0x01000058,
  BrowserBack: 0x01000061,
  BrowserForward: 0x01000062,
  BrowserStop: 0x01000063,
  BrowserRefresh: 0x01000064,
  AudioVolumeDown: 0x01000070,
  AudioVolumeMute: 0x01000071,
  AudioVolumeUp: 0x01000072,
  MediaPlayPause: 0x01000080,
  MediaStop: 0x01000081,
  MediaTrackPrevious: 0x01000082,
  MediaTrackNext: 0x01000083,
  BrowserHome: 0x01000090,
  BrowserFavorites: 0x01000091,
  BrowserSearch: 0x01000092,
  Standby: 0x01000093,
  LaunchMail: 0x010000a0,
  LaunchMediaPlayer: 0x010000a1,
  LaunchApplication1: 0x010000a2,
  LaunchApplication2: 0x010000a3,
  Select: 0x01010000,
};

const KP_ENTER = 0x01000005;

const LABELS: Record<number, string> = {
  [CONTROL]: "Ctrl",
  [ALT]: "Alt",
  [SHIFT]: "Shift",
  [META]: "Win/⌘",
  0x20: "Espacio",
  0x01000000: "Esc",
  0x01000001: "Tab",
  0x01000002: "Tab",
  0x01000003: "Retroceso",
  0x01000004: "Enter",
  [KP_ENTER]: "Enter (numérico)",
  0x01000006: "Insert",
  0x01000007: "Supr",
  0x01000008: "Pausa",
  0x01000009: "Impr Pant",
  0x0100000a: "PetSis",
  0x0100000b: "Borrar",
  0x01000010: "Inicio",
  0x01000011: "Fin",
  0x01000012: "←",
  0x01000013: "↑",
  0x01000014: "→",
  0x01000015: "↓",
  0x01000016: "RePág",
  0x01000017: "AvPág",
  0x01000024: "Bloq Mayús",
  0x01000025: "Bloq Num",
  0x01000026: "Bloq Despl",
  0x01000055: "Menú",
  0x01000058: "Ayuda",
  0x01000061: "Atrás",
  0x01000062: "Adelante",
  0x01000063: "Detener",
  0x01000064: "Actualizar",
  0x01000070: "Volumen −",
  0x01000071: "Silencio",
  0x01000072: "Volumen +",
  0x01000080: "Reproducir",
  0x01000081: "Detener medio",
  0x01000082: "Anterior",
  0x01000083: "Siguiente",
  0x01000090: "Inicio web",
  0x01000091: "Favoritos",
  0x01000092: "Buscar",
  0x01000093: "Suspender",
  0x010000a0: "Correo",
  0x010000a1: "Multimedia",
  0x010000a2: "App 1",
  0x010000a3: "App 2",
  0x01010000: "Seleccionar",
};

/** Label for one key of a sequence, as shown on a keycap. */
export function keyLabel(key: number): string {
  if (isMouseButton(key)) return `Botón ${key === 4 ? 3 : key === 1 || key === 2 ? key : 4} del mouse`;
  const k = isModifier(key) ? key : key & ~KEYPAD;
  if (LABELS[k]) return LABELS[k];
  if (k >= KEY_F1 && k <= KEY_F1 + 34) return `F${k - KEY_F1 + 1}`;
  if (k > 0 && k < 0x10000) return String.fromCodePoint(k).toUpperCase();
  return "?";
}

export interface KeyEventLike {
  key: string;
  code: string;
  ctrlKey: boolean;
  altKey: boolean;
  shiftKey: boolean;
  metaKey: boolean;
}

/** Main (non-modifier) key of a keyboard event as a Qt key, or null if it cannot be used. */
export function qtKeyFromEvent(e: KeyEventLike): number | null {
  if (["Control", "Alt", "Shift", "Meta", "AltGraph", "OS", "Super", "Hyper", "Dead", "Unidentified", "Process"].includes(e.key)) return null;
  // letters and digits by physical key, so Ctrl+Alt+A is not read as whatever AltGr+A types
  let m = /^Key([A-Z])$/.exec(e.code);
  if (m) return m[1].charCodeAt(0);
  m = /^(?:Digit|Numpad)([0-9])$/.exec(e.code);
  if (m) return m[1].charCodeAt(0);
  if (e.code === "NumpadEnter") return KP_ENTER;
  m = /^F([0-9]{1,2})$/.exec(e.key);
  if (m && Number(m[1]) >= 1 && Number(m[1]) <= 35) return KEY_F1 + Number(m[1]) - 1;
  if (e.key in NAMED) return NAMED[e.key];
  // any other printable character, as the layout produces it (Qt uses upper case for letters)
  const chars = [...e.key];
  if (chars.length === 1) {
    const code = chars[0].toUpperCase().codePointAt(0)!;
    if (code > 0x20 && code < 0x10000) return code;
  }
  return null;
}

/** Full sequence for a keyboard event: held modifiers, then the key. */
export function sequenceFromEvent(e: KeyEventLike): number[] | null {
  const key = qtKeyFromEvent(e);
  if (key === null) return null;
  const mods: number[] = [];
  if (e.ctrlKey) mods.push(CONTROL);
  if (e.altKey) mods.push(ALT);
  if (e.shiftKey) mods.push(SHIFT);
  if (e.metaKey) mods.push(META);
  return [...mods, key];
}

/** Modifiers currently held, to show while recording. */
export function heldModifiers(e: KeyEventLike): number[] {
  return MODIFIERS.filter((m) => (m === CONTROL && e.ctrlKey) || (m === ALT && e.altKey) || (m === SHIFT && e.shiftKey) || (m === META && e.metaKey));
}

export const hasMainKey = (keys: number[]) => keys.length > 0 && !isModifier(keys[keys.length - 1]);

export const sameKeys = (a: number[], b: number[]) =>
  a.length === b.length && [...a].sort((x, y) => x - y).every((k, i) => k === [...b].sort((x, y) => x - y)[i]);

export const comboLabel = (keys: number[]) => keys.map(keyLabel).join(" + ");
