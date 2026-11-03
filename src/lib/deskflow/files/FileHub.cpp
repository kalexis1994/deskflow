/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/files/FileHub.h"

#include "base/Log.h"
#include "deskflow/files/FileTransfer.h"

namespace deskflow::files {

namespace fs = std::filesystem;

FileHub::FileHub(std::string serverName, Actions actions)
    : m_server(std::move(serverName)),
      m_actions(std::move(actions)),
      m_owner(m_server),
      m_active(m_server)
{
}

std::optional<std::string> FileHub::currentId() const
{
  return m_current ? std::optional(m_current->id) : std::nullopt;
}

void FileHub::stopTransfers(const std::string &reason)
{
  if (m_sendingTo) {
    m_actions.cancelSend(*m_sendingTo, reason);
    m_sendingTo.reset();
  }
  if (m_receivingFrom) {
    m_actions.cancelReceive(*m_receivingFrom, reason);
    m_receivingFrom.reset();
  }
}

void FileHub::grabbed(const std::string &computer)
{
  m_owner = computer;
  m_current.reset();
  stopTransfers("a newer copy replaces it");
}

void FileHub::localCopy(std::optional<Selection> selection)
{
  if (m_owner != m_server)
    return; // read late: someone else copied since

  if (!selection) {
    m_current.reset();
    return;
  }

  // read again each time the cursor leaves the server: the same files are the same copy
  if (m_current && m_current->owner == m_server && m_current->selection.list == selection->list &&
      m_current->selection.sources == selection->sources)
    return;

  stopTransfers("a newer copy replaces it");
  Copy copy; // no roots: the files are on this clipboard already
  copy.id = Cache::newId();
  copy.owner = m_server;
  copy.selection = std::move(*selection);
  m_current = std::move(copy);
  m_delivered[m_server] = m_current->id;
  LOG_INFO(
      "files copied here: %zu files, %llu bytes", m_current->selection.list.fileCount(),
      static_cast<unsigned long long>(m_current->selection.list.totalBytes())
  );
  deliver();
}

bool FileHub::accept(const std::string &from, const std::string &id)
{
  if (from != m_owner || from == m_server) {
    LOG_DEBUG("refusing files from \"%s\": \"%s\" copied last", from.c_str(), m_owner.c_str());
    return false;
  }
  if (m_receivingFrom && *m_receivingFrom != from)
    m_actions.cancelReceive(*m_receivingFrom, "a newer copy replaces it");
  m_receivingFrom = from;
  return true;
}

void FileHub::received(const std::string &from, const std::string &id, const std::vector<fs::path> &roots)
{
  if (m_receivingFrom == from)
    m_receivingFrom.reset();
  if (from != m_owner)
    return; // superseded while it arrived

  auto selection = collect(roots);
  if (const auto *error = std::get_if<std::string>(&selection)) {
    LOG_WARN("files from \"%s\" can't be passed on: %s", from.c_str(), error->c_str());
    return;
  }
  Copy copy;
  copy.id = id;
  copy.owner = from;
  copy.selection = std::get<Selection>(std::move(selection));
  copy.roots = roots;
  m_current = std::move(copy);
  m_delivered[from] = id;
  deliver();
}

void FileHub::receiveFailed(const std::string &from, const std::string &)
{
  if (m_receivingFrom == from)
    m_receivingFrom.reset();
}

void FileHub::entered(const std::string &computer)
{
  m_active = computer;
  deliver();
}

void FileHub::deliver()
{
  if (!m_current)
    return;
  const auto &id = m_current->id;
  if (const auto it = m_delivered.find(m_active); it != m_delivered.end() && it->second == id)
    return;

  if (m_active == m_server) {
    if (m_actions.setLocalFiles(m_current->roots)) {
      m_delivered[m_server] = id;
      LOG_INFO("files from \"%s\" are on the clipboard", m_current->owner.c_str());
    } else {
      LOG_WARN("could not put the files from \"%s\" on the clipboard", m_current->owner.c_str());
    }
    return;
  }

  if (!m_actions.canReceive(m_active))
    return;
  if (m_sendingTo == m_active)
    return; // on its way
  if (m_sendingTo)
    m_actions.cancelSend(*m_sendingTo, "the cursor moved to another computer");
  m_sendingTo = m_active;
  LOG_INFO(
      "sending files to \"%s\": %zu files, %llu bytes", m_active.c_str(), m_current->selection.list.fileCount(),
      static_cast<unsigned long long>(m_current->selection.list.totalBytes())
  );
  m_actions.send(m_active, id, m_current->selection);
}

void FileHub::sent(const std::string &to, const std::string &id)
{
  if (m_sendingTo == to)
    m_sendingTo.reset();
  m_delivered[to] = id;
  LOG_INFO("files sent to \"%s\"", to.c_str());
}

void FileHub::sendFailed(const std::string &to, const std::string &id)
{
  if (m_sendingTo == to)
    m_sendingTo.reset();
  // not again for this copy: whatever failed would most likely fail again
  m_delivered[to] = id;
}

void FileHub::disconnected(const std::string &computer)
{
  m_delivered.erase(computer);
  if (m_sendingTo == computer)
    m_sendingTo.reset();
  if (m_receivingFrom == computer)
    m_receivingFrom.reset();
  if (m_owner == computer && !m_current)
    m_owner = m_server;
}

} // namespace deskflow::files
