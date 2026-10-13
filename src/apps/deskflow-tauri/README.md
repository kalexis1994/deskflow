# Deskflow – Tauri GUI prototype

An experimental replacement for `deskflow-gui`, built with Tauri 2 (Rust) and React.
It does not touch the core: it drives the same `deskflow-core` / `deskflow-daemon`
binaries the Qt GUI uses, through the same channels.

| Qt GUI (`src/lib/gui`)                 | Here (`src-tauri/src`)                   |
| -------------------------------------- | ---------------------------------------- |
| `ipc/IpcClient` (QLocalSocket)          | `ipc.rs` (named pipe / Unix socket)      |
| `core/CoreProcess`                      | `engine.rs` (state, log, start/stop)     |
| `common/Settings` (QSettings INI)       | `settings.rs` + `paths.rs`               |
| `config/ServerConfig` (grid)            | `layout.rs` (free-form layout)           |

## Running

Needs Rust, Node and pnpm, plus an installed Deskflow (for `deskflow-core`).
Set `DESKFLOW_DIR` if the binaries are not in the default install location.

```bash
pnpm install
pnpm tauri dev
```

To work on the UI in a normal browser, `pnpm dev` and open http://localhost:1420.
Outside Tauri a simulated backend (`src/devMock.ts`) is used, so nothing touches a
real Deskflow install. `?mode=client` starts in client mode and `?pending=name`
simulates an unknown computer trying to connect.

Tests: `pnpm test` (layout geometry) and `cargo test` in `src-tauri`.

## What works

- Attaches to a running core over IPC and shows its state live: connection,
  connected clients, TLS version, refusals, unknown clients, fingerprints, retries.
- When attaching to a core that is already running, the state is first estimated
  from the log (the core only announces changes), then corrected by live events.
- Start / stop through the daemon (service mode) or as a child process (desktop mode).
- Switch between server and client, and edit the server address (restarts the core).
- Activity view of the daemon log or the core output.
- Free-form screen layout editor (server mode):
  - screens are rectangles you move and resize; edges snap to other screens
    (Shift: free aspect ratio, Alt: no snapping), and screens never overlap
  - wherever two screens touch, a link is generated for exactly the shared part
    of the edge, with matching ranges on both sides (`right(50,100) = laptop`),
    so the cursor crosses at the proportional height even between different sizes
  - undo/redo, keyboard moves, zoom/pan, warnings for unreachable screens,
    one-click add for an unknown computer that tries to connect
  - the layout is kept in `deskflow-layout.json` next to the settings file; the
    core gets a generated `deskflow-server-layout.conf` (set as external config,
    keeping existing hotkeys) and one `[computer_<name>]` group per computer;
    the Qt GUI grid is kept roughly in sync

## Not yet

- Per-computer options (aliases, modifiers, switch corners) and hotkey editing.
- Accepting fingerprints, settings dialog, tray icon, autostart.
