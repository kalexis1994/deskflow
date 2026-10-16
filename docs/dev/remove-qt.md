# Removing Qt from the core

Goal: `deskflow-core` and `deskflow-daemon` build and run without Qt, so the only
third-party build dependency on Windows is OpenSSL. The GUI is the Tauri app in
`src/apps/deskflow-tauri`, which already talks to the core through files and IPC only.

Constraints for every phase:

- the network protocol does not change (a Qt-free client must keep working with an
  unmodified Deskflow server, and the other way round);
- the settings file keeps the exact `QSettings` INI format, so existing installs,
  the Qt GUI and the Tauri app keep reading and writing the same file;
- the IPC line protocol (`hello=`, `connectionState=`, ...) does not change;
- each phase builds, passes the tests and is committed on its own.

## Phases

| # | Scope | Replacement | Status |
|---|-------|-------------|--------|
| 0 | Baseline build and tests on Windows (Qt 6.10 SDK, OpenSSL via vcpkg) | 29/29 tests pass | done |
| 1 | `Q_OS_*` macros (`QtSystemDetection`) | own `arch/OsDetection.h` (`DESKFLOW_OS_*`) with the same semantics | done |
| 2 | `QString`/`QByteArray`/`QFile`/`QDateTime` in `base`, `arch`, `io`, `mt`; exceptions and log outputters everywhere | `std::string` (UTF-8), `std::filesystem`, `<chrono>`; `arch/Utf8.h` for UTF-16 and paths; `common-base` (Qt-free headers) | done: `arch`, `base`, `io`, `mt` build without any Qt include path; the rest of `server`/`client` depends on `Settings` (4) and IPC (6) |
| 3 | `qDebug`/`qInfo`/`qWarning`/`qCritical` in the core | `LOG_*`; `CLOG` falls back to a console log when no `Log` exists; the Qt GUI forwards `LOG_*` to its Qt handler; GUI-only code (`I18N`, `KeySequence`, `Action`, `Hotkey`) moved from `common` to `gui` | done (the core's Qt message handler stays until 7) |
| 4 | `QSettings` and `Settings` (a `QObject` singleton) | `common/IniFile` (reads and writes `QSettings`' INI format: sections, `\`-keys, quoting, escapes, lists, `@Invalid()`) and `common/CoreSettings` (same keys and defaults, static API, `std::string`); `NetworkProtocol.h`, `KeyboardModifier.h`, `PlatformInfo.h` without Qt; the Qt `Settings` and `QSettingsProxy` moved to `gui` | done (`common` still links `Qt6::Core` for the moc'd enums in `Enums.h`/`Coordinate.h`, see 6) |
| 5 | `net`: `QCryptographicHash`, fingerprint database I/O | `Fingerprint` with its own `Type` and byte vector (OpenSSL already computed the digests), `FingerprintDatabase` on `std::fstream`, `SecureUtils` returning `std::string`; `ipcSendToClient` takes `std::string` | done: `net` has no Qt code and no longer links `Qt6::Network` (Qt headers still arrive through `deskflow/ipc/CoreIpc.h` until 6) |
| 5b | `platform`: `MSWindowsWatchdog` (process output decoding) on Windows; portals, DBus and X11 helpers on Linux; one macOS file | `Unicode::completeUTF8Length` for chunked output, `Unicode::UTF8ToLatin1`/`Latin1ToUTF8`, `base/Endian.h` for the BMP headers | Windows and macOS done (no Qt left in their `platform` code); X11 text converter done. Linux still uses Qt for portal clipboard images (`QImage`, `QDataStream`) and DBus (`XDGPortalRegistry`, `XDGPowerManager`, `XWindowsKeyState`), which need real replacements (GDBus, an image library) and a Linux build to test |
| 6 | IPC: `QLocalServer`/`QLocalSocket`, `IpcServer` signals | `deskflow/ipc/LocalServer` (overlapped named pipes on Windows, a `poll` loop on a Unix socket, its own thread) under the same line protocol; `std::function` handlers instead of signals; `Enums.h` and `Coordinate.h` without moc | done: on Windows `arch`, `base`, `io`, `mt`, `common`, `net`, `client`, `server` and `platform` compile without any Qt include path; only `app` and the two entry points still use Qt (7) |
| 7 | `deskflow-core`/`deskflow-daemon` entry points: `QApplication`, `QThread`, `QCommandLineParser` | plain `main`, `std::thread`, own argument parsing, the existing `EventQueue` | |
| 8 | Unit tests (`QtTest`) | a header-only framework | |
| 9 | Drop the Qt GUI (`src/lib/gui`, `src/apps/deskflow-gui`) from the build | the Tauri app | |

Linux-only follow-ups: `QDBus` in `platform/XDGPortalRegistry`, `XDGPowerManager`
and `XWindowsKeyState`.

## Settings in the core (phase 4)

`CoreSettings` reads the same `Deskflow.conf` as the GUI, picked the same way
(portable file, `XDG_CONFIG_HOME`, user file, system file), and returns the same
defaults. It differs from the GUI's `Settings` on purpose:

- it never rewrites the file on its own: no upgrading of old keys, no cleaning, no
  writing of the computer name (its default is the cleaned host name instead). It only
  writes the keys it is asked to set (the daemon's `daemon/*` keys, the portal tokens);
- changes edit the file in place, so other keys, their order and the GUI's values it
  doesn't understand (`@Rect(...)`, `@Variant(...)`) stay exactly as they were;
- the file is re-read when its timestamp or size changes, so values the GUI or the
  Tauri app saves are seen on the next read;
- keys compare case-insensitively on Windows and macOS, as `QSettings` does there;
- writes go through a temporary file and a rename, like `QSaveFile`, but don't take
  `QSettings`' `.lock` file.

`IniFileTests` checks the format both ways against `QSettings` itself.

## IPC without Qt (phase 6)

The endpoints are the ones `QLocalServer` used, so the Qt GUI, the Tauri app and
older clients keep connecting: `\\.\pipe\deskflow-core` and `deskflow-daemon` on
Windows, `$TMPDIR/<name>` (or `/tmp/<name>`) elsewhere. Differences, on purpose:

- the pipe's DACL gives Everyone read and write (`FRFW`) where `QLocalServer`'s
  world access gave `FILE_ALL_ACCESS`, which let any local user change the pipe's
  permissions or owner; SYSTEM, administrators and the server's account keep full
  access. Remote clients are rejected (`PIPE_REJECT_REMOTE_CLIENTS`); the daemon runs
  as SYSTEM and takes a config file path from its clients;
- a message split across reads is reassembled instead of dropped, and a message's
  argument is everything after the first `=` (a config path may contain `=`);
- writes are synchronous with a 5 s timeout, so a client that stops reading can't
  block the core forever;
- `ipcSendToClient` works from any thread and does nothing when no core IPC server
  exists (tests, the Qt GUI running shared code).

`IpcServerTests` talks to the server through `QLocalSocket`, the Qt GUI's client, and
checks the pipe's permissions. Its endpoints have their own names, so it never takes
over a running Deskflow's.

## Things Qt was quietly providing

Found while removing it; any library that stops linking Qt needs them from elsewhere:

- `Qt6::Platform` adds `UNICODE`, `_UNICODE`, `_ENABLE_EXTENDED_ALIGNED_STORAGE` and, on
  MSVC, `/utf-8 /permissive- /Zc:__cplusplus` to every consumer. They are now set for
  the whole project in `cmake/Libraries.cmake`, so code builds the same either way.
- Qt headers pull in `<cassert>`; files using `assert` now include it themselves.
- `QString`-based code sometimes compiled only because a Qt header arrived through
  `base/BaseException.h`.
- Libraries declared fewer link dependencies than they use (e.g. `arch` needs
  `version.lib`); it went unnoticed while every test also linked something bigger.
- Qt's `qt_windows.h` defines `NOMINMAX`; code that stops including Qt gets the
  `min`/`max` macros from `Windows.h` and needs `(std::min)(...)`.
- `net` linked `Qt6::Network` without using it; the Qt GUI got its `QNetworkInterface`
  through that and now links `Qt6::Network` itself.
- `common/Settings.h` brought `QString`, `QFile`, `QTextStream` and the `kAppName`
  constants into every file that read a setting; those files now include what they use.

## Building on Windows

Visual Studio 2022 Build Tools (MSVC, bundled vcpkg and Ninja), plus a Qt SDK until
phase 7 is done (`python -m aqt install-qt windows desktop 6.10.3 win64_msvc2022_64`):

```bat
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_TOOLCHAIN_FILE="%VCINSTALLDIR%vcpkg\scripts\buildsystems\vcpkg.cmake" ^
  -DCMAKE_PREFIX_PATH=C:\Qt\6.10.3\msvc2022_64 -DBUILD_INSTALLER=OFF -DSKIP_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build/src/unittests
```
