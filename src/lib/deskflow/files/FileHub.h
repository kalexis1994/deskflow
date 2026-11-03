/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/files/FileList.h"

#include <functional>
#include <map>

namespace deskflow::files {

//! The server's side of copying files: which copy is current and who gets it
/*!
As for the other clipboard formats the server is the hub. A copy made on the server
is read from where it is; a copy made on a client comes to the server's cache when
the cursor leaves that client. Either way it goes to a computer when the cursor
enters it, once per copy:

- the server itself gets the cached files put on its clipboard;
- a client gets them sent, one client at a time: the cursor entering another client
  cancels a transfer still going to the previous one, which gets it again when the
  cursor comes back.

A new copy anywhere makes every transfer of the older one stop.
*/
class FileHub
{
public:
  //! What the hub has done; Server wires these to the computers
  struct Actions
  {
    //! Whether the computer can receive files (a client that agreed to)
    std::function<bool(const std::string &computer)> canReceive;
    std::function<void(const std::string &to, const std::string &id, Selection selection)> send;
    std::function<void(const std::string &to, const std::string &reason)> cancelSend;
    std::function<void(const std::string &from, const std::string &reason)> cancelReceive;
    //! Puts files on the server's clipboard; false if it couldn't
    std::function<bool(const std::vector<std::filesystem::path> &roots)> setLocalFiles;
  };

  FileHub(std::string serverName, Actions actions);

  //! @name what happened
  //@{
  //! A computer copied something (files or not)
  void grabbed(const std::string &computer);
  //! The server's clipboard was read after a copy on it: its files, if any
  void localCopy(std::optional<Selection> selection);
  //! A client offers its copy: whether to take it
  bool accept(const std::string &from, const std::string &id);
  void received(const std::string &from, const std::string &id, const std::vector<std::filesystem::path> &roots);
  void receiveFailed(const std::string &from, const std::string &id);
  //! The cursor entered a computer
  void entered(const std::string &computer);
  void sent(const std::string &to, const std::string &id);
  void sendFailed(const std::string &to, const std::string &id);
  void disconnected(const std::string &computer);
  //@}

  //! @name state
  //@{
  std::optional<std::string> currentId() const;
  //! The current copy's list; nullptr if there is none
  const FileList *currentList() const
  {
    return m_current ? &m_current->selection.list : nullptr;
  }
  std::optional<std::string> sendingTo() const
  {
    return m_sendingTo;
  }
  std::optional<std::string> receivingFrom() const
  {
    return m_receivingFrom;
  }
  //@}

private:
  struct Copy
  {
    std::string id;
    std::string owner;
    Selection selection;
    std::vector<std::filesystem::path> roots; //!< for the server's clipboard
  };

  void stopTransfers(const std::string &reason);
  void deliver();

  std::string m_server;
  Actions m_actions;
  std::string m_owner;  //!< who copied last
  std::string m_active; //!< where the cursor is
  std::optional<Copy> m_current;
  std::map<std::string, std::string> m_delivered; //!< computer: the copy it has
  std::optional<std::string> m_sendingTo;
  std::optional<std::string> m_receivingFrom;
};

} // namespace deskflow::files
