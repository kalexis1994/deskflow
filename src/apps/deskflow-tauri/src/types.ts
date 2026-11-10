// Mirrors the structs serialized by src-tauri/src/{settings,engine,lib}.rs

import type { ClipboardActivity, FileProgress } from "./clipboard/describe";

export type CoreMode = "none" | "client" | "server";
export type ProcessMode = "service" | "desktop";

export interface Overview {
  settingsFile: string;
  computerName: string;
  coreMode: CoreMode;
  processMode: ProcessMode;
  remoteHost: string;
  port: number;
  tlsEnabled: boolean;
  portable: boolean;
}

export interface AppInfo {
  overview: Overview;
  coreFound: boolean;
  coreDir: string | null;
}

export type IpcStatus = "disconnected" | "connected" | "versionMismatch";
export type ProcessStatus = "stopped" | "starting" | "running" | "stopping";
export type ConnectionState = "Disconnected" | "Connecting" | "Connected" | "Listening";
export type ConnectionRefusal = "IncompatibleVersion" | "AlreadyConnected" | "UnknownClient" | "ProtocolError";

export interface CoreState {
  ipc: IpcStatus;
  process: ProcessStatus;
  connection: ConnectionState | null;
  connectionInferred: boolean;
  clients: string[];
  tls: string | null;
  peerFingerprint: string | null;
  fingerprintPrompt: FingerprintPrompt | null;
  unrecognisedClient: string | null;
  refusal: ConnectionRefusal | null;
  retryAt: number | null;
  missingLayouts: string | null;
  /** The computer whose game has the mouse, while the server follows it. */
  gameMode?: string | null;
  /** Each computer's monitors (x, y, width, height in pixels) as the server last heard. */
  monitors?: Record<string, [number, number, number, number][]>;
  /** The server this client connects to shows its log when asked. */
  serverLogAvailable?: boolean;
  coreVersion: string | null;
  lastError: string | null;
  /** Recent shared clipboard activity, newest first. */
  clipboard: ClipboardActivity[];
  /** A copy of files on its way, while it is. */
  fileProgress: FileProgress | null;
}

export interface FingerprintPrompt {
  /** Lower-case hex SHA-256. */
  fingerprint: string;
  /** Who is asking: a client meeting a new server, or a server meeting a new client. */
  mode: CoreMode;
}

export interface Autostart {
  /** Only the Windows service can start Deskflow before anyone logs in. */
  supported: boolean;
  /** Deskflow starts on boot, already at the login screen. */
  enabled: boolean;
}

export interface Changes {
  coreMode?: CoreMode;
  remoteHost?: string;
}
