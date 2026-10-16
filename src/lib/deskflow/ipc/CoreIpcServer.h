/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025-2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "IpcServer.h"

namespace deskflow::core::ipc {

class CoreIpcServer : public IpcServer
{
public:
  //! The name is only changed by tests, so they don't take over a running core's endpoint
  explicit CoreIpcServer(const std::string &serverName = {});
  ~CoreIpcServer() override;

  //! The core's server; only valid while one exists
  static CoreIpcServer &instance();
  static bool exists();

private:
  void processCommand(ClientId client, const std::string &command, const std::vector<std::string> &parts) override;
};

} // namespace deskflow::core::ipc
