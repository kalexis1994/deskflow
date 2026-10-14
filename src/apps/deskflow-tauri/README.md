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
- Per-computer options in the editor's inspector (Keyboard / Advanced tabs),
  written to `[computer_<name>]` exactly as the Qt GUI does and read by the core:
  modifier remapping (with a one-click Ctrl ⇄ ⌘ swap for Macs), lock-key fixes,
  aliases (validated to be unique across all names and aliases), dead corners
  with their size, and the two X11 options. Changes are part of undo/redo and
  are applied together with the layout.
- Global server options (server mode), the same `[server]` keys as the Qt server
  dialog with its limits: clipboard sharing and size, switch delay and double tap,
  Scroll Lock locking / start locked / relative moves, heartbeat, Synergy/Barrier
  compatibility and keeping the foreground window on Windows. Saving restarts a
  running server so the core picks them up.
- Hotkeys (server mode): record a combination, then pick what it does (go to a
  computer, move in a direction, next computer, lock the cursor, send keys to all
  or some computers, reconnect everything), optionally on release. One-click
  suggestions (Ctrl+Alt+arrows, Ctrl+Alt+1..9, Ctrl+Alt+L) and warnings for
  duplicates or broken targets. Stored in `internalConfig/hotkeys` exactly like the
  Qt GUI (Qt key codes), and written to the server config in core syntax. Unlike
  the Qt GUI, on-release actions are written after `;` so they actually run on
  release, punctuation keys are written by name, and actions pointing at computers
  that no longer exist are dropped (the core would reject the whole config);
  renaming or removing a computer in the layout updates the hotkeys too.
  Mouse-button hotkeys from the Qt GUI are kept but flagged: the core currently
  skips them (it looks for `mousepress` instead of `mousebutton`).

- TLS fingerprints: when the core reports a peer whose SHA-256 fingerprint is not
  in `trusted-servers` (client) or `trusted-clients` (server, if peer checking is
  on), a prompt shows it as the same "random art" picture the Qt GUI draws, next
  to this computer's own, so the person can compare them on both screens. Trusting
  writes a `v2:sha256:` line to the same database the core reads; the core keeps
  retrying, so the next attempt connects without a restart. Rejecting skips it for
  the session (a client stops instead of retrying). A Security card shows this
  computer's fingerprint and the trusted computers, which can be forgotten.

- Start before login (Windows service mode): the status card says whether the
  service will start the core on boot, at the login screen (it remembers the config
  while it runs the core, and forgets it when stopped, so stopping asks first).
  A hotkey suggestion sends Ctrl+Alt+Del to the other computers with
  Ctrl+Alt+End, since the real combination is caught by the server's own Windows;
  the client turns it into SendSAS through the service, which unlocks the login
  screen on Windows Server. Send-keys actions also get a Ctrl+Alt+Del button,
  as it cannot be recorded.

## Not yet

- Settings dialog (TLS on/off, key size, regenerating the certificate), tray icon, autostart.
