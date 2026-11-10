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
import type { UpdateChannel, UpdateState } from "./update/describe";

let tlsInfo: TlsInfo = {
  enabled: true,
  checkPeers: true,
  localFingerprint: "a89ec7e43bd8aede07834acac760d9f8dcdf891c08745b3dc08ffe9319aa9fb5",
  certificate: "C:/ProgramData/Deskflow/tls/deskflow.pem",
  trustedServers: [],
  trustedClients: [
    { kind: "sha256", hex: "1f0e2d3c4b5a69788796a5b4c3d2e1f00f1e2d3c4b5a69788796a5b4c3d2e1f0" },
    { kind: "sha256", hex: "9a8b7c6d5e4f30211203f4e5d6c7b8a99a8b7c6d5e4f30211203f4e5d6c7b8a9" },
  ],
  names: { "1f0e2d3c4b5a69788796a5b4c3d2e1f00f1e2d3c4b5a69788796a5b4c3d2e1f0": "laptop" },
};

let hotkeys: Hotkey[] = [
  {
    keys: [0x04000000, 0x08000000, 0x01000014],
    actions: [{ kind: "switchInDirection", keys: [], computers: [], target: "", direction: "right", lockMode: "toggle", onRelease: false }],
  },
];

let serverOptions: ServerOptions = { ...DEFAULT_SERVER_OPTIONS, win32KeepForeground: false };

const params = new URLSearchParams(location.search);

// ?update: a new version waiting for an answer
let updateState: UpdateState = params.has("update")
  ? {
      current: "0.1.8",
      channel: "stable",
      dismissed: false,
      kind: "available",
      version: "0.1.9",
      notes: "Copiar archivos entre computadoras.",
      error: params.get("update") === "failed" ? "No se pudo instalar la versión 0.1.9, y Deskflow sigue como estaba: acceso denegado" : null,
    }
  : { current: "0.1.8", channel: "stable", dismissed: false, kind: "idle" };
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
  gameMode: null,
  coreVersion: "1.27.0.5 · 3aec96d5",
  lastError: null,
  // ?progress: a copy of files on its way
  fileProgress: params.has("progress")
    ? { direction: "send", computer: "laptop", count: 12, bytes: 48_500_000, done: 17_900_000 }
    : null,
  clipboard:
    params.get("clipboard") === "empty"
      ? []
      : params.get("clipboard") === "files"
        ? [
            {
              event: "shared",
              computer: "laptop",
              reason: null,
              formats: ["files"],
              dropped: [],
              size: 48_500_000,
              chars: null,
              lines: null,
              image: null,
              text: null,
              files: { count: 12, folders: 2, bytes: 48_500_000, names: ["Fotos del viaje", "presupuesto.xlsx", "notas.txt"], roots: 5 },
              at: Date.now() - 6_000,
            },
            {
              event: "failed",
              computer: "notebook",
              reason: "noUser",
              detail: "the other computer stopped it: no user is logged on to receive the files",
              formats: ["files"],
              dropped: [],
              size: 1_200_000,
              chars: null,
              lines: null,
              image: null,
              text: null,
              files: { count: 1, folders: 0, bytes: 1_200_000, names: ["informe final.pdf"], roots: 1 },
              at: Date.now() - 90_000,
            },
          ]
      : params.get("mode") === "client"
        ? [
            {
              event: "received",
              computer: null,
              reason: null,
              formats: ["text", "HTML"],
              dropped: [],
              size: 1840,
              chars: 312,
              lines: 6,
              image: null,
              text: "Reunión del jueves\n- revisar el presupuesto de Q4\n- confirmar la sala 3B\n- mandar la minuta a todo el equipo antes del viernes",
              at: Date.now() - 4_000,
            },
            {
              event: "failed",
              computer: null,
              reason: "busy",
              formats: [],
              dropped: [],
              size: null,
              chars: null,
              lines: null,
              image: null,
              text: null,
              at: Date.now() - 120_000,
            },
          ]
        : [
          {
            event: "shared",
            computer: "laptop",
            reason: null,
            formats: ["text", "HTML"],
            dropped: ["bitmap"],
            size: 1840,
            chars: 312,
            lines: 6,
            image: null,
            text: "Reunión del jueves\n- revisar el presupuesto de Q4\n- confirmar la sala 3B\n- mandar la minuta a todo el equipo antes del viernes",
            at: Date.now() - 4_000,
          },
          {
            event: "shared",
            computer: "escritorio",
            reason: null,
            formats: ["bitmap"],
            dropped: [],
            size: 2_073_654,
            chars: null,
            lines: null,
            image: { width: 960, height: 540 },
            text: null,
            at: Date.now() - 95_000,
          },
          {
            event: "skipped",
            computer: "laptop",
            reason: "nothingToShare",
            formats: [],
            dropped: [],
            size: null,
            chars: null,
            lines: null,
            image: null,
            text: null,
            at: Date.now() - 600_000,
          },
        ],
};

let clipboardPrefs = { preview: true, sharing: true, limitMb: 3, files: true, filesLimitMb: 500 };

let languageChoice = "auto";

const log = ["[2026-10-13T08:00:00.000] INFO: starting core", "[2026-10-13T08:00:00.100] INFO: started server, waiting for clients"];

mockIPC(
  async (cmd, args) => {
    const a = args as Record<string, unknown> | undefined;
    switch (cmd) {
      case "language_choice":
        return languageChoice;
      case "set_language":
        languageChoice = (a as { choice: string }).choice;
        return null;
      case "update_state":
        return updateState;
      case "check_for_updates":
        updateState = { current: updateState.current, channel: updateState.channel, dismissed: false, kind: "upToDate" };
        void emit("update-state", updateState);
        return null;
      case "set_update_channel":
        updateState = { current: updateState.current, channel: (args as { channel: UpdateChannel }).channel, dismissed: false, kind: "upToDate" };
        void emit("update-state", updateState);
        return null;
      case "dismiss_update":
        updateState = { ...updateState, dismissed: true };
        void emit("update-state", updateState);
        return null;
      case "install_update":
        updateState = { current: updateState.current, channel: updateState.channel, dismissed: false, kind: "downloading", version: "0.1.9", percent: 40 };
        void emit("update-state", updateState);
        return null;
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
        return { hotkeys, computers: screens.map((x) => x.name), server: info.overview.computerName };
      case "save_hotkeys":
        await new Promise((r) => setTimeout(r, 300));
        hotkeys = a!.hotkeys as Hotkey[];
        return { hotkeys, computers: screens.map((x) => x.name), server: info.overview.computerName };
      case "autostart_status":
        return { supported: true, enabled: core.process === "running" };
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
      case "rename_trusted": {
        const names = { ...tlsInfo.names };
        const name = String(a!.name).trim();
        if (name) names[String(a!.hex)] = name;
        else delete names[String(a!.hex)];
        tlsInfo = { ...tlsInfo, names };
        return tlsInfo;
      }
      case "forget_fingerprint": {
        const key = a!.list === "clients" ? "trustedClients" : "trustedServers";
        tlsInfo = { ...tlsInfo, [key]: tlsInfo[key].filter((t) => t.hex !== a!.hex) };
        return tlsInfo;
      }
      case "clipboard_prefs":
        return clipboardPrefs;
      case "set_clipboard_preview":
        clipboardPrefs = { ...clipboardPrefs, preview: a!.enabled as boolean };
        return clipboardPrefs;
      case "set_file_sharing":
        clipboardPrefs = { ...clipboardPrefs, files: a!.enabled as boolean, filesLimitMb: a!.limitMb as number };
        return clipboardPrefs;
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
