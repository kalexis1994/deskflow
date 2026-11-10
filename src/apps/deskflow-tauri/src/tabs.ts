import type { CoreMode } from "./types";
import type { parseLogLine } from "./status";

export type TabId = "home" | "layout" | "hotkeys" | "clipboard" | "options" | "security" | "activity";

export interface Tab {
  id: TabId;
  label: string;
}

/** The tabs that make sense for what this computer does, in order. */
export function tabsFor(mode: CoreMode): Tab[] {
  const tabs: Tab[] = [{ id: "home", label: "Inicio" }];
  if (mode === "server") tabs.push({ id: "layout", label: "Pantallas" }, { id: "hotkeys", label: "Atajos" });
  if (mode !== "none") tabs.push({ id: "clipboard", label: "Portapapeles" });
  if (mode === "server") tabs.push({ id: "options", label: "Opciones" });
  tabs.push({ id: "security", label: "Seguridad" }, { id: "activity", label: "Actividad" });
  return tabs;
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
