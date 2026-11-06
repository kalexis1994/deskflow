/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/files/FileTransfer.h"

#include "arch/Utf8.h"

#include <openssl/evp.h>

#include <algorithm>
#include <cstdlib>
#include <random>

#if defined(DESKFLOW_OS_WIN)
#include <KnownFolders.h>
#include <ShlObj.h>
#include <WtsApi32.h>
#endif

namespace deskflow::files {

namespace fs = std::filesystem;

namespace {

//! Room left on the disk after a copy, for everything else
constexpr std::uint64_t kSpareBytes = 64ull * 1024 * 1024;

#if defined(DESKFLOW_OS_WIN)
//! What Explorer and most programs still handle
constexpr std::size_t kMaxWindowsPath = 259;

#endif

bool sameTime(const fs::path &path, std::int64_t modified)
{
  std::error_code ec;
  const auto time = fs::last_write_time(path, ec);
  return !ec && toUnixSeconds(time) == modified;
}

} // namespace

std::string toHex(const Digest &digest)
{
  static constexpr char kDigits[] = "0123456789abcdef";
  std::string out;
  out.reserve(digest.size() * 2);
  for (const auto byte : digest) {
    out += kDigits[byte >> 4];
    out += kDigits[byte & 0x0F];
  }
  return out;
}

bool validId(std::string_view id)
{
  return !id.empty() && id.size() <= 64 &&
         std::all_of(id.begin(), id.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

//
// Sha256
//

Sha256::Sha256() : m_context(EVP_MD_CTX_new())
{
  EVP_DigestInit_ex(static_cast<EVP_MD_CTX *>(m_context), EVP_sha256(), nullptr);
}

Sha256::~Sha256()
{
  EVP_MD_CTX_free(static_cast<EVP_MD_CTX *>(m_context));
}

void Sha256::update(std::string_view data)
{
  if (!data.empty())
    EVP_DigestUpdate(static_cast<EVP_MD_CTX *>(m_context), data.data(), data.size());
}

Digest Sha256::finish()
{
  Digest digest{};
  unsigned int size = 0;
  EVP_DigestFinal_ex(static_cast<EVP_MD_CTX *>(m_context), digest.data(), &size);
  return digest;
}

//
// Reader
//

Reader::Reader(Selection selection) : m_selection(std::move(selection))
{
}

Reader::Result Reader::fail(std::string reason, std::string &error)
{
  m_file.close();
  m_open = false;
  m_failed = std::move(reason);
  error = *m_failed;
  return Result::Error;
}

Reader::Result Reader::next(std::size_t maxBytes, Chunk &chunk, std::string &error)
{
  if (m_failed) {
    error = *m_failed;
    return Result::Error;
  }
  const auto &entries = m_selection.list.entries;
  if (entries.size() != m_selection.sources.size())
    return fail("the list and its sources don't match", error);

  if (!m_open) {
    while (m_index < entries.size() && entries[m_index].directory)
      ++m_index;
    if (m_index >= entries.size())
      return Result::Done;

    const auto &entry = entries[m_index];
    const auto &source = m_selection.sources[m_index];
    std::error_code ec;
    const auto size = fs::file_size(source, ec);
    if (ec)
      return fail(entry.path + ": " + ec.message(), error);
    if (size != entry.size || !sameTime(source, entry.modified))
      return fail(entry.path + ": changed after it was copied; copy it again", error);
    m_file.open(source, std::ios::binary);
    if (!m_file)
      return fail(entry.path + ": can't be opened", error);
    m_open = true;
    m_offset = 0;
    m_hash = std::make_unique<Sha256>();
  }

  const auto &entry = entries[m_index];
  const auto want = static_cast<std::size_t>(
      (std::min)(static_cast<std::uint64_t>((std::max)(maxBytes, std::size_t{1})), entry.size - m_offset)
  );
  chunk.index = static_cast<std::uint32_t>(m_index);
  chunk.offset = m_offset;
  chunk.data.resize(want);
  if (want > 0)
    m_file.read(chunk.data.data(), static_cast<std::streamsize>(want));
  if (static_cast<std::size_t>(m_file.gcount()) != want)
    return fail(entry.path + ": changed while it was being sent (shorter)", error);
  m_hash->update(chunk.data);
  m_offset += want;

  chunk.last = m_offset == entry.size;
  if (chunk.last) {
    // nothing must follow, and the file must still be the one that was listed
    if (m_file.peek() != std::ifstream::traits_type::eof())
      return fail(entry.path + ": changed while it was being sent (longer)", error);
    m_file.close();
    m_open = false;
    std::error_code ec;
    const auto &source = m_selection.sources[m_index];
    if (fs::file_size(source, ec) != entry.size || ec || !sameTime(source, entry.modified))
      return fail(entry.path + ": changed while it was being sent", error);
    chunk.digest = m_hash->finish();
    m_hash.reset();
    ++m_index;
  }
  return Result::Chunk;
}

//
// Writer
//

Writer::Writer(fs::path staging, fs::path final, FileList list)
    : m_staging(std::move(staging)),
      m_final(std::move(final)),
      m_list(std::move(list))
{
}

Writer::~Writer()
{
  if (!m_committed)
    abort();
}

std::variant<std::unique_ptr<Writer>, std::string>
Writer::begin(const fs::path &root, const std::string &id, FileList list, const Limits &limits)
{
  if (!validId(id))
    return "invalid transfer id";
  if (auto why = invalidList(list, limits))
    return *why;

  std::error_code ec;
  fs::create_directories(root, ec);
  if (ec)
    return pathToUtf8(root) + ": " + ec.message();

  const auto space = fs::space(root, ec);
  if (ec)
    return pathToUtf8(root) + ": " + ec.message();
  if (space.available < list.totalBytes() + kSpareBytes)
    return "not enough free disk space";

  auto staging = root / (id + ".partial");
  auto final = root / id;
  if (fs::exists(staging, ec) || fs::exists(final, ec))
    return "a copy with this id already exists";

#if defined(DESKFLOW_OS_WIN)
  for (const auto &e : list.entries) {
    if ((final / utf8Path(e.path)).native().size() > kMaxWindowsPath)
      return e.path + ": the path would be too long on this computer";
  }
#endif

  if (!fs::create_directory(staging, ec) || ec)
    return pathToUtf8(staging) + ": " + (ec ? ec.message() : "already exists");

  std::unique_ptr<Writer> writer(new Writer(staging, final, std::move(list)));

  // folders now: a name clash the list check can't see (case of non-ASCII letters)
  // shows up as a folder that already exists
  for (const auto &e : writer->m_list.entries) {
    if (!e.directory)
      continue;
    if (!fs::create_directory(writer->stagingPath(e), ec) || ec)
      return writer->fail(e.path + ": " + (ec ? ec.message() : "listed twice"));
  }
  writer->skipFolders();
  return std::move(writer);
}

fs::path Writer::stagingPath(const Entry &entry) const
{
  return m_staging / utf8Path(entry.path);
}

void Writer::skipFolders()
{
  while (m_index < m_list.entries.size() && m_list.entries[m_index].directory)
    ++m_index;
}

std::string Writer::fail(std::string reason)
{
  if (!m_failed)
    m_failed = std::move(reason);
  abort();
  return *m_failed;
}

void Writer::abort()
{
  if (m_open) {
    m_file.close();
    m_open = false;
  }
  m_hash.reset();
  std::error_code ec;
  fs::remove_all(m_staging, ec);
}

std::optional<std::string> Writer::write(const Chunk &chunk)
{
  if (m_failed)
    return *m_failed;
  if (m_committed)
    return "the copy is already complete";

  skipFolders();
  if (m_index >= m_list.entries.size())
    return fail("more data than the list announced");
  const auto &entry = m_list.entries[m_index];
  if (chunk.index != m_index)
    return fail("data for the wrong file (expected " + entry.path + ")");
  if (chunk.offset != m_written)
    return fail(entry.path + ": data out of order");
  if (chunk.data.size() > entry.size - m_written)
    return fail(entry.path + ": more data than its size");

  const auto target = stagingPath(entry);
  if (!m_open) {
    std::error_code ec;
    if (fs::exists(fs::symlink_status(target, ec)))
      return fail(entry.path + ": listed twice");
    m_file.open(target, std::ios::binary | std::ios::trunc);
    if (!m_file)
      return fail(entry.path + ": can't be created");
    m_open = true;
    m_hash = std::make_unique<Sha256>();
  }

  if (!chunk.data.empty()) {
    m_file.write(chunk.data.data(), static_cast<std::streamsize>(chunk.data.size()));
    if (!m_file)
      return fail(entry.path + ": can't be written (is the disk full?)");
    m_hash->update(chunk.data);
    m_written += chunk.data.size();
    m_received += chunk.data.size();
  }

  if (!chunk.last)
    return std::nullopt;

  if (m_written != entry.size)
    return fail(entry.path + ": shorter than announced");
  m_file.close();
  m_open = false;
  if (m_file.fail())
    return fail(entry.path + ": can't be written (is the disk full?)");
  if (m_hash->finish() != chunk.digest)
    return fail(entry.path + ": arrived damaged (SHA-256 differs)");
  m_hash.reset();

  std::error_code ec;
  fs::last_write_time(target, fromUnixSeconds(entry.modified), ec); // a nicety, not a failure
  ++m_index;
  m_written = 0;
  skipFolders();
  return std::nullopt;
}

bool Writer::complete() const
{
  if (m_failed || m_open)
    return false;
  return std::all_of(
      m_list.entries.begin() + static_cast<std::ptrdiff_t>(m_index), m_list.entries.end(),
      [](const auto &e) { return e.directory; }
  );
}

std::variant<std::vector<fs::path>, std::string> Writer::commit()
{
  if (m_failed)
    return *m_failed;
  if (m_committed)
    return "the copy was already committed";
  if (!complete())
    return fail("the copy is incomplete");

  // folder times last, deepest first: writing their contents changed them
  std::error_code ec;
  for (auto it = m_list.entries.rbegin(); it != m_list.entries.rend(); ++it) {
    if (it->directory)
      fs::last_write_time(stagingPath(*it), fromUnixSeconds(it->modified), ec);
  }

  fs::rename(m_staging, m_final, ec);
  if (ec)
    return fail(pathToUtf8(m_final) + ": " + ec.message());
  m_committed = true;

  std::vector<fs::path> roots;
  for (const auto &name : m_list.roots())
    roots.push_back(m_final / utf8Path(name));
  return roots;
}

//
// Cache
//

std::string Cache::newId()
{
  std::random_device random;
  std::uniform_int_distribution<int> digit(0, 15);
  static constexpr char kDigits[] = "0123456789abcdef";
  std::string id;
  for (int i = 0; i < 16; ++i)
    id += kDigits[digit(random)];
  return id;
}

void Cache::cleanup(std::size_t keep) const
{
  std::error_code ec;
  std::vector<std::pair<fs::file_time_type, fs::path>> complete;
  for (fs::directory_iterator it(m_root, ec), end; !ec && it != end; it.increment(ec)) {
    const auto name = pathToUtf8(it->path().filename());
    std::error_code entryEc;
    if (!it->is_directory(entryEc) || it->is_symlink(entryEc))
      continue;
    if (name.ends_with(".partial") && validId(std::string_view(name).substr(0, name.size() - 8))) {
      fs::remove_all(it->path(), entryEc);
    } else if (name.size() == 16 && validId(name)) {
      complete.emplace_back(it->last_write_time(entryEc), it->path());
    }
  }

  std::sort(complete.begin(), complete.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
  for (std::size_t i = keep; i < complete.size(); ++i)
    fs::remove_all(complete[i].second, ec);
}

//
// The user's cache and identity
//

#if defined(DESKFLOW_OS_WIN)
bool runningAsSystem()
{
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
    return false;
  bool system = false;
  DWORD size = 0;
  GetTokenInformation(token, TokenUser, nullptr, 0, &size);
  std::string buffer(size, '\0');
  if (size > 0 && GetTokenInformation(token, TokenUser, buffer.data(), size, &size)) {
    const auto *user = reinterpret_cast<const TOKEN_USER *>(buffer.data());
    system = IsWellKnownSid(user->User.Sid, WinLocalSystemSid) != FALSE;
  }
  CloseHandle(token);
  return system;
}

HANDLE sessionUserToken()
{
  DWORD session = 0;
  if (!ProcessIdToSessionId(GetCurrentProcessId(), &session))
    return nullptr;
  HANDLE token = nullptr;
  if (!WTSQueryUserToken(session, &token))
    return nullptr;
  return token;
}
#endif

std::optional<fs::path> userCacheRoot()
{
#if defined(DESKFLOW_OS_WIN)
  HANDLE token = nullptr;
  if (runningAsSystem()) {
    token = sessionUserToken();
    if (token == nullptr)
      return std::nullopt;
  }
  PWSTR folder = nullptr;
  const auto result = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, token, &folder);
  if (token != nullptr)
    CloseHandle(token);
  if (FAILED(result)) {
    CoTaskMemFree(folder);
    return std::nullopt;
  }
  fs::path root(folder);
  CoTaskMemFree(folder);
  return root / L"Deskflow" / L"Clipboard";
#else
  const char *home = std::getenv("HOME");
#if defined(DESKFLOW_OS_MAC)
  if (home == nullptr || *home == '\0')
    return std::nullopt;
  return fs::path(home) / "Library" / "Caches" / "Deskflow" / "Clipboard";
#else
  if (const char *cache = std::getenv("XDG_CACHE_HOME"); cache != nullptr && *cache == '/')
    return fs::path(cache) / "deskflow" / "clipboard";
  if (home == nullptr || *home == '\0')
    return std::nullopt;
  return fs::path(home) / ".cache" / "deskflow" / "clipboard";
#endif
#endif
}

AsSessionUser::AsSessionUser()
{
#if defined(DESKFLOW_OS_WIN)
  if (!runningAsSystem())
    return;
  HANDLE token = sessionUserToken();
  if (token == nullptr) {
    m_ok = false;
    return;
  }
  m_impersonating = ImpersonateLoggedOnUser(token) != FALSE;
  m_ok = m_impersonating;
  CloseHandle(token);
#endif
}

AsSessionUser::~AsSessionUser()
{
#if defined(DESKFLOW_OS_WIN)
  if (m_impersonating)
    RevertToSelf();
#endif
}

} // namespace deskflow::files
