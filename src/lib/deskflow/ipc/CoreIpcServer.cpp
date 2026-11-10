/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025-2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "CoreIpcServer.h"

#include "base/Log.h"
#include "common/Constants.h"

#include <atomic>
#include <cassert>

namespace deskflow::core::ipc {

static std::atomic<CoreIpcServer *> s_instance = nullptr;

CoreIpcServer::CoreIpcServer(const std::string &serverName)
    : IpcServer(serverName.empty() ? std::string(kCoreIpcName) : serverName, "core")
{
  assert(s_instance == nullptr);
  s_instance = this;
}

CoreIpcServer::~CoreIpcServer()
{
  // no handler may run once this object is going away
  stop();
  s_instance = nullptr;
}

CoreIpcServer &CoreIpcServer::instance()
{
  assert(s_instance != nullptr);
  return *s_instance;
}

bool CoreIpcServer::exists()
{
  return s_instance != nullptr;
}

void CoreIpcServer::processCommand(ClientId client, const std::string &command, const std::vector<std::string> &)
{
  if (command == "stop") {
    LOG_DEBUG("core ipc server got stop message");
    writeToClient(client, "ok");
    broadcastCommand("bye");
    notify(onStopProcessRequested);
    return;
  }
  if (command == "serverLog") {
    notify(onServerLogRequested);
    return;
  }
  LOG_WARN("core ipc server got unknown command: %s", command.c_str());
}

} // namespace deskflow::core::ipc
