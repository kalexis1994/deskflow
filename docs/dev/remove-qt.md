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
| 4 | `QSettings` and `Settings` (a `QObject` singleton) | INI reader/writer compatible with `QSettings` (arrays, quoting, `@Invalid()`, lists) | |
| 5 | `net`: `QCryptographicHash`, fingerprint database I/O | OpenSSL `EVP_Digest`, `std::fstream` | |
| 5b | `platform`: `MSWindowsWatchdog` (process output decoding) on Windows; portals, DBus and X11 helpers on Linux; one macOS file | `std::string`, `arch/Utf8.h` | |
| 6 | IPC: `QLocalServer`/`QLocalSocket`, `IpcServer` signals | named pipes (Windows) and Unix sockets, same line protocol | |
| 7 | `deskflow-core`/`deskflow-daemon` entry points: `QApplication`, `QThread`, `QCommandLineParser` | plain `main`, `std::thread`, own argument parsing, the existing `EventQueue` | |
| 8 | Unit tests (`QtTest`) | a header-only framework | |
| 9 | Drop the Qt GUI (`src/lib/gui`, `src/apps/deskflow-gui`) from the build | the Tauri app | |

Linux-only follow-ups: `QDBus` in `platform/XDGPortalRegistry`, `XDGPowerManager`
and `XWindowsKeyState`.

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
