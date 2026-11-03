/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/files/FileList.h"

#include <array>
#include <fstream>
#include <memory>

namespace deskflow::files {

using Digest = std::array<std::uint8_t, 32>;

//! SHA-256 over data given in pieces
class Sha256
{
public:
  Sha256();
  ~Sha256();
  Sha256(const Sha256 &) = delete;
  Sha256 &operator=(const Sha256 &) = delete;

  void update(std::string_view data);
  Digest finish();

private:
  void *m_context;
};

//! A piece of one file of a copy
struct Chunk
{
  std::uint32_t index = 0;  //!< the file's entry in the list
  std::uint64_t offset = 0; //!< where the data goes in the file
  std::string data;
  bool last = false; //!< the file ends with this chunk; digest is its SHA-256
  Digest digest{};
};

//! Reads the files of a copy made here, in list order, a chunk at a time
/*!
A file that changes while it is read (different size or modification time from when
the copy was listed) fails the transfer: half old and half new content must never
reach the other computer.
*/
class Reader
{
public:
  explicit Reader(Selection selection);

  enum class Result
  {
    Chunk, //!< `chunk` holds the next piece
    Done,  //!< every file was read
    Error  //!< `error` says why; nothing more comes
  };

  Result next(std::size_t maxBytes, Chunk &chunk, std::string &error);

  const FileList &list() const
  {
    return m_selection.list;
  }

private:
  Result fail(std::string reason, std::string &error);

  Selection m_selection;
  std::optional<std::string> m_failed;
  std::size_t m_index = 0;
  std::ifstream m_file;
  bool m_open = false;
  std::uint64_t m_offset = 0;
  std::unique_ptr<Sha256> m_hash;
};

//! Receives a copy from another computer into the cache, all or nothing
/*!
Files go into `<root>/<id>.partial/` and move to `<root>/<id>/` only once every file
arrived with the size and SHA-256 its sender announced. Chunks must come in list
order. Anything wrong (a bad list, a chunk out of order or too long, a different
hash, no disk space) fails the whole copy, and destroying the writer before commit
deletes what was written.
*/
class Writer
{
public:
  static std::variant<std::unique_ptr<Writer>, std::string>
  begin(const std::filesystem::path &root, const std::string &id, FileList list, const Limits &limits = {});

  ~Writer();
  Writer(const Writer &) = delete;
  Writer &operator=(const Writer &) = delete;

  //! The reason it failed, or nothing
  std::optional<std::string> write(const Chunk &chunk);

  bool complete() const;
  std::uint64_t receivedBytes() const
  {
    return m_received;
  }
  const FileList &list() const
  {
    return m_list;
  }

  //! Moves the verified files into place; their top-level paths, or why it failed
  std::variant<std::vector<std::filesystem::path>, std::string> commit();

private:
  Writer(std::filesystem::path staging, std::filesystem::path final, FileList list);
  std::string fail(std::string reason);
  void abort();
  void skipFolders();
  std::filesystem::path stagingPath(const Entry &entry) const;

  std::filesystem::path m_staging;
  std::filesystem::path m_final;
  FileList m_list;
  std::size_t m_index = 0; //!< the file being received
  std::ofstream m_file;
  bool m_open = false;
  std::uint64_t m_written = 0; //!< of the current file
  std::uint64_t m_received = 0;
  std::unique_ptr<Sha256> m_hash;
  std::optional<std::string> m_failed;
  bool m_committed = false;
};

//! Where received copies are kept
class Cache
{
public:
  explicit Cache(std::filesystem::path root) : m_root(std::move(root))
  {
  }

  //! A fresh name for a copy: 16 random hex digits
  static std::string newId();

  //! Deletes leftover partial copies and all but the newest `keep` complete ones
  /*!
  Only touches folders named like copies (16 hex digits, maybe ".partial"), never
  anything else that may be in the folder. Call it before receiving a new copy, not
  while one is being received.
  */
  void cleanup(std::size_t keep = 2) const;

  const std::filesystem::path &root() const
  {
    return m_root;
  }

private:
  std::filesystem::path m_root;
};

//! The logged-on user's cache: %LOCALAPPDATA%\Deskflow\Clipboard on Windows (that of
//! the session's user when this process is SYSTEM), ~/.cache/deskflow/clipboard or
//! ~/Library/Caches/Deskflow/Clipboard elsewhere. Nothing at a login screen.
std::optional<std::filesystem::path> userCacheRoot();

//! While it exists, this thread reads and writes files as the logged-on user
/*!
Only does something when this process runs as SYSTEM (the service starts the core
that way): the user's network drives become reachable and the files it writes
belong to the user. Elsewhere it does nothing.
*/
class AsSessionUser
{
public:
  AsSessionUser();
  ~AsSessionUser();
  AsSessionUser(const AsSessionUser &) = delete;
  AsSessionUser &operator=(const AsSessionUser &) = delete;

  //! False at a login screen, when there is no user to act as
  bool ok() const
  {
    return m_ok;
  }

private:
  bool m_ok = true;
  bool m_impersonating = false;
};

//! Lowercase hex
std::string toHex(const Digest &digest);

//! 1 to 64 hex digits: safe as a folder name
bool validId(std::string_view id);

} // namespace deskflow::files
