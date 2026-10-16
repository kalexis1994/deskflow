/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025-2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "DaemonIpcServer.h"

#include "base/Log.h"
#include "common/Constants.h"

namespace deskflow::core::ipc {

const auto kAckMessage = "ok";
const auto kErrorMessage = "error";

DaemonIpcServer::DaemonIpcServer(const std::string &logFilename, const std::string &serverName)
    : IpcServer(serverName.empty() ? std::string(kDaemonIpcName) : serverName, "daemon"),
      m_logFilename(logFilename)
{
  // do nothing
}

DaemonIpcServer::~DaemonIpcServer()
{
  // no handler may run once this object is going away
  stop();
}

void DaemonIpcServer::processCommand(ClientId client, const std::string &command, const std::vector<std::string> &parts)
{
  if (command == "logLevel") {
    processLogLevel(client, parts);
  } else if (command == "configFile") {
    processConfigFile(client, parts);
  } else if (command == "start") {
    LOG_DEBUG("daemon ipc server got start message");
    notify(onStartProcessRequested);
    writeToClient(client, kAckMessage);
  } else if (command == "stop") {
    LOG_DEBUG("daemon ipc server got stop message");
    notify(onStopProcessRequested);
    writeToClient(client, kAckMessage);
  } else if (command == "logPath") {
    LOG_DEBUG("daemon ipc server got log path request");
    writeToClient(client, "logPath=" + m_logFilename);
  } else if (command == "clearSettings") {
    LOG_DEBUG("daemon ipc server got clear settings message");
    notify(onClearSettingsRequested);
    writeToClient(client, kAckMessage);
  } else {
    LOG_WARN("daemon ipc server got unknown command: %s", command.c_str());
  }
}

void DaemonIpcServer::processLogLevel(ClientId client, const std::vector<std::string> &messageParts)
{
  if (messageParts.size() < 2) {
    LOG_ERR("daemon ipc server got invalid log level message");
    writeToClient(client, kErrorMessage);
    return;
  }

  const auto &logLevel = messageParts.at(1);
  if (logLevel.empty()) {
    LOG_ERR("daemon ipc server got empty log level");
    writeToClient(client, kErrorMessage);
    return;
  }

  LOG_DEBUG("daemon ipc server got new log level: %s", logLevel.c_str());
  notify(onLogLevelChanged, logLevel);
  writeToClient(client, kAckMessage);
}

void DaemonIpcServer::processConfigFile(ClientId client, const std::vector<std::string> &messageParts)
{
  if (messageParts.size() < 2) {
    LOG_ERR("daemon ipc server got invalid config file message");
    writeToClient(client, kErrorMessage);
    return;
  }

  const auto &configFile = messageParts.at(1);
  if (configFile.empty()) {
    LOG_ERR("daemon ipc server got empty config file path");
    writeToClient(client, kErrorMessage);
    return;
  }

  LOG_DEBUG("daemon ipc server got config file: %s", configFile.c_str());
  notify(onConfigFileChanged, configFile);
  writeToClient(client, kAckMessage);
}

} // namespace deskflow::core::ipc
