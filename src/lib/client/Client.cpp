/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 - 2026 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2012 - 2016, 2026 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2002 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "client/Client.h"
#include <cassert>

#include "arch/Arch.h"
#include "base/IEventQueue.h"
#include "base/Log.h"
#include "client/ServerProxy.h"
#include "client/ServerProxy1_7.h"
#include "client/ServerProxy1_8.h"
#include "common/CoreSettings.h"
#include "common/NetworkProtocol.h"
#include "deskflow/Clipboard.h"
#include "deskflow/Computer.h"
#include "deskflow/DeskflowException.h"
#include "deskflow/IPlatformComputer.h"
#include "deskflow/PacketStreamFilter.h"
#include "deskflow/ProtocolTypes.h"
#include "deskflow/ProtocolUtil.h"
#include "deskflow/StreamChunker.h"
#include "deskflow/files/FileChannel.h"
#include "deskflow/files/FileReport.h"
#include "deskflow/ipc/CoreIpc.h"
#include "net/IDataSocket.h"
#include "net/ISocketFactory.h"
#include "net/SecureSocket.h"
#include "net/TCPSocket.h"

#include <cstdlib>
#include <cstring>

//
// Client
//

Client::DisconnectRequest::DisconnectRequest(Kind kind, const char *message)
    : m_kind(kind),
      m_message(message != nullptr ? message : "")
{
}

Client::DisconnectRequest::DisconnectRequest(deskflow::core::ConnectionRefusal reason, const char *message)
    : m_kind(Kind::Refuse),
      m_refusalReason(reason),
      m_message(message != nullptr ? message : "")
{
}

Client::Client(
    IEventQueue *events, const std::string &name, const NetworkAddress &address, ISocketFactory *socketFactory,
    deskflow::Computer *computer
)
    : m_name(name),
      m_serverAddress(address),
      m_socketFactory(socketFactory),
      m_computer(computer),
      m_events(events),
      m_useSecureNetwork(CoreSettings::value(CoreSettings::Security::TlsEnabled).toBool())
{
  assert(m_socketFactory != nullptr);
  assert(m_computer != nullptr);

  // register suspend/resume event handlers
  m_events->addHandler(EventTypes::ComputerSuspend, getEventTarget(), [this](const auto &) { handleSuspend(); });
  m_events->addHandler(EventTypes::ComputerResume, getEventTarget(), [this](const auto &) { handleResume(); });
}

Client::~Client()
{
  m_events->removeHandler(EventTypes::ComputerSuspend, getEventTarget());
  m_events->removeHandler(EventTypes::ComputerResume, getEventTarget());

  cleanupTimer();
  cleanupClipboardRetry();
  cleanupComputer();
  cleanupConnecting();
  cleanupConnection();
  delete m_socketFactory;
}

void Client::setServerAddress(const NetworkAddress &address)
{
  m_serverAddress = address;
}

void Client::connect(size_t addressIndex)
{
  if (m_stream != nullptr) {
    return;
  }
  if (m_suspended) {
    m_connectOnResume = true;
    return;
  }

  auto securityLevel = m_useSecureNetwork ? SecurityLevel::PeerAuth : SecurityLevel::PlainText;

  try {
    // resolve the server hostname.  do this every time we connect
    // in case we couldn't resolve the address earlier or the address
    // has changed (which can happen frequently if this is a laptop
    // being shuttled between various networks).  patch by Brent
    // Priddy.
    m_resolvedAddressesCount = m_serverAddress.resolve(addressIndex);

    // m_serverAddress will be null if the hostname address is not reolved
    if (m_serverAddress.getAddress() != nullptr) {
      // to help users troubleshoot, show server host name (issue: 60)
      LOG_DEBUG(
          "connecting to '%s': %s:%i", m_serverAddress.getHostname().c_str(),
          ARCH->addrToString(m_serverAddress.getAddress()).c_str(), m_serverAddress.getPort()
      );
      ipcSendConnectionState(deskflow::core::ConnectionState::Connecting);
    }

    // create the socket
    IDataSocket *socket = m_socketFactory->create(ARCH->getAddrFamily(m_serverAddress.getAddress()), securityLevel);
    bindNetworkInterface(socket);

    // filter socket messages, including a packetizing filter
    m_stream = new PacketStreamFilter(m_events, socket, true);

    // connect
    LOG_VERBOSE("connecting to server");
    setupConnecting();
    setupTimer();
    socket->connect(m_serverAddress);
  } catch (BaseException &e) {
    cleanupTimer();
    cleanupConnecting();
    cleanupStream();
    LOG_VERBOSE("connection failed");
    sendConnectionFailedEvent(e.what());
    return;
  }
}

