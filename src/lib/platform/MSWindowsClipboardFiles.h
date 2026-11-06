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
  //@}
};
