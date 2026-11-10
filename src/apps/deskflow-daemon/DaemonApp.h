/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2012 - 2025 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "arch/OsDetection.h"

#include <memory>
#include <mutex>
#include <string>

class Event;
class IEventQueue;
class FileLogOutputter;

namespace deskflow::core::ipc {
class DaemonIpcServer;
}

#if defined(DESKFLOW_OS_WIN)
class MSWindowsWatchdog;
#endif

extern const char *const kLogFilename;

class DaemonApp
{
public:
  explicit DaemonApp(IEventQueue &events);
  ~DaemonApp();

  DaemonApp(const DaemonApp &) = delete;
  DaemonApp &operator=(const DaemonApp &) = delete;

  //! Runs the daemon (as a service unless in the foreground) on the calling thread until it stops
  void run();
  void setForeground();
  void initLogging();
  void connectIpcServer(deskflow::core::ipc::DaemonIpcServer &ipcServer);

  static std::string logFilename();

private:
  void daemonize();
  void handleError(const char *message);
  int mainLoop();
  int daemonLoop();
  void saveLogLevel(const std::string &logLevel) const;
  void setConfigFile(const std::string &configFile);
  void applyWatchdogCommand() const;
  void clearWatchdogCommand();
  void clearSettings();
  std::string installUpdate(const std::string &installer);

  static void showConsole();

#if defined(DESKFLOW_OS_WIN)
  std::unique_ptr<MSWindowsWatchdog> m_pWatchdog;
#endif

  IEventQueue &m_events;
  FileLogOutputter *m_pFileLogOutputter = nullptr;
  deskflow::core::ipc::DaemonIpcServer *m_ipcServer = nullptr;
  mutable std::mutex m_configMutex; // the IPC thread and the daemon thread both use the config file
  std::string m_configFile;
  bool m_foreground = false;
};
