/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/IClipboard.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace deskflow::clipboard {

//! Reads any clipboard into marshalled data (Clipboard::marshall); nothing if it couldn't be opened
std::optional<std::string> snapshot(const IClipboard &source);

//! Whether marshalled clipboard data holds anything to paste (an empty clipboard, or one
//! with only formats Deskflow doesn't share such as files, has nothing)
bool hasContent(const std::string &marshalled);

//! Whether two marshalled clipboards hold the same content for a person pasting it
/*!
Text and HTML are compared ignoring line endings (CRLF and LF) and trailing line
breaks, which systems add or remove when they copy the same text back and forth;
bitmaps must match exactly. A format only one side has counts as a difference.
*/
bool sameContent(const std::string &a, const std::string &b);

//! The result of fitToLimit()
struct Fitted
{
  std::string data;                        //!< marshalled clipboard of what fits
  std::vector<IClipboard::Format> dropped; //!< formats left out to fit
  bool fits = false;                       //!< false when not even the text fits
};

//! Keeps as much of a marshalled clipboard as fits in a size limit
/*!
Drops the bitmap first, then the HTML, so a large screenshot or an Office copy (which
also carries a picture) still shares its text instead of nothing. A limit of 0 means
no limit.
*/
Fitted fitToLimit(const std::string &marshalled, std::size_t limitBytes);

//! "bitmap, HTML", for logs
std::string formatNames(const std::vector<IClipboard::Format> &formats);

//! Remembers what Deskflow last wrote to the local clipboard, to recognize it coming back
/*!
After Deskflow puts another computer's clipboard here, the system or another program
(clipboard history, a clipboard manager, the other system's newline conventions) may
write the same content again, which looks like a new local copy. Sending that back
starts a loop (deskflow/deskflow#10139). A copy whose content is the same as what we
last wrote is an echo and is not shared.
*/
class EchoGuard
{
public:
  //! Deskflow just wrote this (marshalled) content locally
  void applied(const std::string &marshalled);

  //! Whether the local content is what we last wrote; only reads it while armed.
  //! A clipboard that can't be read is not an echo.
  bool isEcho(const IClipboard &local) const;

  //! A real local copy happened: stop comparing until we write again
  void clear();

  bool armed() const
  {
    return m_content.has_value();
  }

private:
  std::optional<std::string> m_content;
};

//! Something that happened to the shared clipboard, for the GUI's clipboard panel
struct Activity
{
  //! "sent" (this computer's copy went out), "received" (another computer's arrived
  //! here), "shared" (the server took a computer's copy), "skipped" or "failed"
  std::string event;
  std::string computer;                    //!< the other computer, when known
  std::string reason;                      //!< skipped/failed: tooLarge, nothingToShare, busy
  std::vector<IClipboard::Format> dropped; //!< formats left out to fit the size limit
  std::string marshalled;                  //!< the content, when there is one
};

//! The activity as one line of JSON:
//! {"event","computer","reason","formats","dropped","size","chars","lines","image":{"width","height"},"text"}
/*!
"text" is the start of the copied text (at most kPreviewChars characters) and only
present with includeText; nothing else of the content is ever included.
*/
std::string activityJson(const Activity &activity, bool includeText);

//! Sends the activity to the GUI over IPC (clipboard=<json>); only the latest is kept
//! for a GUI that connects later. Text preview as the core/clipboardPreview setting says.
void reportActivity(const Activity &activity);

inline constexpr std::size_t kPreviewChars = 200;

} // namespace deskflow::clipboard
