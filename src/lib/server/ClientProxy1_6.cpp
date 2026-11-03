/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2015 - 2016 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "server/ClientProxy1_6.h"

#include "base/Log.h"
#include "deskflow/ClipboardChunk.h"
#include "deskflow/ProtocolUtil.h"
#include "deskflow/StreamChunker.h"
#include "deskflow/files/FileChannel.h"
#include "io/IStream.h"
#include "server/Server.h"

#include <cstring>

//
// ClientProxy1_6
//

ClientProxy1_6::ClientProxy1_6(const std::string &name, deskflow::IStream *stream, Server *server, IEventQueue *events)
    : ClientProxy1_5(name, stream, server, events),
      m_events(events)
{
  m_events->addHandler(EventTypes::ClipboardSending, this, [this](const auto &e) {
    ClipboardChunk::send(getStream(), e.getDataObject());
  });
}

ClientProxy1_6::~ClientProxy1_6()
{
  m_events->removeHandler(EventTypes::ClipboardSending, this);
}

bool ClientProxy1_6::parseMessage(const uint8_t *code)
{
  if (memcmp(code, kMsgCFileClipboard, 4) == 0) {
    recvFileClipboard();
    return true;
  }
  if (recvFileMessage(code))
    return true;
  return ClientProxy1_5::parseMessage(code);
}

void ClientProxy1_6::recvFileClipboard()
{
  using namespace deskflow::files;

  uint32_t megabytes = 0;
  ProtocolUtil::readf(getStream(), kMsgCFileClipboard + 4, &megabytes);
  const auto maxBytes = m_server->fileClipboardMaxBytes();
  if (maxBytes == 0 || megabytes == 0)
    return; // never offered, or the client turned it off
  const auto peerMaxBytes = static_cast<uint64_t>(megabytes) * 1024 * 1024;
  if (m_files) {
    m_files->setPeerMaxBytes(peerMaxBytes);
    return;
  }
  LOG_INFO("client \"%s\" copies files, up to %u MB", getName().c_str(), megabytes);

  FileChannel::Wire wire;
  wire.list = [this](const std::string &id, const std::string &list) {
    LOG_DEBUG("send file list %s to \"%s\", %zu bytes", id.c_str(), getName().c_str(), list.size());
    ProtocolUtil::writef(getStream(), kMsgDFileList, &id, &list);
  };
  wire.chunk = [this](const std::string &id, uint32_t index, const std::string &data) {
    ProtocolUtil::writef(getStream(), kMsgDFileChunk, &id, index, &data);
  };
  wire.done = [this](const std::string &id, uint32_t index, const std::string &digest) {
    ProtocolUtil::writef(getStream(), kMsgDFileDone, &id, index, &digest);
  };
  wire.ack = [this](const std::string &id) { ProtocolUtil::writef(getStream(), kMsgDFileAck, &id); };
  wire.cancel = [this](const std::string &id, const std::string &reason) {
    LOG_DEBUG("send file cancel %s to \"%s\": %s", id.c_str(), getName().c_str(), reason.c_str());
    ProtocolUtil::writef(getStream(), kMsgDFileCancel, &id, &reason);
  };

  FileChannel::Events events;
  events.accept = [this](const std::string &id, const FileList &) { return m_server->acceptFiles(this, id); };
  events.received = [this](const std::string &id, const std::vector<std::filesystem::path> &roots) {
    m_server->filesReceived(this, id, roots);
  };
  events.receiveFailed = [this](const std::string &id, const std::string &reason) {
    m_server->filesReceiveFailed(this, id, reason);
  };
  events.sent = [this](const std::string &id) { m_server->filesSent(this, id); };
  events.sendFailed = [this](const std::string &id, const std::string &reason) {
    m_server->filesSendFailed(this, id, reason);
  };

  FileChannel::Options options;
  options.cacheRoot = userCacheRoot;
  options.limits.maxBytes = maxBytes;
  options.peerMaxBytes = peerMaxBytes;
  m_files = std::make_unique<FileChannel>(std::move(wire), std::move(events), std::move(options));
}

bool ClientProxy1_6::recvFileMessage(const uint8_t *code)
{
  // read whole even with nothing to give it to: the next message follows
  std::string id;
  std::string data;
  uint32_t index = 0;
  if (memcmp(code, kMsgDFileList, 4) == 0) {
    ProtocolUtil::readf(getStream(), kMsgDFileList + 4, &id, &data);
    LOG_DEBUG("recv file list %s from \"%s\", %zu bytes", id.c_str(), getName().c_str(), data.size());
    if (m_files)
      m_files->onList(id, data);
  } else if (memcmp(code, kMsgDFileChunk, 4) == 0) {
    ProtocolUtil::readf(getStream(), kMsgDFileChunk + 4, &id, &index, &data);
    if (m_files)
      m_files->onChunk(id, index, data);
  } else if (memcmp(code, kMsgDFileDone, 4) == 0) {
    ProtocolUtil::readf(getStream(), kMsgDFileDone + 4, &id, &index, &data);
    if (m_files)
      m_files->onDone(id, index, data);
  } else if (memcmp(code, kMsgDFileAck, 4) == 0) {
    ProtocolUtil::readf(getStream(), kMsgDFileAck + 4, &id);
    if (m_files)
      m_files->onAck(id);
  } else if (memcmp(code, kMsgDFileCancel, 4) == 0) {
    ProtocolUtil::readf(getStream(), kMsgDFileCancel + 4, &id, &data);
    LOG_DEBUG("recv file cancel %s from \"%s\": %s", id.c_str(), getName().c_str(), data.c_str());
    if (m_files)
      m_files->onCancel(id, data);
  } else {
    return false;
  }
  return true;
}

void ClientProxy1_6::setClipboard(ClipboardID id, const IClipboard *clipboard)
{
  // ignore if this clipboard is already clean
  if (m_clipboard[id].m_dirty) {
    // this clipboard is now clean
    m_clipboard[id].m_dirty = false;
    Clipboard::copy(&m_clipboard[id].m_clipboard, clipboard);

    std::string data = m_clipboard[id].m_clipboard.marshall();

    size_t size = data.size();
    LOG_DEBUG("sending clipboard %d to \"%s\"", id, getName().c_str());

    StreamChunker::sendClipboard(data, size, id, 0, m_events, this);
  }
}

bool ClientProxy1_6::recvClipboard()
{
  // parse message
  ClipboardID id;
  uint32_t seq;

  auto r = ClipboardChunk::assemble(
      getStream(), m_clipboardDataCached, id, seq, m_clipboardChunkState, m_server->getMaximumClipboardSizeBytes()
  );

  if (r == TransferState::Started) {
    size_t size = ClipboardChunk::getExpectedSize(m_clipboardChunkState);
    LOG_DEBUG("receiving clipboard %d size=%zu", id, size);
  } else if (r == TransferState::Finished) {
    LOG(
        (CLOG_DEBUG "received client \"%s\" clipboard %d seqnum=%d, size=%zu", getName().c_str(), id, seq,
         m_clipboardDataCached.size())
    );
    // save clipboard
    m_clipboard[id].m_clipboard.unmarshall(m_clipboardDataCached, 0);
    m_clipboard[id].m_sequenceNumber = seq;
    m_clipboardDataCached.clear();
    m_clipboardDataCached.shrink_to_fit();

    // notify
    auto *info = new ClipboardInfo;
    info->m_id = id;
    info->m_sequenceNumber = seq;
    m_events->addEvent(Event(EventTypes::ClipboardChanged, getEventTarget(), info));
  } else if (r == TransferState::Error) {
    return false;
  }

  return true;
}
