/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2012 - 2025 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "DaemonApp.h"
#include "arch/OsDetection.h"

#include "arch/Arch.h"
#include "arch/Utf8.h"
#include "base/IEventQueue.h"
#include "base/Log.h"
#include "base/LogOutputters.h"
#include "common/Constants.h"
#include "common/CoreSettings.h"
#include "common/ExitCodes.h"
#include "common/IniFile.h"
#include "deskflow/ipc/DaemonIpcServer.h"

#if defined(DESKFLOW_OS_WIN)
#include "arch/win32/ArchDaemonWindows.h"
#include "deskflow/Computer.h"
#include "platform/MSWindowsDebugOutputter.h"
#include "platform/MSWindowsEventQueueBuffer.h"
#include "platform/MSWindowsWatchdog.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#endif

#include <algorithm>
#include <filesystem>

using namespace deskflow::core;

namespace {

#if defined(DESKFLOW_OS_WIN)
//! Where this program is, with forward slashes, as QCoreApplication::applicationDirPath()
std::string applicationDirPath()
{
  std::wstring path(MAX_PATH, L'\0');
  for (;;) {
    const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length < path.size()) {
      path.resize(length);
      break;
    }
    path.resize(path.size() * 2);
  }
  auto dir = deskflow::pathToUtf8(std::filesystem::path(path).parent_path());
  std::replace(dir.begin(), dir.end(), '\\', '/');
  return dir;
}
#endif

} // namespace

DaemonApp::DaemonApp(IEventQueue &events) : m_events(events)
{
  // do nothing
}

DaemonApp::~DaemonApp() = default;

void DaemonApp::saveLogLevel(const std::string &logLevel) const
{
  LOG_DEBUG("log level changed: %s", logLevel.c_str());
  CLOG->setFilter(logLevel);
  CoreSettings::setValue(CoreSettings::Daemon::LogLevel, logLevel);
}

void DaemonApp::setConfigFile(const std::string &configFile)
{
  LOG_DEBUG("config file updated: %s", configFile.c_str());
  {
    std::scoped_lock lock{m_configMutex};
    m_configFile = configFile;
  }
  CoreSettings::setValue(CoreSettings::Daemon::ConfigFile, configFile);
}

void DaemonApp::applyWatchdogCommand() const
{
#if defined(DESKFLOW_OS_WIN)
  std::string configFile;
  {
    std::scoped_lock lock{m_configMutex};
    configFile = m_configFile;
  }
  if (configFile.empty()) {
    LOG_ERR("cannot apply watchdog command: no config file set");
    return;
  }

  // Checking whether a UNC path exists triggers SMB auth from this SYSTEM-context
  // process, leaking the machine NTLM hash to whoever controls the remote host.
  // Any local user can reach this via the IPC pipe, so reject remote paths up front.
  if (configFile.starts_with(R"(\\)") || configFile.starts_with("//")) {
    LOG_ERR("cannot apply watchdog command: remote config file paths are not allowed: %s", configFile.c_str());
    return;
  }

  if (std::error_code ec; !std::filesystem::exists(deskflow::utf8Path(configFile), ec)) {
    LOG_ERR("cannot apply watchdog command: config file does not exist: %s", configFile.c_str());
    return;
  }

  const auto config = deskflow::IniFile::load(deskflow::utf8Path(configFile));
  const auto coreMode = config.value(CoreSettings::Core::CoreMode).toInt();
  const auto elevate = config.value(CoreSettings::Daemon::Elevate, !CoreSettings::isPortableMode()).toBool();

  std::string modeArg;
  if (coreMode == CoreSettings::CoreMode::Server) {
    modeArg = "server";
  } else if (coreMode == CoreSettings::CoreMode::Client) {
    modeArg = "client";
  } else {
    LOG_ERR("cannot apply watchdog command: invalid core mode in config: %d", coreMode);
    return;
  }

  const auto corePath = applicationDirPath() + "/" + kCoreBinName;
  const auto command = "\"" + corePath + "\" " + modeArg + " --settings \"" + configFile + "\"";

  LOG_DEBUG("applying watchdog command (elevate: %s)", elevate ? "yes" : "no");
  m_pWatchdog->setProcessConfig(command, elevate);
#else
  LOG_ERR("applying watchdog command not implemented on this platform");
#endif
}

void DaemonApp::clearWatchdogCommand()
{
  LOG_DEBUG("clearing watchdog command");

  // Clear the persisted config path so the daemon does not auto-start the core on next boot.
  {
    std::scoped_lock lock{m_configMutex};
    m_configFile.clear();
  }
  CoreSettings::setValue(CoreSettings::Daemon::ConfigFile);

#if defined(DESKFLOW_OS_WIN)
  m_pWatchdog->setProcessConfig("", false);
#else
  LOG_ERR("clearing watchdog command not implemented on this platform");
#endif
}

