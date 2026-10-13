// Browser preview: a fake backend so the UI can be developed and tested with
// `pnpm dev` in a normal browser, without touching a real Deskflow install.
// Only loaded in dev builds when not running inside Tauri (see main.tsx).

import { mockIPC } from "@tauri-apps/api/mocks";
import { emit } from "@tauri-apps/api/event";
import type { AppInfo, CoreState } from "./types";
import type { Screen } from "./layout/geometry";
import { defaultOptions, toggleMacSwap, type ComputerOptions } from "./layout/options";
import { DEFAULT_SERVER_OPTIONS, type ServerOptions } from "./ServerOptionsCard";
import type { Hotkey } from "./hotkeys/model";
import type { TlsInfo } from "./tls/Security";

let tlsInfo: TlsInfo = {
  enabled: true,
  checkPeers: true,
  localFingerprint: "a89ec7e43bd8aede07834acac760d9f8dcdf891c08745b3dc08ffe9319aa9fb5",
  certificate: "C:/ProgramData/Deskflow/tls/deskflow.pem",
  trustedServers: [],
  trustedClients: [{ kind: "sha256", hex: "1f0e2d3c4b5a69788796a5b4c3d2e1f00f1e2d3c4b5a69788796a5b4c3d2e1f0" }],
};

let hotkeys: Hotkey[] = [
  {
    keys: [0x04000000, 0x08000000, 0x01000014],
    actions: [{ kind: "switchInDirection", keys: [], computers: [], target: "", direction: "right", lockMode: "toggle", onRelease: false }],
  },
];

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
  fingerprintPrompt: params.get("fingerprint")
    ? { fingerprint: "4577ebbf2317c8d371fc408d2b72810c237473f774260d158bbb9eb6fe549dc1", mode: params.get("mode") === "client" ? "client" : "server" }
    : null,
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
      case "get_hotkeys":
        return { hotkeys, computers: screens.map((x) => x.name) };
      case "save_hotkeys":
        await new Promise((r) => setTimeout(r, 300));
        hotkeys = a!.hotkeys as Hotkey[];
        return { hotkeys, computers: screens.map((x) => x.name) };
      case "tls_info":
        return tlsInfo;
      case "trust_fingerprint":
        tlsInfo = {
          ...tlsInfo,
          [info.overview.coreMode === "server" ? "trustedClients" : "trustedServers"]: [
            ...(info.overview.coreMode === "server" ? tlsInfo.trustedClients : tlsInfo.trustedServers),
            { kind: "sha256", hex: a!.fingerprint as string },
          ],
        };
        core.fingerprintPrompt = null;
        emit("core-state", { ...core });
        return tlsInfo;
      case "reject_fingerprint":
        core.fingerprintPrompt = null;
        emit("core-state", { ...core });
        return null;
      case "forget_fingerprint": {
        const key = a!.list === "clients" ? "trustedClients" : "trustedServers";
        tlsInfo = { ...tlsInfo, [key]: tlsInfo[key].filter((t) => t.hex !== a!.hex) };
        return tlsInfo;
      }
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
