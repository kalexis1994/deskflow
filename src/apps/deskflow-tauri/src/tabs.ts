import type { CoreMode } from "./types";
import type { parseLogLine } from "./status";

export type TabId = "home" | "layout" | "hotkeys" | "clipboard" | "options" | "security" | "activity" | "about";

export interface Tab {
  id: TabId;
}

/** The tabs that make sense for what this computer does, in order; `common.tab.<id>` names them. */
export function tabsFor(mode: CoreMode): Tab[] {
  const ids: TabId[] = ["home"];
  if (mode === "server") ids.push("layout", "hotkeys");
  if (mode !== "none") ids.push("clipboard");
  if (mode === "server") ids.push("options");
  ids.push("security", "activity", "about");
  return ids.map((id) => ({ id }));
}

/** The chosen tab, or Inicio when this mode doesn't have it (say, Atajos on a client). */
export function shownTab(chosen: string | null, tabs: Tab[]): TabId {
  return tabs.find((t) => t.id === chosen)?.id ?? "home";
}

/** Warnings and errors since the core last started: older ones are history. */
export function recentProblems(lines: ReturnType<typeof parseLogLine>[]): number {
  const sinceStart = Math.max(0, lines.map((l) => l.text).lastIndexOf("starting core"));
  return lines.slice(sinceStart).filter((l) => l.level === "error" || l.level === "warn").length;
}

const KEY = "deskflow.tab";

export function savedTab(): string | null {
  try {
    return localStorage.getItem(KEY);
  } catch {
    return null;
  }
}

export function saveTab(id: TabId) {
  try {
    localStorage.setItem(KEY, id);
  } catch {
    // only a convenience
  }
}