void DaemonApp::clearSettings()
{
  LOG_INFO("clearing daemon settings");
  {
    std::scoped_lock lock{m_configMutex};
    m_configFile.clear();
  }
  CoreSettings::setValue(CoreSettings::Daemon::ConfigFile);
  CoreSettings::setValue(CoreSettings::Daemon::LogFile);
  CoreSettings::setValue(CoreSettings::Daemon::LogLevel);
}

void DaemonApp::connectIpcServer(ipc::DaemonIpcServer &ipcServer)
{
  // The handlers run on the IPC thread, as the direct Qt connections did before.
  ipcServer.onLogLevelChanged = [this](const std::string &logLevel) { saveLogLevel(logLevel); };
  ipcServer.onConfigFileChanged = [this](const std::string &configFile) { setConfigFile(configFile); };
  ipcServer.onStartProcessRequested = [this] { applyWatchdogCommand(); };
  ipcServer.onStopProcessRequested = [this] { clearWatchdogCommand(); };
  ipcServer.onClearSettingsRequested = [this] { clearSettings(); };
}

void DaemonApp::run()
{
  LOG_INFO("starting daemon");

#if defined(DESKFLOW_OS_WIN)
  m_pWatchdog = std::make_unique<MSWindowsWatchdog>(m_foreground, *m_pFileLogOutputter);

  if (const auto persistedConfig = CoreSettings::value(CoreSettings::Daemon::ConfigFile).toString();
      !persistedConfig.empty()) {
    LOG_DEBUG("using last known config file: %s", persistedConfig.c_str());
    {
      std::scoped_lock lock{m_configMutex};
      m_configFile = persistedConfig;
    }
    applyWatchdogCommand();
  }
#endif

  if (m_foreground) {
    LOG_DEBUG("running daemon in foreground");
    mainLoop();
  } else {
    LOG_DEBUG("running daemon in background (daemonizing)");
    ARCH->daemonize([this] { return daemonLoop(); });
  }
  LOG_DEBUG("daemon finished");
}

int DaemonApp::daemonLoop()
{
#if defined(DESKFLOW_OS_WIN)
  // Runs the daemon through the Windows service controller, which controls the program lifecycle.
  return ArchDaemonWindows::runDaemon([this]() { return mainLoop(); });
#else
  return mainLoop();
#endif
}

int DaemonApp::mainLoop()
{
#if defined(DESKFLOW_OS_WIN)
  if (m_pWatchdog == nullptr) {
    LOG_ERR("watchdog not initialized");
    return s_exitFailed;
  }
  ArchDaemonWindows::daemonRunning(true);
#endif

  try {
#if defined(DESKFLOW_OS_WIN)
    // Install the platform event queue to handle service stop events.
    // This must be done on the same thread as the event loop, otherwise the service stop
    // request will not add the quit event to the event queue, and the service won't stop.
    m_events.adoptBuffer(new MSWindowsEventQueueBuffer(&m_events));

    LOG_DEBUG("starting watchdog threads");
    m_pWatchdog->startAsync();
#endif

    LOG_INFO("daemon is running");
    m_events.loop();
  } catch (std::exception &e) { // NOSONAR - Catching all exceptions
    LOG_CRIT("daemon error: %s", e.what());
  } catch (...) { // NOSONAR - Catching remaining exceptions
    LOG_CRIT("daemon unknown error");
  }

  LOG_INFO("daemon is stopping");

#if defined(DESKFLOW_OS_WIN)
  try {
    LOG_DEBUG("stopping process watchdog");
    m_pWatchdog->stop();
  } catch (std::exception &e) { // NOSONAR - Catching all exceptions
    LOG_CRIT("daemon stop watchdog error: %s", e.what());
  } catch (...) { // NOSONAR - Catching remaining exceptions
    LOG_CRIT("daemon stop watchdog unknown error");
  }
  ArchDaemonWindows::daemonRunning(false);
#endif

  return s_exitSuccess;
}

std::string DaemonApp::logFilename()
{
  return CoreSettings::value(CoreSettings::Daemon::LogFile).toString();
}

void DaemonApp::setForeground()
{
  m_foreground = true;
  showConsole();
}

void DaemonApp::initLogging()
{
#if defined(DESKFLOW_OS_WIN)
  if (!m_foreground) {
    // Only use MS debug outputter when the process is daemonized, since stdout won't be accessible
    // in that case, but is accessible when running in the foreground.
    CLOG->insert(new MSWindowsDebugOutputter()); // NOSONAR - Adopted by `Log`
  }
#endif

  m_pFileLogOutputter = new FileLogOutputter(logFilename()); // NOSONAR - Adopted by `Log`
  CLOG->insert(m_pFileLogOutputter);
}

void DaemonApp::showConsole()
{
#if defined(DESKFLOW_OS_WIN)
  // The daemon bin is compiled using the Win32 subsystem which works best for Windows services,
  // so when running as a foreground process we need to allocate a console (or we won't see output).
  // It is important to do this inside the arg check loop so that we can attach console ahead
  // of log output generated by handling other args.
  AllocConsole();
  freopen("CONOUT$", "w", stdout);
  freopen("CONOUT$", "w", stderr);
#endif
}
