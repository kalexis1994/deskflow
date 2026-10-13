// Mirrors the structs serialized by src-tauri/src/{settings,engine,lib}.rs

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
  unrecognisedClient: string | null;
  refusal: ConnectionRefusal | null;
  retryAt: number | null;
  missingLayouts: string | null;
  coreVersion: string | null;
  lastError: string | null;
}

export interface Changes {
  coreMode?: CoreMode;
  remoteHost?: string;
}