void Client::disconnect(const char *msg)
{
  cleanup();

  if (msg) {
    sendConnectionFailedEvent(msg);
  } else {
    sendEvent(EventTypes::ClientDisconnected);
  }
}

void Client::refuseConnection(deskflow::core::ConnectionRefusal reason, const char *msg)
{
  ipcSendToClient("connectionRefused", std::string(deskflow::enumToString(reason)));

  cleanup();

  if (msg) {
    auto info = new FailInfo(msg);
    info->m_retry = true;
    Event event(EventTypes::ClientConnectionRefused, getEventTarget(), info, Event::EventFlags::DontFreeData);
    m_events->addEvent(std::move(event));
  }
}

void Client::handshakeComplete()
{
  m_ready = true;
  m_computer->enable();
  if (m_relativeMouseMoves && !m_hasRelativeRestorePosition) {
    saveRelativeRestorePosition();
  }
  sendEvent(EventTypes::ClientConnected);
}

bool Client::isConnected() const
{
  return (m_server != nullptr);
}

bool Client::isConnecting() const
{
  return (m_timer != nullptr);
}

NetworkAddress Client::getServerAddress() const
{
  return m_serverAddress;
}

size_t Client::getMaximumClipboardReceiveSizeBytes() const
{
  return m_maximumClipboardSize * 1024;
}

void *Client::getEventTarget() const
{
  return m_computer->getEventTarget();
}

bool Client::getClipboard(ClipboardID id, IClipboard *clipboard) const
{
  return m_computer->getClipboard(id, clipboard);
}

void Client::getShape(int32_t &x, int32_t &y, int32_t &w, int32_t &h) const
{
  m_computer->getShape(x, y, w, h);
}

void Client::getCursorPos(int32_t &x, int32_t &y) const
{
  m_computer->getCursorPos(x, y);
}

void Client::enter(int32_t xAbs, int32_t yAbs, uint32_t, KeyModifierMask mask, bool)
{
  m_active = true;
  if (m_relativeMouseMoves && m_hasRelativeRestorePosition) {
    xAbs = m_relativeRestoreX;
    yAbs = m_relativeRestoreY;
    LOG_VERBOSE("using relative restore position: %d,%d", xAbs, yAbs);
  }
  m_computer->mouseMove(xAbs, yAbs);
  m_computer->enter(mask);
}

bool Client::leave()
{
  if (m_relativeMouseMoves) {
    saveRelativeRestorePosition();
  }
  m_active = false;

  m_computer->leave();

  if (m_enableClipboard) {
    // send clipboards that we own and that have changed
    for (ClipboardID id = 0; id < kClipboardEnd; ++id) {
      if (m_ownClipboard[id]) {
        sendClipboard(id);
      }
    }
    sendFiles();
  }

  return true;
}

void Client::setClipboard(ClipboardID id, const IClipboard *clipboard)
{
  const auto data = IClipboard::marshall(clipboard);

  // An empty clipboard from the server means it has nothing yet (it just started, or
  // another computer's data hasn't arrived); writing it would erase what is here.
  if (!deskflow::clipboard::hasContent(data)) {
    LOG_DEBUG("ignoring empty clipboard %d from server", id);
    return;
  }

  m_ownClipboard[id] = false;
  m_sentClipboard[id] = false;
  m_pendingSend[id] = false;

  if (writeClipboard(id, data)) {
    m_pendingWrite[id].reset();
    LOG_INFO("clipboard %d updated from the server", id);
    reportClipboard(id, {.event = "received", .marshalled = data});
  } else {
    LOG_WARN("could not write clipboard %d (another program has it open), retrying", id);
    m_pendingWrite[id] = data;
    scheduleClipboardRetry();
  }
}

bool Client::writeClipboard(ClipboardID id, const std::string &marshalled)
{
  Clipboard clipboard;
  clipboard.unmarshall(marshalled, 0);
  if (!m_computer->setClipboard(id, &clipboard))
    return false;
  m_echoGuard[id].applied(marshalled);
  return true;
}

