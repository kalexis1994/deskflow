/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/ipc/LocalServer.h"
#include "arch/OsDetection.h"

#include "arch/Utf8.h"
#include "base/Log.h"

#include <array>
#include <atomic>
#include <cstdlib>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#if defined(DESKFLOW_OS_WIN)
#include <sddl.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace deskflow::core::ipc {

namespace {
constexpr std::size_t kBufferSize = 4096;
constexpr int kWriteTimeoutMs = 5000;
} // namespace

#if defined(DESKFLOW_OS_WIN)

namespace {

//! SYSTEM, administrators and the account running the server get full access. Everyone
//! else may read and write, but unlike QLocalServer's world access not change the
//! pipe's permissions or owner.
std::wstring pipeSecurityDescriptor()
{
  std::wstring user;
  HANDLE token = nullptr;
  if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<BYTE> buffer(size);
    if (size != 0 && GetTokenInformation(token, TokenUser, buffer.data(), size, &size)) {
      LPWSTR sid = nullptr;
      if (ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(buffer.data())->User.Sid, &sid)) {
        user = sid;
        LocalFree(sid);
      }
    }
    CloseHandle(token);
  }

  std::wstring sddl = L"D:(A;;GA;;;SY)(A;;GA;;;BA)";
  if (!user.empty())
    sddl += L"(A;;GA;;;" + user + L")";
  sddl += L"(A;;FRFW;;;WD)"; // FILE_GENERIC_READ | FILE_GENERIC_WRITE for Everyone
  return sddl;
}

} // namespace

struct LocalServer::Impl
{
  struct Client
  {
    ClientId id = 0;
    HANDLE pipe = INVALID_HANDLE_VALUE;
    HANDLE readEvent = nullptr;
    OVERLAPPED readOverlapped{};
    bool readPending = false;
    std::array<char, kBufferSize> buffer{};
    std::mutex writeMutex;
  };

  Callbacks callbacks;
  std::wstring pipeName;
  PSECURITY_DESCRIPTOR securityDescriptor = nullptr;
  SECURITY_ATTRIBUTES securityAttributes{};

  HANDLE stopEvent = nullptr;
  HANDLE wakeEvent = nullptr;
  HANDLE connectEvent = nullptr;
  HANDLE pendingPipe = INVALID_HANDLE_VALUE;
  OVERLAPPED connectOverlapped{};
  bool connectPending = false;   // ConnectNamedPipe is waiting in the background
  bool alreadyConnected = false; // a client got there before ConnectNamedPipe; nothing to wait for

  std::thread thread;
  std::mutex mutex; // guards clients and closeRequests
  std::map<ClientId, std::shared_ptr<Client>> clients;
  std::vector<ClientId> closeRequests;
  ClientId nextId = 1;

  ~Impl()
  {
    for (auto *handle : {stopEvent, wakeEvent, connectEvent}) {
      if (handle)
        CloseHandle(handle);
    }
    if (securityDescriptor)
      LocalFree(securityDescriptor);
  }

  HANDLE createInstance() const
  {
    return CreateNamedPipeW(
        pipeName.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, PIPE_UNLIMITED_INSTANCES,
        kBufferSize, kBufferSize, 0, const_cast<SECURITY_ATTRIBUTES *>(&securityAttributes)
    );
  }

  //! Waits for the next client on the pending instance
  void startConnect()
  {
    connectOverlapped = {};
    connectOverlapped.hEvent = connectEvent;
    ResetEvent(connectEvent);
    connectPending = false;
    alreadyConnected = false;
    if (ConnectNamedPipe(pendingPipe, &connectOverlapped)) {
      alreadyConnected = true;
      SetEvent(connectEvent);
      return;
    }
    switch (const auto error = GetLastError(); error) {
    case ERROR_IO_PENDING:
      connectPending = true;
      break;
    case ERROR_PIPE_CONNECTED: // a client arrived between creating the instance and now
      alreadyConnected = true;
      SetEvent(connectEvent);
      break;
    default:
      LOG_ERR("ipc server could not wait for clients: %s", windowsError(error).c_str());
    }
  }

