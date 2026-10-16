/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2012 - 2016, 2025 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2012 Nick Bolton
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "DaemonApp.h"
#include "arch/OsDetection.h"

#include "arch/Arch.h"
#include "arch/Utf8.h"
#include "base/EventQueue.h"
#include "base/Log.h"
#include "common/Constants.h"
#include "common/CoreSettings.h"
#include "common/ExitCodes.h"
#include "common/VersionInfo.h"
#include "deskflow/ProcessUtils.h"
#include "deskflow/ipc/DaemonIpcServer.h"

#if defined(DESKFLOW_OS_WIN)
#include "arch/win32/ArchMiscWindows.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

#include <cstdio>
#include <string>

using namespace deskflow::core;

void handleError(const char *message = "Unrecognized error.");

namespace {

const auto kDaemonName = std::string(kAppName) + " Daemon";

const auto kHelpText = "Usage: " + std::string(kAppId) +
                       "-daemon [options]\n\n"
                       "Options:\n"
                       "  -h, --help        Displays help on commandline options.\n"
                       "  -v, --version     Displays version information.\n"
                       "  -f, --foreground  Run in the foreground (show console)\n";

//! Shows text where the user can see it: the daemon is a Win32 program without a console
void showMessage(const std::string &text, bool error = false)
{
#if defined(DESKFLOW_OS_WIN)
  if (!GetConsoleWindow()) {
    MessageBoxW(
        nullptr, deskflow::utf8ToWide(text).c_str(), deskflow::utf8ToWide(kDaemonName).c_str(),
        MB_OK | (error ? MB_ICONERROR : MB_ICONINFORMATION)
    );
    return;
  }
#endif
  std::fputs(text.c_str(), error ? stderr : stdout);
}

} // namespace

int main(int argc, char **argv)
{
#if defined(DESKFLOW_OS_WIN)
  ArchMiscWindows::guardRuntimeVersion();

  // Save window instance for later use, e.g. `GetModuleFileName` which is used when installing the daemon.
  ArchMiscWindows::setInstanceWin32(GetModuleHandle(nullptr));
#endif

  Arch arch;
  arch.init();

  Log log;
  EventQueue events;

  DaemonApp daemon(events);

  // The options QCommandLineParser handled before (process() also exits on --help,
  // --version and unknown options)
  bool foreground = false;
  const auto args = deskflow::commandLineArguments(argc, argv);
  for (size_t i = 1; i < args.size(); ++i) {
    const auto &arg = args[i];
    if (arg == "-h" || arg == "--help" || arg == "-help" || arg == "-?" || arg == "/?") {
      showMessage(kHelpText);
      return s_exitSuccess;
    }
    if (arg == "-v" || arg == "--version" || arg == "-version") {
      showMessage(kDaemonName + " " + kDisplayVersion + "\n");
      return s_exitSuccess;
    }
    if (arg == "-f" || arg == "--foreground" || arg == "-foreground") {
      foreground = true;
      continue;
    }
    showMessage("Unknown option '" + arg + "'.\n\n" + kHelpText, true);
    return s_exitArgs;
  }

  if (foreground) {
    daemon.setForeground();
  }

  // Depends on whether foreground option was set.
  daemon.initLogging();

  // Important: Log the app name and version number to the log file daemon app has initialized
  // logging as it creates the file logger. Logging before would only log to stdout which is not
  // useful for troubleshooting Windows services.
  // It's important to write the version number to the log file so we can be certain the old daemon
  // was uninstalled, since sometimes Windows services can get stuck and fail to be removed.
  LOG_PRINT("%s v%s", kDaemonName.c_str(), kDisplayVersion);
  LOG_INFO("settings file: %s", CoreSettings::settingsFile().c_str());

  // Default log level to system setting (found in Registry).
  auto logLevel = CoreSettings::value(CoreSettings::Daemon::LogLevel).toString();
  if (!logLevel.empty()) {
    CLOG->setFilter(logLevel.c_str());
    LOG_DEBUG("log level: %s", logLevel.c_str());
  }

  try {

#if defined(DESKFLOW_OS_WIN)
    // Show warning if not running as admin as daemon will behave differently.
    if (!ArchMiscWindows::isProcessElevated()) {
      LOG_WARN("not running as admin, some features may not work");
    }
#endif

    ipc::DaemonIpcServer ipcServer(DaemonApp::logFilename());
    daemon.connectIpcServer(ipcServer);
    ipcServer.listen();

    // The IPC server has its own thread; the daemon (and the service dispatcher) runs on this one.
    daemon.run();
    ipcServer.stop();

    LOG_DEBUG("daemon exited, code: %d", s_exitSuccess);
    return s_exitSuccess;

  } catch (std::exception &e) {
    handleError(e.what());
    return s_exitFailed;
  } catch (...) {
    handleError();
    return s_exitFailed;
  }
}

#if defined(DESKFLOW_OS_WIN)
// Win32 subsystem entry point (simply forwards to main).
// We need this because using regular main under the Win32 subsystem results in empty args.
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
  return main(__argc, __argv);
}

#endif

void handleError(const char *message)
{
  // Always print error to stdout in case run as CLI program.
  LOG_ERR("%s", message);

#if defined(DESKFLOW_OS_WIN)
  // Show a message box for when run from MSI in Win32 subsystem.
  MessageBoxA(nullptr, message, "Deskflow daemon error", MB_OK | MB_ICONERROR);
#endif
}