void Client::grabClipboard(ClipboardID id)
{
  // Another computer copied. Its data follows; until then this computer keeps what it
  // has rather than being emptied, so a failed or skipped transfer loses nothing.
  m_ownClipboard[id] = false;
  m_sentClipboard[id] = false;
  m_pendingSend[id] = false;

  // its files, if any, come from the server; ours are no longer wanted
  if (id == kClipboardClipboard && m_files) {
    m_files->cancelSend("a newer copy replaces it");
    m_fileProgress->finish();
    m_filesSent = true;
  }
}

void Client::setClipboardDirty(ClipboardID, bool)
{
  assert(0 && "shouldn't be called");
}

void Client::keyDown(KeyID id, KeyModifierMask mask, KeyButton button, const std::string &lang)
{
  m_computer->keyDown(id, mask, button, lang);
}

void Client::keyRepeat(KeyID id, KeyModifierMask mask, int32_t count, KeyButton button, const std::string &lang)
{
  m_computer->keyRepeat(id, mask, count, button, lang);
}

void Client::keyUp(KeyID id, KeyModifierMask mask, KeyButton button)
{
  m_computer->keyUp(id, mask, button);
}

void Client::mouseDown(ButtonID id)
{
  m_computer->mouseDown(id);
}

void Client::mouseUp(ButtonID id)
{
  m_computer->mouseUp(id);
}

void Client::mouseMove(int32_t x, int32_t y)
{
  m_computer->mouseMove(x, y);
}

void Client::mouseRelativeMove(int32_t dx, int32_t dy)
{
  m_computer->mouseRelativeMove(dx, dy);
}

void Client::mouseWheel(int32_t xDelta, int32_t yDelta)
{
  m_computer->mouseWheel(xDelta, yDelta);
}

void Client::screensaver(bool activate)
{
  m_computer->screensaver(activate);
}

void Client::resetOptions()
{
  m_relativeMouseMoves = false;
  m_hasRelativeRestorePosition = false;
  m_computer->resetOptions();
}

void Client::setOptions(const OptionsList &options)
{
  if (options.size() % 2 != 0) {
    LOG_ERR("options are the incorrect size, can not process them");
    return;
  }

  for (auto index = options.begin(); index != options.end(); ++index) {
    const OptionID id = *index;
    if (id == kOptionFileClipboard) {
      index++;
      if (index != options.end() && *index > 0) {
        enableFiles(static_cast<uint64_t>(*index) * 1024 * 1024);
      }
    } else if (id == kOptionClipboardSharing) {
      index++;
      if (index != options.end()) {
        if (!*index) {
          LOG_INFO("clipboard sharing disabled by server");
        }
        m_enableClipboard = *index;
      }
    } else if (id == kOptionClipboardSharingSize) {
      index++;
      if (index != options.end()) {
        m_maximumClipboardSize = *index;
      }
    } else if (id == kOptionRelativeMouseMoves) {
      index++;
      if (index != options.end()) {
        m_relativeMouseMoves = (*index != 0);
        if (m_relativeMouseMoves && m_ready && !m_hasRelativeRestorePosition) {
          saveRelativeRestorePosition();
        }
      }
    }
  }

  if (m_enableClipboard && !m_maximumClipboardSize) {
    m_enableClipboard = false;
    LOG_INFO("clipboard sharing is disabled because the server set the maximum clipboard size to 0");
  }

  m_computer->setOptions(options);
}

void Client::saveRelativeRestorePosition()
{
  m_computer->getCursorPos(m_relativeRestoreX, m_relativeRestoreY);
  m_hasRelativeRestorePosition = true;
  LOG_VERBOSE("saved relative restore position: %d,%d", m_relativeRestoreX, m_relativeRestoreY);
}

std::string Client::getName() const
{
  return m_name;
}

