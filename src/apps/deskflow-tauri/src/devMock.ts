// Browser preview: a fake backend so the UI can be developed and tested with
// `pnpm dev` in a normal browser, without touching a real Deskflow install.
// Only loaded in dev builds when not running inside Tauri (see main.tsx).

import { mockIPC } from "@tauri-apps/api/mocks";
import { emit } from "@tauri-apps/api/event";
import type { AppInfo, CoreState } from "./types";
import type { Screen } from "./layout/geometry";
import { defaultOptions, toggleMacSwap, type ComputerOptions } from "./layout/options";
import { DEFAULT_SERVER_OPTIONS, type ServerOptions } from "./ServerOptionsCard";

let serverOptions: ServerOptions = { ...DEFAULT_SERVER_OPTIONS, win32KeepForeground: false };

const params = new URLSearchParams(location.search);
type MockScreen = Screen & { options: ComputerOptions };
let screens: MockScreen[] = [
  { name: "escritorio", x: 0, y: 0, w: 192, h: 108, options: defaultOptions() },
  { name: "laptop", x: 192, y: 40, w: 128, h: 80, options: { ...toggleMacSwap(defaultOptions()), aliases: ["macbook"] } },
];

const info: AppInfo = {
  overview: {
    settingsFile: "C:/Users/demo/AppData/Roaming/Deskflow/Deskflow.conf",
    computerName: "escritorio",
    coreMode: params.get("mode") === "client" ? "client" : "server",
    processMode: "service",
    remoteHost: "192.168.1.60",
    port: 24800,
    tlsEnabled: true,
    portable: false,
  },
  coreFound: true,
  coreDir: "C:/Program Files/Deskflow",
};

const core: CoreState = {
  ipc: "connected",
  process: "running",
  connection: "Connected",
  connectionInferred: false,
  clients: ["laptop"],
  tls: "TLSv1.3",
  peerFingerprint: null,
  unrecognisedClient: params.get("pending") ?? null,
  refusal: null,
  retryAt: null,
  missingLayouts: null,
  coreVersion: "1.27.0.5 · 3aec96d5",
  lastError: null,
};

const log = ["[2026-10-13T08:00:00.000] INFO: starting core", "[2026-10-13T08:00:00.100] INFO: started server, waiting for clients"];

mockIPC(
  async (cmd, args) => {
    const a = args as Record<string, unknown> | undefined;
    switch (cmd) {
      case "app_info":
        return info;
      case "core_state":
        return core;
      case "core_log":
        return log;
      case "local_address":
        return "192.168.1.50";
      case "get_layout":
        return { screens, serverName: info.overview.computerName, foreignConfig: null, imported: false };
      case "save_layout": {
        screens = a!.screens as MockScreen[];
        for (const r of a!.renames as { from: string; to: string }[]) {
          if (r.from === info.overview.computerName) info.overview.computerName = r.to;
        }
        await new Promise((r) => setTimeout(r, 300));
        if (core.unrecognisedClient && screens.some((s) => s.name === core.unrecognisedClient)) {
          core.clients = [...core.clients, core.unrecognisedClient];
          core.unrecognisedClient = null;
          emit("core-state", { ...core });
        }
        return { screens, serverName: info.overview.computerName, foreignConfig: null, imported: false };
      }
      case "get_server_options":
        return serverOptions;
      case "save_server_options":
        await new Promise((r) => setTimeout(r, 300));
        serverOptions = a!.options as ServerOptions;
        return serverOptions;
      case "start_core":
      case "stop_core":
        return null;
      case "save_settings":
        Object.assign(info.overview, a!.changes);
        return info.overview;
    }
    throw new Error(`devMock: unknown command ${cmd}`);
  },
  { shouldMockEvents: true },
);

console.info("[devMock] running with a simulated Deskflow backend");
