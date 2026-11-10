/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "base/ILogOutputter.h"

#include <cstddef>
#include <deque>
#include <mutex>
#include <string>

//! Keeps the newest log lines in memory, so a server can show its log to its clients
/*!
The service writes the core's log to a file on the computer the core runs on; a person
at a client, where the problem shows, can't read it there. A server hands these lines
to its clients when they ask (kMsgQServerLog).
*/
class RecentLogOutputter : public ILogOutputter
{
public:
  explicit RecentLogOutputter(std::size_t maxLines = 2000);
  ~RecentLogOutputter() override;

  void open(const std::string &) override
  {
  }
  void close() override
  {
  }
  bool write(LogLevel::Level level, const std::string &message) override;

  //! The newest lines that fit in \p maxBytes, oldest first, one per line
  std::string text(std::size_t maxBytes) const;

  //! The one this process keeps, if any
  static const RecentLogOutputter *instance();

private:
  mutable std::mutex m_mutex;
  std::deque<std::string> m_lines;
  std::size_t m_maxLines;
};