  static std::string windowsError(DWORD error)
  {
    LPWSTR text = nullptr;
    FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, 0,
        reinterpret_cast<LPWSTR>(&text), 0, nullptr
    );
    std::string result = text ? deskflow::trimmed(deskflow::wideToUtf8(text)) : std::to_string(error);
    if (text)
      LocalFree(text);
    return result;
  }

  bool startRead(Client &client)
  {
    client.readOverlapped = {};
    client.readOverlapped.hEvent = client.readEvent;
    ResetEvent(client.readEvent);
    if (ReadFile(client.pipe, client.buffer.data(), kBufferSize, nullptr, &client.readOverlapped) ||
        GetLastError() == ERROR_IO_PENDING) {
      client.readPending = true; // a read finished right away also signals the event
      return true;
    }
    client.readPending = false;
    return false;
  }

  void closeClient(const std::shared_ptr<Client> &client, bool notify)
  {
    {
      std::scoped_lock lock{client->writeMutex};
      if (client->pipe == INVALID_HANDLE_VALUE)
        return;
      if (client->readPending) {
        CancelIoEx(client->pipe, &client->readOverlapped);
        DWORD ignored = 0;
        GetOverlappedResult(client->pipe, &client->readOverlapped, &ignored, TRUE);
        client->readPending = false;
      }
      // Closing without DisconnectNamedPipe lets the client still read what was written last.
      CloseHandle(client->pipe);
      client->pipe = INVALID_HANDLE_VALUE;
      CloseHandle(client->readEvent);
      client->readEvent = nullptr;
    }
    {
      std::scoped_lock lock{mutex};
      clients.erase(client->id);
    }
    if (notify && callbacks.disconnected)
      callbacks.disconnected(client->id);
  }

  void acceptPending()
  {
    DWORD ignored = 0;
    const bool connected = alreadyConnected || GetOverlappedResult(pendingPipe, &connectOverlapped, &ignored, FALSE);
    connectPending = false;
    if (!connected) {
      LOG_WARN("ipc server client failed to connect: %s", windowsError(GetLastError()).c_str());
      CloseHandle(pendingPipe);
    } else {
      auto client = std::make_shared<Client>();
      client->pipe = pendingPipe;
      client->readEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
      {
        std::scoped_lock lock{mutex};
        client->id = nextId++;
        clients.emplace(client->id, client);
      }
      if (callbacks.connected)
        callbacks.connected(client->id);
      if (!startRead(*client))
        closeClient(client, true);
    }

    pendingPipe = createInstance();
    if (pendingPipe == INVALID_HANDLE_VALUE) {
      LOG_ERR("ipc server could not create a pipe instance: %s", windowsError(GetLastError()).c_str());
      ResetEvent(connectEvent);
      return;
    }
    startConnect();
  }

  void readFinished(const std::shared_ptr<Client> &client)
  {
    DWORD bytes = 0;
    const bool ok = GetOverlappedResult(client->pipe, &client->readOverlapped, &bytes, FALSE);
    client->readPending = false;
    if (!ok) {
      if (const auto error = GetLastError(); error != ERROR_BROKEN_PIPE && error != ERROR_OPERATION_ABORTED)
        LOG_DEBUG("ipc server client read failed: %s", windowsError(error).c_str());
      closeClient(client, true);
      return;
    }
    if (bytes > 0 && callbacks.received)
      callbacks.received(client->id, std::string_view(client->buffer.data(), bytes));
    if (client->pipe != INVALID_HANDLE_VALUE && !startRead(*client))
      closeClient(client, true);
  }

  void run()
  {
    startConnect();

    std::vector<HANDLE> handles;
    std::vector<std::shared_ptr<Client>> order;
    while (true) {
      handles = {stopEvent, wakeEvent, connectEvent};
      order.clear();
      {
        std::scoped_lock lock{mutex};
        for (const auto &[id, client] : clients) {
          if (handles.size() == MAXIMUM_WAIT_OBJECTS)
            break;
          handles.push_back(client->readEvent);
          order.push_back(client);
        }
      }

      const auto result = WaitForMultipleObjects(static_cast<DWORD>(handles.size()), handles.data(), FALSE, INFINITE);
      if (result == WAIT_FAILED) {
        LOG_ERR("ipc server wait failed: %s", windowsError(GetLastError()).c_str());
        break;
      }
      const auto index = result - WAIT_OBJECT_0;
      if (index == 0)
        break;

      if (index == 1) {
        std::vector<ClientId> requests;
        {
          std::scoped_lock lock{mutex};
          requests.swap(closeRequests);
        }
        for (const auto id : requests) {
          std::shared_ptr<Client> client;
          {
            std::scoped_lock lock{mutex};
            if (const auto it = clients.find(id); it != clients.end())
              client = it->second;
          }
          if (client)
            closeClient(client, true);
        }
      } else if (index == 2) {
        if (pendingPipe != INVALID_HANDLE_VALUE)
          acceptPending();
        else
          ResetEvent(connectEvent);
      } else if (index < handles.size()) {
        readFinished(order[index - 3]);
      }
    }

    // shutting down
    if (pendingPipe != INVALID_HANDLE_VALUE) {
      if (connectPending) {
        CancelIoEx(pendingPipe, &connectOverlapped);
        DWORD ignored = 0;
        GetOverlappedResult(pendingPipe, &connectOverlapped, &ignored, TRUE);
      }
      CloseHandle(pendingPipe);
      pendingPipe = INVALID_HANDLE_VALUE;
    }
    std::map<ClientId, std::shared_ptr<Client>> remaining;
    {
      std::scoped_lock lock{mutex};
      remaining = clients;
    }
    for (const auto &[id, client] : remaining)
      closeClient(client, false);
  }
};

