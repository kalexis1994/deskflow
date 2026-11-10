import { useCallback, useReducer } from "react";
import type { Screen } from "./geometry";
import type { ComputerOptions } from "./options";

export interface EditScreen extends Screen {
  /** Stable id while editing; names can change. */
  id: string;
  /** Name when loaded from disk, to report renames on save. null for new screens. */
  orig: string | null;
  options: ComputerOptions;
}

interface History {
  past: EditScreen[][];
  present: EditScreen[];
  future: EditScreen[][];
  /** Snapshot that matches what is saved on disk. */
  saved: EditScreen[];
}

type Action =
  | { type: "reset"; screens: EditScreen[] }
  | { type: "commit"; screens: EditScreen[] }
  | { type: "undo" }
  | { type: "redo" }
  | { type: "revert" };

const LIMIT = 100;

function reducer(h: History, a: Action): History {
  switch (a.type) {
    case "reset":
      return { past: [], present: a.screens, future: [], saved: a.screens };
    case "commit":
      if (same(a.screens, h.present)) return h;
      return { ...h, past: [...h.past, h.present].slice(-LIMIT), present: a.screens, future: [] };
    case "undo":
      if (!h.past.length) return h;
      return { ...h, past: h.past.slice(0, -1), present: h.past[h.past.length - 1], future: [h.present, ...h.future] };
    case "redo":
      if (!h.future.length) return h;
      return { ...h, past: [...h.past, h.present], present: h.future[0], future: h.future.slice(1) };
    case "revert":
      return { ...h, past: [...h.past, h.present], present: h.saved, future: [] };
  }
}

function same(a: EditScreen[], b: EditScreen[]): boolean {
  return a.length === b.length && a.every((s, i) => {
    const t = b[i];
    return (
      s.id === t.id &&
      s.name === t.name &&
      s.x === t.x &&
      s.y === t.y &&
      s.w === t.w &&
      s.h === t.h &&
      JSON.stringify(s.monitors ?? []) === JSON.stringify(t.monitors ?? []) &&
      (s.options === t.options || JSON.stringify(s.options) === JSON.stringify(t.options))
    );
  });
}

export function useLayoutHistory() {
  const [h, dispatch] = useReducer(reducer, { past: [], present: [], future: [], saved: [] });
  return {
    screens: h.present,
    canUndo: h.past.length > 0,
    canRedo: h.future.length > 0,
    dirty: !same(h.present, h.saved),
    reset: useCallback((screens: EditScreen[]) => dispatch({ type: "reset", screens }), []),
    commit: useCallback((screens: EditScreen[]) => dispatch({ type: "commit", screens }), []),
    undo: useCallback(() => dispatch({ type: "undo" }), []),
    redo: useCallback(() => dispatch({ type: "redo" }), []),
    revert: useCallback(() => dispatch({ type: "revert" }), []),
  };
}
