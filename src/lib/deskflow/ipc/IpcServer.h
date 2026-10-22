/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025-2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/ipc/LocalServer.h"

#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace deskflow::core::ipc {

//! The line protocol the GUI talks to the core and daemon with
/*!
Messages are UTF-8 lines, `command` or `command=args`. A client first sends
`hello=<version>+<sha>` and gets `hello=...` back (or `versionMismatch=...`), then
any messages broadcast before it connected.

The handlers run on the IPC thread; broadcastCommand() may be called from any thread.
*/
class IpcServer
{
public:
  using ClientId = LocalServer::ClientId;

  explicit IpcServer(const std::string &serverName, const std::string &typeName);
  virtual ~IpcServer();

  IpcServer(const IpcServer &) = delete;
  IpcServer &operator=(const IpcServer &) = delete;

  void listen();

  //! Stops serving and waits for the IPC thread, so no handler runs after this
  void stop();

  //! Sends to every client, or queues for the next one. With latestOnly, a queued
  //! message replaces earlier queued ones of the same command (state, not history).
  void broadcastCommand(const std::string &command, const std::string &args = {}, bool latestOnly = false);

  // Requests from clients; set them before listen()
  std::function<void(const std::string &logLevel)> onLogLevelChanged;
  std::function<void(const std::string &configFile)> onConfigFileChanged;
  std::function<void()> onStartProcessRequested;
  std::function<void()> onStopProcessRequested;
  std::function<void()> onClearSettingsRequested;

protected:
  //! Writes a message to a client, adding the newline
  void writeToClient(ClientId client, const std::string &message);

  //! Calls a handler if it is set
  template <typename Handler, typename... Args> static void notify(const Handler &handler, Args &&...args)
  {
    if (handler)
      handler(std::forward<Args>(args)...);
  }

  const std::string &typeName() const
  {
    return m_typeName;
  }

private:
  void handleConnected(ClientId client);
  void handleReceived(ClientId client, std::string_view data);
  void handleDisconnected(ClientId client);
  void processMessage(ClientId client, const std::string &message);

  //! `parts` is the command and, when the message has an `=`, everything after it
  virtual void processCommand(ClientId client, const std::string &command, const std::vector<std::string> &parts) = 0;

  std::string m_serverName;
  std::string m_typeName;

  std::mutex m_mutex; // guards m_clients and m_pendingMessages
  std::set<ClientId> m_clients;
  std::vector<std::string> m_pendingMessages;
  std::map<ClientId, std::string> m_partialLines; // IPC thread only

  LocalServer m_server;
};

} // namespace deskflow::core::ipc