void Client::sendClipboard(ClipboardID id)
{
  // note -- m_mutex must be locked on entry
  assert(m_computer != nullptr);
  assert(m_server != nullptr);

  // get clipboard data.  set the clipboard time to the last
  // clipboard time before getting the data from the computer
  // as the computer may detect an unchanged clipboard and
  // avoid copying the data.
  Clipboard clipboard;
  if (clipboard.open(m_timeClipboard[id])) {
    clipboard.close();
  }
  if (!m_computer->getClipboard(id, &clipboard)) {
    // never send what a failed read left behind: it would replace the copy everywhere
    LOG_WARN("could not read clipboard %d (another program has it open), retrying", id);
    m_pendingSend[id] = true;
    scheduleClipboardRetry();
    return;
  }
  m_pendingSend[id] = false;

  // check time
  if (m_timeClipboard[id] == 0 || clipboard.getTime() != m_timeClipboard[id]) {
    const auto data = clipboard.marshall();
    if (!deskflow::clipboard::hasContent(data)) {
      // files travel on their own (sendFiles)
      if (id == kClipboardClipboard && m_files && m_computer->getClipboardFiles()) {
        LOG_DEBUG("clipboard %d holds files only", id);
        return;
      }
      LOG_INFO("not sharing clipboard %d: nothing in it Deskflow can share (text, HTML or image)", id);
      reportClipboard(id, {.event = "skipped", .reason = "nothingToShare"});
      return;
    }

    const auto fitted = deskflow::clipboard::fitToLimit(data, m_maximumClipboardSize * 1024);
    if (!fitted.fits) {
      LOG_WARN("not sending clipboard %d, even its text exceeds the limit of %zu KB", id, m_maximumClipboardSize);
      reportClipboard(id, {.event = "skipped", .reason = "tooLarge", .dropped = fitted.dropped, .marshalled = data});
      return;
    }
    if (!fitted.dropped.empty()) {
      LOG_INFO(
          "clipboard %d exceeds the limit of %zu KB, sending it without: %s", id, m_maximumClipboardSize,
          deskflow::clipboard::formatNames(fitted.dropped).c_str()
      );
    }

    // save new time
    m_timeClipboard[id] = clipboard.getTime();
    // save and send data if different or not yet sent
    if (!m_sentClipboard[id] || fitted.data != m_dataClipboard[id]) {
      m_sentClipboard[id] = true;
      m_dataClipboard[id] = fitted.data;
      Clipboard toSend;
      toSend.unmarshall(fitted.data, clipboard.getTime());
      m_server->onClipboardChanged(id, &toSend);
      reportClipboard(id, {.event = "sent", .dropped = fitted.dropped, .marshalled = fitted.data});
    }
  }
}

void Client::enableFiles(uint64_t serverMaxBytes)
{
  using namespace deskflow::files;

  if (m_files) {
    m_files->setPeerMaxBytes(serverMaxBytes);
    return;
  }
  const auto maxBytes = configuredMaxBytes();
  if (maxBytes == 0) {
    LOG_INFO("not copying files with the server: turned off here");
    return;
  }
  if (!m_computer->canShareFiles()) {
    LOG_INFO("not copying files with the server: not supported on this computer yet");
    return;
  }

  FileChannel::Wire wire;
  wire.list = [this](const auto &id, const auto &list) { m_server->sendFileList(id, list); };
  wire.chunk = [this](const auto &id, auto index, const auto &data) { m_server->sendFileChunk(id, index, data); };
  wire.done = [this](const auto &id, auto index, const auto &digest) { m_server->sendFileDone(id, index, digest); };
  wire.ack = [this](const auto &id) { m_server->sendFileAck(id); };
  wire.cancel = [this](const auto &id, const auto &reason) { m_server->sendFileCancel(id, reason); };

  FileChannel::Events events;
  events.accept = [this](const auto &, const FileList &list) -> std::optional<std::string> {
    if (!m_enableClipboard)
      return "clipboard sharing is off";
    // copied here while the server started sending an older copy
    if (m_ownClipboard[kClipboardClipboard])
      return "a newer copy was made here";
    LOG_INFO(
        "receiving files from the server: %zu files, %llu bytes", list.fileCount(),
        static_cast<unsigned long long>(list.totalBytes())
    );
    m_filesIn = std::make_unique<FileList>(list);
    return std::nullopt;
  };
  events.received = [this](const auto &, const std::vector<std::filesystem::path> &roots) {
    m_fileProgress->finish();
    if (m_ownClipboard[kClipboardClipboard]) {
      LOG_INFO("not putting the files from the server on the clipboard: a newer copy was made here");
    } else if (m_computer->setClipboardFiles(roots)) {
      LOG_INFO("files from the server are on the clipboard");
      reportFiles({.event = "received", .list = m_filesIn.get()});
    } else {
      LOG_WARN("could not put the files from the server on the clipboard");
      reportFiles({.event = "failed", .list = m_filesIn.get(), .detail = "the clipboard is busy"});
    }
  };
  events.receiveFailed = [this](const auto &, const std::string &reason) {
    LOG_WARN("files from the server didn't arrive: %s", reason.c_str());
    m_fileProgress->finish();
    reportFiles({.event = "failed", .list = m_filesIn.get(), .detail = reason});
  };
  events.sent = [this](const auto &) {
    LOG_INFO("files sent to the server");
    m_fileProgress->finish();
    reportFiles({.event = "sent", .list = m_filesOut.get()});
  };
  events.sendFailed = [this](const auto &, const std::string &reason) {
    LOG_WARN("files not sent to the server: %s", reason.c_str());
    m_fileProgress->finish();
    reportFiles({.event = "failed", .list = m_filesOut.get(), .detail = reason});
  };
  events.progress = [this](const auto &, bool sending, const FileList &list, uint64_t done) {
    m_fileProgress->update(sending ? "send" : "receive", "", list, done);
  };

  FileChannel::Options options;
  options.cacheRoot = userCacheRoot;
  options.limits.maxBytes = maxBytes;
  options.peerMaxBytes = serverMaxBytes;
  m_fileProgress = std::make_unique<FileProgress>();
  m_files = std::make_unique<FileChannel>(std::move(wire), std::move(events), std::move(options));
  m_server->sendFileClipboard(static_cast<uint32_t>(maxBytes / (1024 * 1024)));
  LOG_INFO("copying files with the server, up to %llu MB", static_cast<unsigned long long>(maxBytes / (1024 * 1024)));
}

