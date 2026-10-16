/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025-2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "IpcServer.h"

#include <string>

namespace deskflow::core::ipc {

class DaemonIpcServer : public IpcServer
{
public:
  //! The name is only changed by tests, so they don't take over a running daemon's endpoint
  explicit DaemonIpcServer(const std::string &logFilename, const std::string &serverName = {});
  ~DaemonIpcServer() override;

private:
  void processCommand(ClientId client, const std::string &command, const std::vector<std::string> &parts) override;
  void processLogLevel(ClientId client, const std::vector<std::string> &messageParts);
  void processConfigFile(ClientId client, const std::vector<std::string> &messageParts);

  const std::string m_logFilename;
};

} // namespace deskflow::core::ipc
