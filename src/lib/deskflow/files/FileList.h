/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

//! Files copied to the clipboard, as they travel between computers (docs/dev/file-clipboard.md)
namespace deskflow::files {

//! One file or folder of a copy
struct Entry
{
  //! Relative to the copy, '/'-separated, UTF-8: "report.pdf", "photos/2026/a.jpg"
  std::string path;
  bool directory = false;
  std::uint64_t size = 0;    //!< bytes; 0 for folders
  std::int64_t modified = 0; //!< Unix seconds

  bool operator==(const Entry &) const = default;
};

//! What a computer accepts
struct Limits
{
  std::uint64_t maxBytes = 500ull * 1024 * 1024;
  std::size_t maxEntries = 10000;
  std::size_t maxDepth = 32;
  std::size_t maxPathBytes = 1024; //!< of one relative path, in UTF-8
};

//! The files and folders of one copy, folders before what they contain
struct FileList
{
  std::vector<Entry> entries;

  std::uint64_t totalBytes() const;
  std::size_t fileCount() const;

  //! The top-level names ("report.pdf", "photos"), in order
  std::vector<std::string> roots() const;

  bool operator==(const FileList &) const = default;
};

//! Why a path from another computer can't be used, or nothing if it can
/*!
Paths come from the network: relative only, '/'-separated, valid UTF-8, no "." or
".." parts, nothing Windows refuses or treats specially (drive letters, ':' streams,
\ < > " | ? *, control characters, trailing dots or spaces, device names such as CON
or COM1, even with an extension).
*/
std::optional<std::string> invalidPath(std::string_view path, const Limits &limits = {});

//! Why a whole list can't be accepted, or nothing if it can: every path valid, no
//! duplicates (ignoring case), no file inside a file, folders listed before their
//! contents, within the limits.
std::optional<std::string> invalidList(const FileList &list, const Limits &limits = {});

//! Binary form for the network
std::string serialize(const FileList &list);
//! Nothing for malformed data (validation is separate: invalidList)
std::optional<FileList> deserialize(std::string_view data);

//! A copy made on this computer: the list, and where each file is on disk
struct Selection
{
  FileList list;
  std::vector<std::filesystem::path> sources; //!< one per entry; empty for folders
};

//! Builds the list for files and folders selected on this computer
/*!
Folders are walked recursively. Symbolic links and junctions are skipped, never
followed. Fails (with the reason) when something can't be read or the copy breaks a
limit.
*/
std::variant<Selection, std::string>
collect(const std::vector<std::filesystem::path> &selected, const Limits &limits = {});

//! Unix seconds of a file's modification time
std::int64_t toUnixSeconds(std::filesystem::file_time_type time);
std::filesystem::file_time_type fromUnixSeconds(std::int64_t seconds);

} // namespace deskflow::files