void Client::sendFiles()
{
  using namespace deskflow::files;

  if (!m_files || !m_ownClipboard[kClipboardClipboard] || m_filesSent)
    return;
  m_filesSent = true;

  const auto files = m_computer->getClipboardFiles();
  if (!files)
    return; // a copy of something else

  Limits limits;
  limits.maxBytes = m_files->peerMaxBytes();
  std::variant<Selection, std::string> selection;
  {
    AsSessionUser asUser;
    selection = collect(*files, limits);
  }
  if (const auto *error = std::get_if<std::string>(&selection)) {
    LOG_WARN("not sending the copied files: %s", error->c_str());
    reportFiles({.event = reasonCode(*error) == "tooLarge" ? "skipped" : "failed", .detail = *error});
    return;
  }
  auto &chosen = std::get<Selection>(selection);
  m_filesOut = std::make_unique<FileList>(chosen.list);
  LOG_INFO(
      "sending files to the server: %zu files, %llu bytes", chosen.list.fileCount(),
      static_cast<unsigned long long>(chosen.list.totalBytes())
  );
  m_files->send(Cache::newId(), std::move(chosen));
}

void Client::filesSuperseded(const char *reason)
{
  m_filesSent = false;
  if (m_files) {
    m_files->cancelSend(reason);
    m_files->cancelReceive(reason);
    m_fileProgress->finish();
  }
}

void Client::scheduleClipboardRetry()
{
  static constexpr int kMaxRetries = 12;
  static constexpr double kRetryDelay = 0.25; // seconds: 3 s in all

  if (m_clipboardRetryTimer != nullptr)
    return;
  if (++m_clipboardRetries > kMaxRetries) {
    LOG_ERR("giving up on the clipboard: another program keeps it open");
    reportClipboard(kClipboardClipboard, {.event = "failed", .reason = "busy"});
    m_clipboardRetries = 0;
    for (ClipboardID id = 0; id < kClipboardEnd; ++id) {
      m_pendingWrite[id].reset();
      m_pendingSend[id] = false;
    }
    return;
  }
  m_clipboardRetryTimer = m_events->newOneShotTimer(kRetryDelay, nullptr);
  m_events->addHandler(EventTypes::Timer, m_clipboardRetryTimer, [this](const auto &) { handleClipboardRetry(); });
}

void Client::handleClipboardRetry()
{
  cleanupClipboardRetry();

  for (ClipboardID id = 0; id < kClipboardEnd; ++id) {
    if (m_pendingWrite[id]) {
      if (writeClipboard(id, *m_pendingWrite[id])) {
        LOG_INFO("clipboard %d updated from the server", id);
        m_pendingWrite[id].reset();
      }
    }
    // only while not on this computer: when it's active, leaving sends the clipboard
    if (m_pendingSend[id] && !m_active) {
      sendClipboard(id);
    }
  }

  bool pending = false;
  for (ClipboardID id = 0; id < kClipboardEnd; ++id)
    pending = pending || m_pendingWrite[id] || (m_pendingSend[id] && !m_active);
  if (pending) {
    scheduleClipboardRetry();
  } else {
    m_clipboardRetries = 0;
  }
}