LocalServer::LocalServer(Callbacks callbacks) : m_impl{std::make_unique<Impl>()}
{
  m_impl->callbacks = std::move(callbacks);
}

LocalServer::~LocalServer()
{
  stop();
}

std::string LocalServer::endpoint(const std::string &name)
{
  return R"(\\.\pipe\)" + name;
}

bool LocalServer::listen(const std::string &name)
{
  auto &impl = *m_impl;
  impl.pipeName = deskflow::utf8ToWide(endpoint(name));

  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
          pipeSecurityDescriptor().c_str(), SDDL_REVISION_1, &impl.securityDescriptor, nullptr
      )) {
    LOG_ERR("ipc server could not build pipe permissions: %s", Impl::windowsError(GetLastError()).c_str());
    return false;
  }
  impl.securityAttributes.nLength = sizeof(SECURITY_ATTRIBUTES);
  impl.securityAttributes.lpSecurityDescriptor = impl.securityDescriptor;
  impl.securityAttributes.bInheritHandle = FALSE;

  impl.pendingPipe = impl.createInstance();
  if (impl.pendingPipe == INVALID_HANDLE_VALUE) {
    LOG_ERR(
        "ipc server could not create pipe %s: %s", endpoint(name).c_str(), Impl::windowsError(GetLastError()).c_str()
    );
    return false;
  }

  impl.stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  impl.wakeEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  impl.connectEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  impl.thread = std::thread([&impl] { impl.run(); });
  return true;
}

void LocalServer::stop()
{
  if (!m_impl->thread.joinable())
    return;
  SetEvent(m_impl->stopEvent);
  m_impl->thread.join();
}

bool LocalServer::write(ClientId id, std::string_view data)
{
  std::shared_ptr<Impl::Client> client;
  {
    std::scoped_lock lock{m_impl->mutex};
    if (const auto it = m_impl->clients.find(id); it != m_impl->clients.end())
      client = it->second;
  }
  if (!client)
    return false;

  std::scoped_lock lock{client->writeMutex};
  if (client->pipe == INVALID_HANDLE_VALUE)
    return false;

  OVERLAPPED overlapped{};
  overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  bool ok = true;
  while (ok && !data.empty()) {
    ResetEvent(overlapped.hEvent);
    const auto chunk = static_cast<DWORD>((std::min)(data.size(), std::size_t{0x10000000}));
    if (!WriteFile(client->pipe, data.data(), chunk, nullptr, &overlapped) && GetLastError() != ERROR_IO_PENDING) {
      ok = false;
      break;
    }
    DWORD written = 0;
    if (WaitForSingleObject(overlapped.hEvent, kWriteTimeoutMs) != WAIT_OBJECT_0) {
      // the client isn't reading; give up rather than block the caller forever
      CancelIoEx(client->pipe, &overlapped);
      GetOverlappedResult(client->pipe, &overlapped, &written, TRUE);
      ok = false;
      break;
    }
    ok = GetOverlappedResult(client->pipe, &overlapped, &written, FALSE) && written > 0;
    data.remove_prefix(written);
  }
  CloseHandle(overlapped.hEvent);
  return ok;
}

