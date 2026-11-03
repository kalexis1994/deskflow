/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/files/FileTransfer.h"

#include <functional>

namespace deskflow::files {

//! The file copies going over one connection: at most one out and one in
/*!
Knows nothing of sockets: messages leave through `Wire` and the connection's
parser hands the other computer's messages to the on...() methods.

Flow control: the receiver acknowledges every chunk and every finished file, and
no more than `kWindow` of them are ever unacknowledged. So little file data sits in
the socket at any time, and key and mouse messages written after it go out at once.

Every message carries the copy's id; messages for a copy that was cancelled (they
may still be on their way) are ignored. A transfer either completes with every
file verified or leaves nothing behind (see Writer).
*/
class FileChannel
{
public:
  static constexpr std::size_t kChunkBytes = 64 * 1024;
  static constexpr std::size_t kWindow = 8;
  //! Lists larger than this can't go in one message (the protocol's string limit)
  static constexpr std::size_t kMaxListBytes = 1000 * 1000;

  //! How messages reach the other computer
  struct Wire
  {
    std::function<void(const std::string &id, const std::string &list)> list;
    std::function<void(const std::string &id, std::uint32_t index, const std::string &data)> chunk;
    std::function<void(const std::string &id, std::uint32_t index, const std::string &digest)> done;
    std::function<void(const std::string &id)> ack;
    std::function<void(const std::string &id, const std::string &reason)> cancel;
  };

  //! What happened. Not called for what the owner cancels itself.
  struct Events
  {
    //! A copy is offered: the reason to refuse it, or nothing to accept it
    std::function<std::optional<std::string>(const std::string &id, const FileList &list)> accept;
    //! Every file arrived verified; the copy's top-level files and folders
    std::function<void(const std::string &id, const std::vector<std::filesystem::path> &roots)> received;
    std::function<void(const std::string &id, const std::string &reason)> receiveFailed;
    //! The other computer has every file
    std::function<void(const std::string &id)> sent;
    std::function<void(const std::string &id, const std::string &reason)> sendFailed;
  };

  struct Options
  {
    //! Where received copies go; nothing when there is no user to receive them
    std::function<std::optional<std::filesystem::path>()> cacheRoot;
    //! What this computer accepts
    Limits limits;
    //! What the other computer accepts (its size limit); larger copies aren't sent
    std::uint64_t peerMaxBytes = Limits{}.maxBytes;
  };

  FileChannel(Wire wire, Events events, Options options);
  ~FileChannel();
  FileChannel(const FileChannel &) = delete;
  FileChannel &operator=(const FileChannel &) = delete;

  //! @name sending
  //@{
  //! Starts sending a copy; one being sent is cancelled
  void send(const std::string &id, Selection selection);
  //! Stops sending and tells the other computer why
  void cancelSend(const std::string &reason);
  std::optional<std::string> sendingId() const;
  //@}

  //! @name receiving
  //@{
  //! Stops receiving, deletes what arrived, and tells the other computer why
  void cancelReceive(const std::string &reason);
  std::optional<std::string> receivingId() const;
  //@}

  void setPeerMaxBytes(std::uint64_t bytes)
  {
    m_options.peerMaxBytes = bytes;
  }
  std::uint64_t peerMaxBytes() const
  {
    return m_options.peerMaxBytes;
  }

  //! @name messages from the other computer
  //@{
  void onList(const std::string &id, const std::string &list);
  void onChunk(const std::string &id, std::uint32_t index, const std::string &data);
  void onDone(const std::string &id, std::uint32_t index, const std::string &digest);
  void onAck(const std::string &id);
  void onCancel(const std::string &id, const std::string &reason);
  //@}

private:
  struct Outgoing
  {
    std::string id;
    std::unique_ptr<Reader> reader;
    std::size_t unacknowledged = 0;
    bool read = false;               //!< every file was read
    std::optional<Chunk> pendingEnd; //!< a file's end, waiting for room in the window
  };

  struct Incoming
  {
    std::string id;
    std::unique_ptr<Writer> writer;
    std::uint32_t index = 0;
    std::uint64_t offset = 0; //!< in the file being received
  };

  void pump();
  bool write(const Chunk &chunk);
  void finishReceive();
  void failReceive(const std::string &reason);
  void dropIncoming();

  Wire m_wire;
  Events m_events;
  Options m_options;
  std::optional<Outgoing> m_out;
  std::optional<Incoming> m_in;
  bool m_pumping = false;
};

//! The largest copy this computer sends or accepts (the settings); 0 when copying files is off
std::uint64_t configuredMaxBytes();

} // namespace deskflow::files