void Client::reportClipboard(ClipboardID id, const deskflow::clipboard::Activity &activity) const
{
  // the X11 selection changes with every text selected: only the clipboard is news
  if (id == kClipboardClipboard)
    deskflow::clipboard::reportActivity(activity);
}

void Client::cleanupClipboardRetry()
{
  if (m_clipboardRetryTimer != nullptr) {
    m_events->removeHandler(EventTypes::Timer, m_clipboardRetryTimer);
    m_events->deleteTimer(m_clipboardRetryTimer);
    m_clipboardRetryTimer = nullptr;
  }
}

void Client::sendEvent(EventTypes type)
{
  m_events->addEvent(Event(type, getEventTarget()));
}

void Client::sendConnectionFailedEvent(const char *msg)
{
  auto *info = new FailInfo(msg);
  info->m_retry = true;
  Event event(EventTypes::ClientConnectionFailed, getEventTarget(), info, Event::EventFlags::DontFreeData);
  m_events->addEvent(std::move(event));
}

void Client::setupConnecting()
{
  assert(m_stream != nullptr);

  if (CoreSettings::value(CoreSettings::Security::TlsEnabled).toBool()) {
    m_events->addHandler(EventTypes::DataSocketSecureConnected, m_stream->getEventTarget(), [this](const auto &) {
      handleConnected();
    });
  } else {
    m_events->addHandler(EventTypes::DataSocketConnected, m_stream->getEventTarget(), [this](const auto &) {
      handleConnected();
    });
  }
  m_events->addHandler(EventTypes::DataSocketConnectionFailed, m_stream->getEventTarget(), [this](const auto &e) {
    handleConnectionFailed(e);
  });
}

void Client::setupConnection()
{
  assert(m_stream != nullptr);

  m_events->addHandler(EventTypes::ClientDisconnectRequested, m_stream->getEventTarget(), [this](const auto &e) {
    handleDisconnectRequested(e);
  });
  m_events->addHandler(EventTypes::SocketDisconnected, m_stream->getEventTarget(), [this](const auto &) {
    handleDisconnected();
  });
  m_events->addHandler(EventTypes::StreamInputReady, m_stream->getEventTarget(), [this](const auto &) {
    handleHello();
  });
  m_events->addHandler(EventTypes::StreamOutputError, m_stream->getEventTarget(), [this](const auto &) {
    handleOutputError();
  });
  m_events->addHandler(EventTypes::StreamInputShutdown, m_stream->getEventTarget(), [this](const auto &) {
    handleDisconnected();
  });
  m_events->addHandler(EventTypes::StreamOutputShutdown, m_stream->getEventTarget(), [this](const auto &) {
    handleDisconnected();
  });
}

bool Client::setupComputer(int16_t protocolMinor)
{
  assert(m_server == nullptr);

  m_ready = false;

  // only 1.6 and later have a proxy: the clipboard, mouse wheel and key message formats
  // differ below that, and nothing older (synergy 1.4 and earlier) still needs supporting.
  // a version with no case is refused by the hello handler.
  switch (protocolMinor) {
  case 6:
    m_server = new ServerProxy(this, m_stream, m_events);
    break;
  case 7:
    m_server = new ServerProxy1_7(this, m_stream, m_events);
    break;
  case 8:
    m_server = new ServerProxy1_8(this, m_stream, m_events);
    break;
  default:
    break;
  }

  if (m_server != nullptr) {
    m_events->addHandler(EventTypes::ComputerShapeChanged, getEventTarget(), [this](const auto &) {
      handleShapeChanged();
    });
    m_events->addHandler(EventTypes::ClipboardGrabbed, getEventTarget(), [this](const auto &e) {
      handleClipboardGrabbed(e);
    });
  }
  return m_server != nullptr;
}

void Client::setupTimer()
{
  assert(m_timer == nullptr);
  m_timer = m_events->newOneShotTimer(2.0, nullptr);
  m_events->addHandler(EventTypes::Timer, m_timer, [this](const auto &) { handleConnectTimeout(); });
}

void Client::cleanup()
{
  m_connectOnResume = false;
  cleanupTimer();
  cleanupComputer();
  cleanupConnecting();
  cleanupConnection();
}

