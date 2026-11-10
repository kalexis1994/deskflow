# The server's log, from a client

A problem usually shows on a client, but the server decides most of what happens (when
to switch, what to share), and its log is a file on the server's own computer
(`C:\ProgramData\Deskflow\deskflow-daemon.log` under the service). So a client can read
it from its **Actividad** tab, choosing **Servidor**.

## How

- Every core keeps its newest 2000 log lines in memory (`RecentLogOutputter`, added
  when logging is set up).
- A server offers the `SLOG` option to its clients unless `server/shareLog` is off
  (**Mostrar el registro a las computadoras conectadas** in Opciones).
- The app asks its core (`serverLog` on the core's IPC); the client sends `QSLG`; the
  server answers `DSLG` with the newest lines that fit in 256 KiB; the client's core
  hands them to the app as a JSON string (`serverLog`).
- While the server's log is on screen the app asks every 5 seconds. It also writes the
  last one to `%LOCALAPPDATA%\org.deskflow.tauri\server-log.txt`, where it can be read
  without the app.

Only clients that passed the TLS fingerprint check are connected, so only computers the
server trusts can read it. The log names computers and copied files, not clipboard
contents.
