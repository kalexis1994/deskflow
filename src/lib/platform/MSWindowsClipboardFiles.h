/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shellapi.h> // HDROP

//! Files on the Windows clipboard, as Explorer copies them (docs/dev/file-clipboard.md)
/*!
A copy in Explorer puts the files' paths on the clipboard as `CF_HDROP`. Received
files are put there the same way, with "Preferred DropEffect" set to copy so that
pasting never moves them out of the cache, and with Deskflow's ownership format so
that this computer doesn't take them for a new copy of its own.
*/
class MSWindowsClipboardFiles
{
public:
  //! What makes `deskflow-core` print the files on the clipboard and exit
  static constexpr auto kPrintFilesArg = "--print-clipboard-files";

  //! What makes `deskflow-core` print the whole clipboard, files included, and exit
  static constexpr auto kPrintClipboardArg = "--print-clipboard";

  //! The clipboard as its user sees it
  struct UserClipboard
  {
    std::string marshalled;                   //!< text, HTML and image, as `Clipboard::marshall`
    std::vector<std::filesystem::path> files; //!< the copied files, if any
  };

  //! What `kPrintClipboardArg` does: writes `packClipboard` to stdout; the exit code
  /*!
  Runs at the very start of the process, before logging exists; it keeps the log
  quiet, since stdout is where the answer goes.
  */
  static int printClipboard();

  //! Runs `program` with `kPrintClipboardArg` as the owner of `token`; nothing if it failed
  /*!
  SYSTEM, in the user's session, sees only the first format of a copy made through
  OLE, as Explorer, the Snipping Tool and many programs make them: the image of a
  screenshot never shows. The user's own process sees it all.
  */
  static std::optional<UserClipboard> readClipboardThroughHelper(const std::filesystem::path &program, HANDLE token);

  //! The clipboard read by a process of the session's user, when this one is SYSTEM
  /*!
  Nothing when this process isn't SYSTEM (it reads the clipboard itself), at the login
  screen (no user) or if the helper failed.
  */
  static std::optional<UserClipboard> readAsUser();

  //! The files on the clipboard; nothing if it holds none or couldn't be opened
  static std::optional<std::vector<std::filesystem::path>> get(HWND window);

  //! What `kPrintFilesArg` does: writes `packPaths` to stdout; the process's exit code
  /*!
  Runs at the very start of the process, before logging exists.
  */
  static int printFiles();

  //! Runs `program` with `kPrintFilesArg` as the owner of `token`; nullptr for this user
  /*!
  Empty if the clipboard holds no files, nothing if the helper failed.
  */
  static std::optional<std::vector<std::filesystem::path>>
  readThroughHelper(const std::filesystem::path &program, HANDLE token);

  //! Replaces the clipboard with these files; false if it couldn't
  static bool set(HWND window, const std::vector<std::filesystem::path> &files);

  //! @name the data, apart from the clipboard
  //@{
  //! `CF_HDROP` data (DROPFILES and wide paths); nullptr for no files or no memory
  static HGLOBAL makeDropFiles(const std::vector<std::filesystem::path> &files);
  //! The paths in `CF_HDROP` data, wide or ANSI
  static std::vector<std::filesystem::path> readDropFiles(HDROP drop);
  //! "Preferred DropEffect" data
  static HGLOBAL makeDropEffect(DWORD effect);
  //! The registered "Preferred DropEffect" format
  static UINT dropEffectFormat();
  //! Paths as the helper sends them: wide text, each one terminated
  static std::string packPaths(const std::vector<std::filesystem::path> &files);
  //! The paths in `packPaths` data; a path cut short by a truncated output is dropped
  static std::vector<std::filesystem::path> unpackPaths(const std::string &packed);
  //! The helper's answer: the marshalled clipboard's size (4 bytes, big-endian), the
  //! marshalled clipboard, then `packPaths`
  static std::string packClipboard(const UserClipboard &clipboard);
  //! What `packClipboard` wrote; nothing if it is cut short
  static std::optional<UserClipboard> unpackClipboard(const std::string &packed);
  //@}

private:
  //! Runs `program` with `argument` as the owner of `token` and returns what it printed
  static std::optional<std::string> runHelper(const std::filesystem::path &program, HANDLE token, const char *argument);
  //! Writes all of `data` to stdout
  static bool writeOutput(const std::string &data);
};