void LocalServer::disconnect(ClientId id)
{
  {
    std::scoped_lock lock{m_impl->mutex};
    m_impl->closeRequests.push_back(id);
  }
  if (m_impl->wakeEvent)
    SetEvent(m_impl->wakeEvent);
}

#else // Unix

struct LocalServer::Impl
{
  struct Client
  {
    ClientId id = 0;
    int fd = -1;
    std::mutex writeMutex;
  };

  Callbacks callbacks;
  std::string path;
  int listenFd = -1;
  std::array<int, 2> wakePipe{-1, -1};
  std::atomic<bool> stopping{false};

  std::thread thread;
  std::mutex mutex; // guards clients and closeRequests
  std::map<ClientId, std::shared_ptr<Client>> clients;
  std::vector<ClientId> closeRequests;
  ClientId nextId = 1;

  void wake() const
  {
    const char byte = 1;
    [[maybe_unused]] const auto ignored = ::write(wakePipe[1], &byte, 1);
  }

  void closeClient(const std::shared_ptr<Client> &client, bool notify)
  {
    {
      std::scoped_lock lock{client->writeMutex};
      if (client->fd < 0)
        return;
      ::close(client->fd); // data already sent stays readable for the client
      client->fd = -1;
    }
    {
      std::scoped_lock lock{mutex};
      clients.erase(client->id);
    }
    if (notify && callbacks.disconnected)
      callbacks.disconnected(client->id);
  }

  void accept()
  {
    const int fd = ::accept(listenFd, nullptr, nullptr);
    if (fd < 0) {
      if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
        LOG_WARN("ipc server could not accept a client: %s", std::strerror(errno));
      return;
    }
    fcntl(fd, F_SETFD, FD_CLOEXEC);
#if defined(SO_NOSIGPIPE)
    const int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif
    timeval timeout{kWriteTimeoutMs / 1000, 0};
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    auto client = std::make_shared<Client>();
    client->fd = fd;
    {
      std::scoped_lock lock{mutex};
      client->id = nextId++;
      clients.emplace(client->id, client);
    }
    if (callbacks.connected)
      callbacks.connected(client->id);
  }