void Client::cleanupConnecting()
{
  if (m_stream != nullptr) {
    m_events->removeHandler(EventTypes::DataSocketConnected, m_stream->getEventTarget());
    m_events->removeHandler(EventTypes::DataSocketSecureConnected, m_stream->getEventTarget());
    m_events->removeHandler(EventTypes::DataSocketConnectionFailed, m_stream->getEventTarget());
  }
}

void Client::cleanupConnection()
{
  if (m_stream != nullptr) {
    using enum EventTypes;
    m_events->removeHandler(StreamInputReady, m_stream->getEventTarget());
    m_events->removeHandler(StreamOutputError, m_stream->getEventTarget());
    m_events->removeHandler(StreamInputShutdown, m_stream->getEventTarget());
    m_events->removeHandler(StreamOutputShutdown, m_stream->getEventTarget());
    m_events->removeHandler(SocketDisconnected, m_stream->getEventTarget());
    m_events->removeHandler(ClientDisconnectRequested, m_stream->getEventTarget());
    cleanupStream();
  }
}

void Client::cleanupComputer()
{
  if (m_server != nullptr) {
    if (m_ready) {
      m_computer->disable();
      m_ready = false;
    }
    m_events->removeHandler(EventTypes::ComputerShapeChanged, getEventTarget());
    m_events->removeHandler(EventTypes::ClipboardGrabbed, getEventTarget());
    m_files.reset(); // deletes what was arriving
    m_filesSent = false;
    if (m_fileProgress) {
      m_fileProgress->finish();
    }
    delete m_server;
    m_server = nullptr;
  }
}

void Client::cleanupTimer()
{
  if (m_timer != nullptr) {
    m_events->removeHandler(EventTypes::Timer, m_timer);
    m_events->deleteTimer(m_timer);
    m_timer = nullptr;
  }
}

void Client::cleanupStream()
{
  delete m_stream;
  m_stream = nullptr;
}

void Client::handleConnected()
{
  LOG_VERBOSE("connected, waiting for hello");
  cleanupConnecting();
  setupConnection();

  // reset clipboard state
  for (ClipboardID id = 0; id < kClipboardEnd; ++id) {
    m_ownClipboard[id] = false;
    m_sentClipboard[id] = false;
    m_timeClipboard[id] = 0;
    m_pendingWrite[id].reset();
    m_pendingSend[id] = false;
  }
}

void Client::handleConnectionFailed(const Event &event)
{
  auto *info = static_cast<IDataSocket::ConnectionFailedInfo *>(event.getData());

  cleanupTimer();
  cleanupConnecting();
  cleanupStream();
  LOG_VERBOSE("connection failed");
  sendConnectionFailedEvent(info->m_what.c_str());
  delete info;
}

void Client::handleConnectTimeout()
{
  cleanupTimer();
  cleanupConnecting();
  cleanupConnection();
  cleanupStream();
  LOG_VERBOSE("connection timed out");
  sendConnectionFailedEvent("Timed out");
}

void Client::handleOutputError()
{
  cleanupTimer();
  cleanupComputer();
  cleanupConnection();
  LOG_WARN("error sending to server");
  sendEvent(EventTypes::ClientDisconnected);
}

void Client::handleDisconnected()
{
  cleanupTimer();
  cleanupComputer();
  cleanupConnection();
  LOG_VERBOSE("disconnected");
  sendEvent(EventTypes::ClientDisconnected);
}

void Client::handleDisconnectRequested(const Event &event)
{
  const auto *request = static_cast<const DisconnectRequest *>(event.getDataObject());
  if (request == nullptr) {
    disconnect(nullptr);
    return;
  }

  if (request->kind() == DisconnectRequest::Kind::Refuse) {
    refuseConnection(request->refusalReason(), request->message());
  } else {
    disconnect(request->message());
  }
}

void Client::handleShapeChanged()
{
  LOG_DEBUG("resolution changed");
  m_server->onInfoChanged();
}

