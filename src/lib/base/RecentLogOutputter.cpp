/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "base/RecentLogOutputter.h"

#include <atomic>
#include <vector>

namespace {
std::atomic<const RecentLogOutputter *> s_instance = nullptr;
} // namespace

RecentLogOutputter::RecentLogOutputter(std::size_t maxLines) : m_maxLines(maxLines == 0 ? 1 : maxLines)
{
  s_instance = this;
}

RecentLogOutputter::~RecentLogOutputter()
{
  const RecentLogOutputter *self = this;
  s_instance.compare_exchange_strong(self, nullptr);
}

bool RecentLogOutputter::write(LogLevel::Level, const std::string &message)
{
  // must not log: Log holds its lock while outputters write
  auto line = message;
  while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
    line.pop_back();

  std::scoped_lock lock{m_mutex};
  m_lines.push_back(std::move(line));
  while (m_lines.size() > m_maxLines)
    m_lines.pop_front();
  return true;
}

std::string RecentLogOutputter::text(std::size_t maxBytes) const
{
  std::scoped_lock lock{m_mutex};
  // newest first until full, then put back in order
  std::vector<const std::string *> kept;
  std::size_t size = 0;
  for (auto line = m_lines.rbegin(); line != m_lines.rend(); ++line) {
    if (size + line->size() + 1 > maxBytes)
      break;
    size += line->size() + 1;
    kept.push_back(&*line);
  }
  std::string out;
  out.reserve(size);
  for (auto line = kept.rbegin(); line != kept.rend(); ++line) {
    out += **line;
    out += '\n';
  }
  return out;
}

const RecentLogOutputter *RecentLogOutputter::instance()
{
  return s_instance;
}