  void readFrom(const std::shared_ptr<Client> &client)
  {
    std::array<char, kBufferSize> buffer{};
    const auto bytes = ::recv(client->fd, buffer.data(), buffer.size(), 0);
    if (bytes > 0) {
      if (callbacks.received)
        callbacks.received(client->id, std::string_view(buffer.data(), static_cast<std::size_t>(bytes)));
      return;
    }
    if (bytes < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
      return;
    closeClient(client, true);
  }

  void run()
  {
    std::vector<pollfd> fds;
    std::vector<std::shared_ptr<Client>> order;
    while (!stopping) {
      fds = {{wakePipe[0], POLLIN, 0}, {listenFd, POLLIN, 0}};
      order.clear();
      {
        std::scoped_lock lock{mutex};
        for (const auto &[id, client] : clients) {
          fds.push_back({client->fd, POLLIN, 0});
          order.push_back(client);
        }
      }

      if (::poll(fds.data(), fds.size(), -1) < 0) {
        if (errno == EINTR)
          continue;
        LOG_ERR("ipc server poll failed: %s", std::strerror(errno));
        break;
      }

      if (fds[0].revents) {
        std::array<char, 64> drain{};
        [[maybe_unused]] const auto ignored = ::read(wakePipe[0], drain.data(), drain.size());
        if (stopping)
          break;
        std::vector<ClientId> requests;
        {
          std::scoped_lock lock{mutex};
          requests.swap(closeRequests);
        }
        for (const auto id : requests) {
          std::shared_ptr<Client> client;
          {
            std::scoped_lock lock{mutex};
            if (const auto it = clients.find(id); it != clients.end())
              client = it->second;
          }
          if (client)
            closeClient(client, true);
        }
      }
      if (fds[1].revents & POLLIN)
        accept();
      for (std::size_t i = 0; i < order.size(); ++i) {
        if (fds[i + 2].revents && order[i]->fd >= 0)
          readFrom(order[i]);
      }
    }

    std::map<ClientId, std::shared_ptr<Client>> remaining;
    {
      std::scoped_lock lock{mutex};
      remaining = clients;
    }
    for (const auto &[id, client] : remaining)
      closeClient(client, false);
  }
};

LocalServer::LocalServer(Callbacks callbacks) : m_impl{std::make_unique<Impl>()}
{
  m_impl->callbacks = std::move(callbacks);
}

LocalServer::~LocalServer()
{
  stop();
}

std::string LocalServer::endpoint(const std::string &name)
{
  // QDir::tempPath(), where QLocalServer put its sockets
  std::string temp;
  if (const char *tmpdir = std::getenv("TMPDIR"); tmpdir && *tmpdir)
    temp = tmpdir;
  while (temp.size() > 1 && temp.back() == '/')
    temp.pop_back();
  if (temp.empty())
    temp = "/tmp";
  return temp + "/" + name;
}

bool LocalServer::listen(const std::string &name)
{
  auto &impl = *m_impl;
  impl.path = endpoint(name);

  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  if (impl.path.size() >= sizeof(address.sun_path)) {
    LOG_ERR("ipc server socket path is too long: %s", impl.path.c_str());
    return false;
  }
  std::memcpy(address.sun_path, impl.path.c_str(), impl.path.size() + 1);

  ::unlink(impl.path.c_str()); // a socket left behind by a previous run, as QLocalServer::removeServer
  impl.listenFd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (impl.listenFd < 0) {
    LOG_ERR("ipc server could not create a socket: %s", std::strerror(errno));
    return false;
  }
  fcntl(impl.listenFd, F_SETFD, FD_CLOEXEC);

  if (::bind(impl.listenFd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0 ||
      ::listen(impl.listenFd, SOMAXCONN) != 0) {
    LOG_ERR("ipc server could not listen on %s: %s", impl.path.c_str(), std::strerror(errno));
    ::close(impl.listenFd);
    impl.listenFd = -1;
    return false;
  }
  // any local user may connect: the daemon runs as root, the GUI as the user
  ::chmod(impl.path.c_str(), 0777);

  if (::pipe(impl.wakePipe.data()) != 0) {
    LOG_ERR("ipc server could not create its wake pipe: %s", std::strerror(errno));
    ::close(impl.listenFd);
    impl.listenFd = -1;
    return false;
  }
  for (const int fd : impl.wakePipe)
    fcntl(fd, F_SETFD, FD_CLOEXEC);

  impl.thread = std::thread([&impl] { impl.run(); });
  return true;
}

void LocalServer::stop()
{
  auto &impl = *m_impl;
  if (!impl.thread.joinable())
    return;
  impl.stopping = true;
  impl.wake();
  impl.thread.join();

  ::close(impl.listenFd);
  impl.listenFd = -1;
  ::unlink(impl.path.c_str());
  for (int &fd : impl.wakePipe) {
    ::close(fd);
    fd = -1;
  }
}

bool LocalServer::write(ClientId id, std::string_view data)
{
  std::shared_ptr<Impl::Client> client;
  {
    std::scoped_lock lock{m_impl->mutex};
    if (const auto it = m_impl->clients.find(id); it != m_impl->clients.end())
      client = it->second;
  }
  if (!client)
    return false;

  std::scoped_lock lock{client->writeMutex};
  while (client->fd >= 0 && !data.empty()) {
#if defined(MSG_NOSIGNAL)
    const auto sent = ::send(client->fd, data.data(), data.size(), MSG_NOSIGNAL);
#else
    const auto sent = ::send(client->fd, data.data(), data.size(), 0);
#endif
    if (sent < 0) {
      if (errno == EINTR)
        continue;
      return false; // gone, or not reading for kWriteTimeoutMs
    }
    data.remove_prefix(static_cast<std::size_t>(sent));
  }
  return data.empty();
}

void LocalServer::disconnect(ClientId id)
{
  {
    std::scoped_lock lock{m_impl->mutex};
    m_impl->closeRequests.push_back(id);
  }
  if (m_impl->wakePipe[1] >= 0)
    m_impl->wake();
}

#endif

} // namespace deskflow::core::ipc
