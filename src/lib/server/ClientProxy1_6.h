/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2015 - 2016 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/ClipboardChunk.h"
#include "server/ClientProxy1_5.h"

#include <memory>
#include <string>

namespace deskflow::files {
class FileChannel;
}

class Server;
class IEventQueue;

//! Proxy for client implementing protocol version 1.6
class ClientProxy1_6 : public ClientProxy1_5
{
public:
  ClientProxy1_6(const std::string &name, deskflow::IStream *adoptedStream, Server *server, IEventQueue *events);
  ~ClientProxy1_6() override;

  void setClipboard(ClipboardID id, const IClipboard *clipboard) override;
  bool recvClipboard() override;
  bool parseMessage(const uint8_t *code) override;
  deskflow::files::FileChannel *fileChannel() const override
  {
    return m_files.get();
  }

private:
  //! The client takes part in copying files
  void recvFileClipboard();
  //! A file message: true if \p code is one
  bool recvFileMessage(const uint8_t *code);

  std::unique_ptr<deskflow::files::FileChannel> m_files;
  IEventQueue *m_events;
  std::string m_clipboardDataCached;
  ClipboardChunkAssemblyState m_clipboardChunkState;
};
