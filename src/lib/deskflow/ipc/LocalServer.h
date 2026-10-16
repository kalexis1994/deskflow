/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace deskflow::core::ipc {

//! A local stream server that clients reach by name, on its own thread
/*!
Windows: the named pipe `\\.\pipe\<name>`. Elsewhere: the Unix socket
`$TMPDIR/<name>` (or `/tmp/<name>`). These are the endpoints QLocalServer used, so
the Qt GUI, the Tauri app and older clients keep finding the server. Any local
user may connect, as with QLocalServer::WorldAccessOption: the daemon runs as
SYSTEM/root and the GUI as the logged-in user.

The callbacks run on the server's thread. write() and disconnect() may be called
from any thread.
*/
class LocalServer
{
public:
  using ClientId = std::uint64_t;

  struct Callbacks
  {
    std::function<void(ClientId)> connected;
    std::function<void(ClientId, std::string_view)> received;
    std::function<void(ClientId)> disconnected;
  };

  explicit LocalServer(Callbacks callbacks);
  ~LocalServer();

  LocalServer(const LocalServer &) = delete;
  LocalServer &operator=(const LocalServer &) = delete;

  //! Starts listening and serving; false if the endpoint couldn't be created
  bool listen(const std::string &name);

  //! Disconnects every client and stops the thread; safe to call more than once
  void stop();

  //! Writes all of the data before returning; false if the client is gone or stuck
  bool write(ClientId client, std::string_view data);

  //! Closes a client's connection once pending writes are done
  void disconnect(ClientId client);

  //! The path clients open for this name
  static std::string endpoint(const std::string &name);

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

} // namespace deskflow::core::ipc
