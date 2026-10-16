/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "CoreIpc.h"

#include "CoreIpcServer.h"

#include "base/Log.h"

void ipcSendToClient(const std::string &command, const std::string &args)
{
  // Code shared with other programs (tests, the Qt GUI) runs without a core IPC server.
  if (!deskflow::core::ipc::CoreIpcServer::exists()) {
    LOG_VERBOSE("no core ipc server, not sending: %s", command.c_str());
    return;
  }
  deskflow::core::ipc::CoreIpcServer::instance().broadcastCommand(command, args);
}

void ipcSendConnectionState(deskflow::core::ConnectionState state)
{
  ipcSendToClient("connectionState", std::string(deskflow::enumToString(state)));
}
