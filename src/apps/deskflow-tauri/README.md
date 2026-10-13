# Deskflow – Tauri GUI prototype

An experimental replacement for `deskflow-gui`, built with Tauri 2 (Rust) and React.
It does not touch the core: it drives the same `deskflow-core` / `deskflow-daemon`
binaries the Qt GUI uses, through the same channels.

| Qt GUI (`src/lib/gui`)                 | Here (`src-tauri/src`)                   |
| -------------------------------------- | ---------------------------------------- |
| `ipc/IpcClient` (QLocalSocket)          | `ipc.rs` (named pipe / Unix socket)      |
| `core/CoreProcess`                      | `engine.rs` (state, log, start/stop)     |
| `common/Settings` (QSettings INI)       | `settings.rs` + `paths.rs`               |

## Running

Needs Rust, Node and pnpm, plus an installed Deskflow (for `deskflow-core`).
Set `DESKFLOW_DIR` if the binaries are not in the default install location.

```bash
pnpm install
pnpm tauri dev
```

## What works

- Attaches to a running core over IPC and shows its state live: connection,
  connected clients, TLS version, refusals, unknown clients, fingerprints, retries.
- When attaching to a core that is already running, the state is first estimated
  from the log (the core only announces changes), then corrected by live events.
- Start / stop through the daemon (service mode) or as a child process (desktop mode).
- Switch between server and client, and edit the server address (restarts the core).
- Activity view of the daemon log or the core output.

## Not yet

- Screen layout editor for server mode (the server config file must already exist).
- Accepting fingerprints, settings dialog, tray icon, autostart.
