/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/files/FileChannel.h"

#include "common/CoreSettings.h"

#include <algorithm>
#include <cstring>

namespace deskflow::files {

namespace fs = std::filesystem;

namespace {

//! Owners may leave out the events they don't need
template <typename F, typename... Args> void notify(const F &f, Args &&...args)
{
  if (f)
    f(std::forward<Args>(args)...);
}

std::string megabytes(std::uint64_t bytes)
{
  return std::to_string(bytes / (1024 * 1024)) + " MB";
}

} // namespace

FileChannel::FileChannel(Wire wire, Events events, Options options)
    : m_wire(std::move(wire)),
      m_events(std::move(events)),
      m_options(std::move(options))
{
}

FileChannel::~FileChannel()
{
  dropIncoming();
}

//
// sending
//

void FileChannel::send(const std::string &id, Selection selection)
{
  if (m_out)
    cancelSend("a newer copy replaces it");

  auto list = serialize(selection.list);
  if (list.size() > kMaxListBytes) {
    notify(m_events.sendFailed, id, "too many files to send at once");
    return;
  }
  if (const auto total = selection.list.totalBytes(); total > m_options.peerMaxBytes) {
    notify(
        m_events.sendFailed, id,
        "the copy is larger than the " + megabytes(m_options.peerMaxBytes) + " the other computer accepts"
    );
    return;
  }

  m_out = Outgoing{id, std::make_unique<Reader>(std::move(selection))};
  m_wire.list(id, list);
  pump();
}

void FileChannel::cancelSend(const std::string &reason)
{
  if (!m_out)
    return;
  const auto id = m_out->id;
  m_out.reset();
  m_wire.cancel(id, reason);
}

std::optional<std::string> FileChannel::sendingId() const
{
  return m_out ? std::optional(m_out->id) : std::nullopt;
}

void FileChannel::pump()
{
  // a wire that delivers at once could acknowledge from inside this loop
  if (m_pumping)
    return;
  m_pumping = true;

  while (m_out && m_out->unacknowledged < kWindow) {
    if (m_out->pendingEnd) {
      const auto &end = *m_out->pendingEnd;
      m_wire.done(m_out->id, end.index, std::string(end.digest.begin(), end.digest.end()));
      ++m_out->unacknowledged;
      m_out->pendingEnd.reset();
      continue;
    }
    if (m_out->read)
      break;

    Chunk chunk;
    std::string error;
    Reader::Result result;
    {
      AsSessionUser asUser; // the user's network drives
      result = m_out->reader->next(kChunkBytes, chunk, error);
    }

    if (result == Reader::Result::Error) {
      const auto id = m_out->id;
      m_out.reset();
      m_wire.cancel(id, error);
      m_pumping = false;
      notify(m_events.sendFailed, id, error);
      return;
    }
    if (result == Reader::Result::Done) {
      m_out->read = true;
      break;
    }
    // an empty file is only its end
    if (!chunk.data.empty()) {
      m_wire.chunk(m_out->id, chunk.index, chunk.data);
      ++m_out->unacknowledged;
      m_out->sentBytes += chunk.data.size();
      notify(m_events.progress, m_out->id, true, m_out->reader->list(), m_out->sentBytes);
    }
    if (chunk.last) {
      chunk.data.clear();
      m_out->pendingEnd = std::move(chunk); // its own turn: the window holds
    }
  }

  m_pumping = false;
  if (m_out && m_out->read && !m_out->pendingEnd && m_out->unacknowledged == 0) {
    const auto id = m_out->id;
    m_out.reset();
    notify(m_events.sent, id);
  }
}

void FileChannel::onAck(const std::string &id)
{
  if (!m_out || m_out->id != id || m_out->unacknowledged == 0)
    return; // a cancelled copy's
  --m_out->unacknowledged;
  pump();
}

//
// receiving
//

void FileChannel::cancelReceive(const std::string &reason)
{
  if (!m_in)
    return;
  const auto id = m_in->id;
  dropIncoming();
  m_wire.cancel(id, reason);
}

std::optional<std::string> FileChannel::receivingId() const
{
  return m_in ? std::optional(m_in->id) : std::nullopt;
}

void FileChannel::dropIncoming()
{
  if (!m_in)
    return;
  AsSessionUser asUser; // its files are the user's
  m_in.reset();
}

void FileChannel::failReceive(const std::string &reason)
{
  const auto id = m_in->id;
  dropIncoming();
  m_wire.cancel(id, reason);
  notify(m_events.receiveFailed, id, reason);
}

void FileChannel::onList(const std::string &id, const std::string &data)
{
  // the sender moved on to a newer copy
  dropIncoming();

  const auto refuse = [this, &id](const std::string &reason, bool report) {
    m_wire.cancel(id, reason);
    if (report)
      notify(m_events.receiveFailed, id, reason);
  };

  auto list = deserialize(data);
  if (!list) {
    refuse("the file list is malformed", true);
    return;
  }
  if (m_events.accept) {
    if (auto why = m_events.accept(id, *list)) {
      refuse(*why, false);
      return;
    }
  }
  const auto root = m_options.cacheRoot ? m_options.cacheRoot() : std::nullopt;
  if (!root) {
    refuse("no user is logged on to receive the files", true);
    return;
  }

  AsSessionUser asUser;
  if (!asUser.ok()) {
    refuse("no user is logged on to receive the files", true);
    return;
  }
  Cache(*root).cleanup();
  auto begun = Writer::begin(*root, id, std::move(*list), m_options.limits);
  if (const auto *error = std::get_if<std::string>(&begun)) {
    refuse(*error, true);
    return;
  }
  m_in = Incoming{id, std::get<std::unique_ptr<Writer>>(std::move(begun))};

  // only folders: nothing more comes
  if (m_in->writer->complete())
    finishReceive();
}

bool FileChannel::write(const Chunk &chunk)
{
  if (chunk.index != m_in->index) {
    m_in->index = chunk.index;
    m_in->offset = 0;
  }
  Chunk placed = chunk;
  placed.offset = m_in->offset;

  std::optional<std::string> error;
  {
    AsSessionUser asUser;
    error = m_in->writer->write(placed);
  }
  if (error) {
    failReceive(*error);
    return false;
  }
  m_in->offset += chunk.data.size();
  return true;
}

void FileChannel::onChunk(const std::string &id, std::uint32_t index, const std::string &data)
{
  if (!m_in || m_in->id != id)
    return; // a cancelled copy's
  if (data.empty()) {
    failReceive("an empty chunk");
    return;
  }
  Chunk chunk;
  chunk.index = index;
  chunk.data = data;
  if (!write(chunk))
    return;
  m_wire.ack(id);
  notify(m_events.progress, id, false, m_in->writer->list(), m_in->writer->receivedBytes());
}

void FileChannel::onDone(const std::string &id, std::uint32_t index, const std::string &digest)
{
  if (!m_in || m_in->id != id)
    return;
  Chunk chunk;
  chunk.index = index;
  chunk.last = true;
  if (digest.size() != chunk.digest.size()) {
    failReceive("a malformed SHA-256");
    return;
  }
  std::memcpy(chunk.digest.data(), digest.data(), chunk.digest.size());
  if (!write(chunk))
    return;

  // the last file's acknowledgment means the copy is in place
  if (m_in->writer->complete())
    finishReceive();
  else
    m_wire.ack(id);
}

void FileChannel::finishReceive()
{
  const auto id = m_in->id;
  std::variant<std::vector<fs::path>, std::string> committed;
  {
    AsSessionUser asUser;
    committed = m_in->writer->commit();
  }
  if (const auto *error = std::get_if<std::string>(&committed)) {
    failReceive(*error);
    return;
  }
  dropIncoming();
  m_wire.ack(id);
  notify(m_events.received, id, std::get<std::vector<fs::path>>(committed));
}

void FileChannel::onCancel(const std::string &id, const std::string &reason)
{
  if (m_out && m_out->id == id) {
    m_out.reset();
    notify(m_events.sendFailed, id, "the other computer stopped it: " + reason);
  }
  if (m_in && m_in->id == id) {
    dropIncoming();
    notify(m_events.receiveFailed, id, "the other computer stopped it: " + reason);
  }
}

std::uint64_t configuredMaxBytes()
{
  if (!CoreSettings::value(CoreSettings::Core::FileClipboard).toBool())
    return 0;
  const auto megabytes = CoreSettings::value(CoreSettings::Core::FileClipboardMaxSize).toInt();
  return megabytes > 0 ? static_cast<std::uint64_t>(megabytes) * 1024 * 1024 : 0;
}

} // namespace deskflow::files
