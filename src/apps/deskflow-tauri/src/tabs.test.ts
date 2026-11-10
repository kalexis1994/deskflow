import { describe, expect, it } from "vitest";
import { recentProblems, shownTab, tabsFor } from "./tabs";
import { parseLogLine } from "./status";

const ids = (mode: Parameters<typeof tabsFor>[0]) => tabsFor(mode).map((t) => t.id);

describe("tabs", () => {
  it("offers what each mode uses", () => {
    expect(ids("server")).toEqual(["home", "layout", "hotkeys", "clipboard", "options", "security", "activity"]);
    expect(ids("client")).toEqual(["home", "clipboard", "security", "activity"]);
    expect(ids("none")).toEqual(["home", "security", "activity"]);
  });

  it("goes back to Inicio when the mode doesn't have the chosen tab", () => {
    expect(shownTab("hotkeys", tabsFor("server"))).toBe("hotkeys");
    expect(shownTab("hotkeys", tabsFor("client"))).toBe("home");
    expect(shownTab(null, tabsFor("client"))).toBe("home");
    expect(shownTab("whatever", tabsFor("server"))).toBe("home");
  });

  it("counts only problems since the core last started", () => {
    const lines = [
      "[2026-11-10T12:00:00.000] WARNING: old",
      "[2026-11-10T12:00:01.000] INFO: starting core",
      "[2026-11-10T12:00:02.000] WARNING: failed to connect to server: Timed out",
      "[2026-11-10T12:00:03.000] INFO: connected",
    ].map(parseLogLine);
    expect(recentProblems(lines)).toBe(1);
  });
});
