/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/files/FileList.h"

#include <chrono>

//! What the GUI's clipboard panel shows about copied files
namespace deskflow::files {

//! How a copy of files ended, for the panel's history (the `clipboard` IPC message,
//! as the other clipboard activity)
struct FilesActivity
{
  //! "sent" (this computer's copy went out), "received" (another computer's arrived
  //! here), "shared" (the server took a copy), "skipped" or "failed"
  std::string event;
  std::string computer; //!< the other computer, when known
  const FileList *list = nullptr;
  std::string detail; //!< skipped/failed: why, in English (from the logs)
};

//! One line of JSON: the clipboard activity fields plus
//! "files":{"count","folders","bytes","names"} and, when it failed, "reason" (a code
//! the panel explains: tooLarge, noUser, changed, damaged, diskFull, refused) and "detail"
std::string filesActivityJson(const FilesActivity &activity);
void reportFiles(const FilesActivity &activity);

//! The code for a reason given in English by the file transfer code; empty if none fits
std::string reasonCode(const std::string &detail);

//! Progress of a copy going out or coming in (the `fileProgress` IPC message)
/*!
Sent at most every 250 ms, plus the start; `finish()` tells the panel it is over
(the result follows as activity).
*/
class FileProgress
{
public:
  void update(const std::string &direction, const std::string &computer, const FileList &list, std::uint64_t done);
  void finish();

  //! {"direction":"send"|"receive","computer","count","bytes","done"}, or {"done":true} when over
  static std::string
  json(const std::string &direction, const std::string &computer, const FileList &list, std::uint64_t done);

private:
  std::chrono::steady_clock::time_point m_last;
  bool m_active = false;
};

} // namespace deskflow::files
