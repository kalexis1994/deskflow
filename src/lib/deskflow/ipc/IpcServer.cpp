/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025-2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "IpcServer.h"

#include "base/Log.h"
#include "common/VersionInfo.h"

namespace deskflow::core::ipc {

namespace {
// a client that never sends a newline shouldn't grow the buffer forever
constexpr std::size_t kMaxLineLength = 64 * 1024;
} // namespace

IpcServer::IpcServer(const std::string &serverName, const std::string &typeName)
    : m_serverName(serverName),
      m_typeName(typeName),
      m_server({
          [this](ClientId client) { handleConnected(client); },
          [this](ClientId client, std::string_view data) { handleReceived(client, data); },
          [this](ClientId client) { handleDisconnected(client); },
      })
{
  // do nothing
}

IpcServer::~IpcServer()
{
  stop();
}

void IpcServer::listen()
{
  if (m_server.listen(m_serverName)) {
    LOG_DEBUG("%s ipc server listening on: %s", m_typeName.c_str(), LocalServer::endpoint(m_serverName).c_str());
  } else {
    LOG_ERR("%s ipc server failed to listen on: %s", m_typeName.c_str(), m_serverName.c_str());
  }
}

void IpcServer::stop()
{
  m_server.stop();
}

void IpcServer::handleConnected(ClientId client)
{
  LOG_DEBUG("%s ipc server got new connection", m_typeName.c_str());
  std::scoped_lock lock{m_mutex};
  m_clients.insert(client);
}

void IpcServer::handleReceived(ClientId client, std::string_view data)
{
  LOG_VERBOSE("%s ipc server ready to read data", m_typeName.c_str());

  // each message is delimited by a newline to keep the protocol super simple;
  // a message split across reads waits for the rest
  auto &buffer = m_partialLines[client];
  buffer.append(data);

  std::size_t start = 0;
  for (auto newline = buffer.find('\n'); newline != std::string::npos; newline = buffer.find('\n', start)) {
    auto message = buffer.substr(start, newline - start);
    if (message.ends_with('\r'))
      message.pop_back();
    start = newline + 1;
    processMessage(client, message);
  }
  buffer.erase(0, start);

  if (buffer.size() > kMaxLineLength) {
    LOG_WARN("%s ipc server dropped an over-long message without a newline", m_typeName.c_str());
    buffer.clear();
  }
}

void IpcServer::handleDisconnected(ClientId client)
{
  LOG_DEBUG("%s ipc server client disconnected", m_typeName.c_str());
  m_partialLines.erase(client);
  std::scoped_lock lock{m_mutex};
  m_clients.erase(client);
}

void IpcServer::processMessage(ClientId client, const std::string &message)
{
  LOG_VERBOSE("%s ipc server got message: %s", m_typeName.c_str(), message.c_str());

  // the argument is everything after the first '=', so it may contain '=' itself
  std::vector<std::string> parts;
  if (const auto equals = message.find('='); equals == std::string::npos) {
    parts = {message};
  } else {
    parts = {message.substr(0, equals), message.substr(equals + 1)};
  }

  if (const auto &command = parts.at(0); command == "hello") {
    if (parts.size() < 2) {
      LOG_ERR("%s ipc client hello missing version", m_typeName.c_str());
      writeToClient(client, "error=missing version");
      m_server.disconnect(client);
      return;
    }

    const auto versionId = std::string(kVersion) + "+" + kVersionGitSha;
    const auto &clientVersion = parts.at(1);
    LOG_DEBUG("%s ipc server got hello message (version: %s)", m_typeName.c_str(), versionId.c_str());

    if (clientVersion != versionId) {
      LOG_WARN(
          "%s ipc client version mismatch (client: %s, server: %s)", m_typeName.c_str(), clientVersion.c_str(),
          versionId.c_str()
      );
      writeToClient(client, "versionMismatch=" + versionId);
      return;
    }

    LOG_DEBUG("%s ipc server sending hello back", m_typeName.c_str());
    writeToClient(client, "hello=" + versionId);

    // Replay messages that were queued before any clients connected.
    std::scoped_lock lock{m_mutex};
    LOG_VERBOSE("ipc server replaying %zu pending messages", m_pendingMessages.size());
    for (const auto &pending : m_pendingMessages) {
      LOG_VERBOSE("%s ipc server replaying: %s", m_typeName.c_str(), pending.c_str());
      writeToClient(client, pending);
    }
    m_pendingMessages.clear();
  } else if (command == "noop") {
    LOG_DEBUG("%s ipc server got noop message", m_typeName.c_str());
    writeToClient(client, "ok");
  } else {
    processCommand(client, command, parts);
  }
}

void IpcServer::broadcastCommand(const std::string &command, const std::string &args, bool latestOnly)
{
  const auto message = args.empty() ? command : command + "=" + args;

  std::scoped_lock lock{m_mutex};
  if (m_clients.empty()) {
    LOG_VERBOSE("%s ipc server has no clients, message queued: %s", m_typeName.c_str(), message.c_str());
    if (latestOnly) {
      std::erase_if(m_pendingMessages, [&command](const std::string &pending) {
        return pending == command || pending.starts_with(command + "=");
      });
    }
    m_pendingMessages.push_back(message);
    return;
  }

  LOG_VERBOSE(
      "%s ipc server broadcasting message to %zu clients: %s", m_typeName.c_str(), m_clients.size(), message.c_str()
  );
  for (const auto client : m_clients) {
    writeToClient(client, message);
  }
}

void IpcServer::writeToClient(ClientId client, const std::string &message)
{
  if (!m_server.write(client, message + '\n')) {
    LOG_ERR("%s ipc server failed to write full message to client socket", m_typeName.c_str());
  } else {
    LOG_VERBOSE("%s ipc server wrote message to client socket: %s", m_typeName.c_str(), message.c_str());
  }
}

} // namespace deskflow::core::ipc