void Client::handleClipboardGrabbed(const Event &event)
{
  if (!m_enableClipboard || (m_maximumClipboardSize == 0)) {
    return;
  }

  const auto *info = static_cast<const IComputer::ClipboardInfo *>(event.getData());

  // Deskflow writing the server's clipboard here, or another program putting the same
  // content back (clipboard history, newline conversion), is not a new copy: sending it
  // back would loop between the computers.
  if (auto &guard = m_echoGuard[info->m_id]; guard.armed()) {
    Clipboard local;
    if (m_computer->getClipboard(info->m_id, &local) && guard.isEcho(local)) {
      LOG_DEBUG("ignoring clipboard %d change, it's what the server sent", info->m_id);
      return;
    }
    guard.clear();
  }

  // grab ownership
  m_server->onGrabClipboard(info->m_id);
  if (info->m_id == kClipboardClipboard) {
    filesSuperseded("a newer copy replaces it");
  }

  // we now own the clipboard and it has not been sent to the server
  m_ownClipboard[info->m_id] = true;
  m_sentClipboard[info->m_id] = false;
  m_timeClipboard[info->m_id] = 0;

  // if we're not the active computer then send the clipboard now,
  // otherwise we'll wait until we leave.
  if (!m_active) {
    sendClipboard(info->m_id);
    if (info->m_id == kClipboardClipboard) {
      sendFiles();
    }
  }
}

void Client::handleHello()
{
  int16_t serverMajor;
  int16_t serverMinor;

  // as luck would have it, both "Synergy" and "Barrier" are 7 chars,
  // so we eat 7 chars and then test for either protocol name.
  // we cannot re-use `readf` to check for various hello messages,
  // as `readf` eats bytes (advances the stream position reference).
  std::string protocolName;
  ProtocolUtil::readf(m_stream, kMsgHello, &protocolName, &serverMajor, &serverMinor);

  if (const auto proto = networkProtocolFromString(protocolName); proto == NetworkProtocol::Unknown) {
    LOG_WARN("hello back received with protocol: '%s'", protocolName.c_str());
    sendConnectionFailedEvent("got invalid hello message from server");
    cleanupTimer();
    cleanupConnection();
    return;
  }

  if (serverMajor != kProtocolMajorVersion) {
    LOG_WARN("server protocol version not compatible: %d.%d", serverMajor, serverMinor);
    sendConnectionFailedEvent(IncompatibleClientException(serverMajor, serverMinor).what());
    cleanupTimer();
    cleanupConnection();
    return;
  }

  int16_t helloBackMinor = kProtocolMinorVersion;
  if (serverMinor < kProtocolMinorVersion) {
    helloBackMinor = serverMinor;
    LOG_INFO(
        "downgrading client protocol version from %d.%d to %d.%d", //
        kProtocolMajorVersion, kProtocolMinorVersion, kProtocolMajorVersion, helloBackMinor
    );
  }

  // no proxy speaks the negotiated version, so hang up as incompatible rather than
  // talk a version the client does not implement, the same rule the server applies
  // when it picks a client proxy
  if (!setupComputer(helloBackMinor)) {
    LOG_WARN("server protocol version not supported: %d.%d", serverMajor, serverMinor);
    sendConnectionFailedEvent(IncompatibleClientException(serverMajor, serverMinor).what());
    cleanupTimer();
    cleanupConnection();
    return;
  }

  LOG_DEBUG("saying hello back with version %s %d.%d", protocolName.c_str(), kProtocolMajorVersion, helloBackMinor);

  // dynamically build write format for hello back since `ProtocolUtil::writef`
  // doesn't support formatting fixed length strings yet.
  std::string helloBackMessage = protocolName + kMsgHelloBackArgs;
  ProtocolUtil::writef(m_stream, helloBackMessage.c_str(), kProtocolMajorVersion, helloBackMinor, &m_name);

  // now connected but waiting to complete handshake
  cleanupTimer();

  // make sure we process any remaining messages later.  we won't
  // receive another event for already pending messages so we fake
  // one.
  if (m_stream->isReady()) {
    m_events->addEvent(Event(EventTypes::StreamInputReady, m_stream->getEventTarget()));
  }
}

void Client::handleSuspend()
{
  if (!m_suspended) {
    LOG_INFO("suspend");
    m_suspended = true;
    bool wasConnected = isConnected();
    disconnect(nullptr);
    m_connectOnResume = wasConnected;
  }
}

void Client::handleResume()
{
  if (m_suspended) {
    LOG_INFO("resume");
    m_suspended = false;
    if (m_connectOnResume) {
      m_connectOnResume = false;
      connect();
    }
  }
}

void Client::bindNetworkInterface(IDataSocket *socket) const
{
  const auto address = CoreSettings::value(CoreSettings::Core::Interface).toString();
  if (address.empty())
    return;

  LOG_DEBUG("bind to network interface: %s", address.c_str());

  NetworkAddress bindAddress(address);
  bindAddress.resolve();

  socket->bind(bindAddress);
}
