/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/files/FileList.h"

#include "arch/Utf8.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <set>

namespace deskflow::files {

namespace fs = std::filesystem;

namespace {

constexpr std::string_view kMagic = "DFL1";
constexpr std::size_t kMaxComponentBytes = 255;

bool validUtf8(std::string_view s)
{
  std::size_t i = 0;
  while (i < s.size()) {
    const auto c = static_cast<unsigned char>(s[i]);
    std::size_t extra = 0;
    std::uint32_t cp = 0;
    if (c < 0x80) {
      ++i;
      continue;
    } else if ((c & 0xE0) == 0xC0) {
      extra = 1;
      cp = c & 0x1F;
    } else if ((c & 0xF0) == 0xE0) {
      extra = 2;
      cp = c & 0x0F;
    } else if ((c & 0xF8) == 0xF0) {
      extra = 3;
      cp = c & 0x07;
    } else {
      return false;
    }
    if (i + extra >= s.size())
      return false; // cut short
    for (std::size_t j = 1; j <= extra; ++j) {
      const auto cc = static_cast<unsigned char>(s[i + j]);
      if ((cc & 0xC0) != 0x80)
        return false;
      cp = (cp << 6) | (cc & 0x3F);
    }
    // overlong forms, surrogates, past U+10FFFF
    if ((extra == 1 && cp < 0x80) || (extra == 2 && cp < 0x800) || (extra == 3 && cp < 0x10000) || cp > 0x10FFFF ||
        (cp >= 0xD800 && cp <= 0xDFFF))
      return false;
    i += extra + 1;
  }
  return true;
}

std::string asciiLower(std::string_view s)
{
  std::string out(s);
  for (auto &c : out) {
    if (c >= 'A' && c <= 'Z')
      c = static_cast<char>(c - 'A' + 'a');
  }
  return out;
}

//! CON, PRN, AUX, NUL, COM1-9, LPT1-9 (also with superscript digits), alone or with an extension
bool isDeviceName(std::string_view component)
{
  const auto base = asciiLower(component.substr(0, component.find('.')));
  static const std::set<std::string, std::less<>> names = {"con", "prn", "aux", "nul", "conin$", "conout$"};
  if (names.contains(base))
    return true;
  if ((base.starts_with("com") || base.starts_with("lpt"))) {
    const auto rest = std::string_view(base).substr(3);
    if (rest.size() == 1 && rest[0] >= '0' && rest[0] <= '9')
      return true;
    if (rest == "\xC2\xB9" || rest == "\xC2\xB2" || rest == "\xC2\xB3") // ¹ ² ³
      return true;
  }
  return false;
}

void putU32(std::string &out, std::uint32_t v)
{
  for (int i = 0; i < 4; ++i)
    out += static_cast<char>((v >> (8 * i)) & 0xFF);
}

void putU64(std::string &out, std::uint64_t v)
{
  for (int i = 0; i < 8; ++i)
    out += static_cast<char>((v >> (8 * i)) & 0xFF);
}

template <typename T> bool get(std::string_view &in, T &v)
{
  if (in.size() < sizeof(T))
    return false;
  std::uint64_t value = 0;
  for (std::size_t i = 0; i < sizeof(T); ++i)
    value |= static_cast<std::uint64_t>(static_cast<unsigned char>(in[i])) << (8 * i);
  v = static_cast<T>(value);
  in.remove_prefix(sizeof(T));
  return true;
}

std::size_t depthOf(std::string_view path)
{
  return static_cast<std::size_t>(std::count(path.begin(), path.end(), '/')) + 1;
}

} // namespace

std::uint64_t FileList::totalBytes() const
{
  std::uint64_t total = 0;
  for (const auto &e : entries)
    total += e.size;
  return total;
}

std::size_t FileList::fileCount() const
{
  return static_cast<std::size_t>(std::count_if(entries.begin(), entries.end(), [](const auto &e) {
    return !e.directory;
  }));
}

std::vector<std::string> FileList::roots() const
{
  std::vector<std::string> result;
  for (const auto &e : entries) {
    if (e.path.find('/') == std::string::npos)
      result.push_back(e.path);
  }
  return result;
}

std::optional<std::string> invalidPath(std::string_view path, const Limits &limits)
{
  if (path.empty())
    return "empty path";
  if (path.size() > limits.maxPathBytes)
    return "path too long";
  if (!validUtf8(path))
    return "path is not valid UTF-8";
  for (const char c : path) {
    const auto u = static_cast<unsigned char>(c);
    if (u < 0x20 || u == 0x7F)
      return "control character in path";
    if (std::strchr("\\:<>\"|?*", c) != nullptr)
      return std::string("character not allowed in a file name: ") + c;
  }
  if (path.front() == '/')
    return "absolute path";

  std::size_t start = 0;
  while (start <= path.size()) {
    const auto slash = path.find('/', start);
    const auto component = path.substr(start, slash == std::string_view::npos ? std::string_view::npos : slash - start);
    if (component.empty())
      return "empty name in path";
    if (component == "." || component == "..")
      return "relative reference in path";
    if (component.size() > kMaxComponentBytes)
      return "name too long";
    if (component.back() == '.' || component.back() == ' ')
      return "name ends with a dot or a space";
    if (isDeviceName(component))
      return "reserved device name: " + std::string(component);
    if (slash == std::string_view::npos)
      break;
    start = slash + 1;
  }
  return std::nullopt;
}

std::optional<std::string> invalidList(const FileList &list, const Limits &limits)
{
  if (list.entries.empty())
    return "no files";
  if (list.entries.size() > limits.maxEntries)
    return "too many files and folders";

  std::set<std::string, std::less<>> files;
  std::set<std::string, std::less<>> folders;
  std::uint64_t total = 0;
  for (const auto &e : list.entries) {
    if (auto why = invalidPath(e.path, limits))
      return e.path + ": " + *why;
    if (depthOf(e.path) > limits.maxDepth)
      return e.path + ": too deeply nested";
    if (e.directory && e.size != 0)
      return e.path + ": a folder with a size";

    const auto key = asciiLower(e.path);
    if (files.contains(key) || folders.contains(key))
      return e.path + ": listed twice";

    // its folder must come first, and must be a folder
    if (const auto slash = key.rfind('/'); slash != std::string::npos) {
      const auto parent = key.substr(0, slash);
      if (files.contains(parent))
        return e.path + ": inside a file";
      if (!folders.contains(parent))
        return e.path + ": its folder isn't listed before it";
    }
    (e.directory ? folders : files).insert(key);

    total += e.size;
    if (total > limits.maxBytes)
      return "the copy is larger than the limit";
  }
  return std::nullopt;
}

std::string serialize(const FileList &list)
{
  std::string out(kMagic);
  putU32(out, static_cast<std::uint32_t>(list.entries.size()));
  for (const auto &e : list.entries) {
    out += static_cast<char>(e.directory ? 1 : 0);
    putU64(out, e.size);
    putU64(out, static_cast<std::uint64_t>(e.modified));
    putU32(out, static_cast<std::uint32_t>(e.path.size()));
    out += e.path;
  }
  return out;
}

std::optional<FileList> deserialize(std::string_view in)
{
  if (!in.starts_with(kMagic))
    return std::nullopt;
  in.remove_prefix(kMagic.size());

  std::uint32_t count = 0;
  if (!get(in, count))
    return std::nullopt;
  // each entry takes at least 21 bytes: never trust a count the data can't hold
  if (count > in.size() / 21)
    return std::nullopt;

  FileList list;
  list.entries.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    std::uint8_t flags = 0;
    std::uint64_t modified = 0;
    std::uint32_t length = 0;
    Entry e;
    if (!get(in, flags) || flags > 1 || !get(in, e.size) || !get(in, modified) || !get(in, length) ||
        length > in.size())
      return std::nullopt;
    e.directory = flags == 1;
    e.modified = static_cast<std::int64_t>(modified);
    e.path = std::string(in.substr(0, length));
    in.remove_prefix(length);
    list.entries.push_back(std::move(e));
  }
  if (!in.empty())
    return std::nullopt;
  return list;
}

std::int64_t toUnixSeconds(fs::file_time_type time)
{
  const auto system = std::chrono::clock_cast<std::chrono::system_clock>(time);
  return std::chrono::duration_cast<std::chrono::seconds>(system.time_since_epoch()).count();
}

fs::file_time_type fromUnixSeconds(std::int64_t seconds)
{
  const std::chrono::sys_seconds system{std::chrono::seconds{seconds}};
  return std::chrono::clock_cast<fs::file_time_type::clock>(system);
}

namespace {

struct Collector
{
  const Limits &limits;
  Selection selection;
  std::uint64_t total = 0;

  //! The reason it failed, or nothing
  std::optional<std::string> add(const fs::path &source, const std::string &relative, std::size_t depth)
  {
    std::error_code ec;
    const auto status = fs::symlink_status(source, ec);
    if (ec)
      return pathToUtf8(source) + ": " + ec.message();
    // never follow links: they may point anywhere, even back up the tree
    if (fs::is_symlink(status))
      return std::nullopt;
#if defined(_MSC_VER)
    if (status.type() == fs::file_type::junction)
      return std::nullopt;
#endif

    if (depth > limits.maxDepth)
      return relative + ": too deeply nested";
    if (selection.list.entries.size() >= limits.maxEntries)
      return "more than " + std::to_string(limits.maxEntries) + " files and folders";

    Entry entry;
    entry.path = relative;
    entry.modified = toUnixSeconds(fs::last_write_time(source, ec));
    if (ec)
      return pathToUtf8(source) + ": " + ec.message();

    if (fs::is_directory(status)) {
      entry.directory = true;
      selection.list.entries.push_back(entry);
      selection.sources.emplace_back();

      // sorted, so the same folder always gives the same list
      std::vector<fs::path> children;
      for (fs::directory_iterator it(source, ec), end; !ec && it != end; it.increment(ec))
        children.push_back(it->path());
      if (ec)
        return pathToUtf8(source) + ": " + ec.message();
      std::sort(children.begin(), children.end());
      for (const auto &child : children) {
        if (auto why = add(child, relative + "/" + pathToUtf8(child.filename()), depth + 1))
          return why;
      }
      return std::nullopt;
    }

    if (!fs::is_regular_file(status))
      return std::nullopt; // devices, sockets: nothing to copy

    entry.size = fs::file_size(source, ec);
    if (ec)
      return pathToUtf8(source) + ": " + ec.message();
    total += entry.size;
    if (total > limits.maxBytes)
      return "the copy is larger than the limit of " + std::to_string(limits.maxBytes / (1024 * 1024)) + " MB";
    selection.list.entries.push_back(entry);
    selection.sources.push_back(source);
    return std::nullopt;
  }
};

} // namespace

std::variant<Selection, std::string> collect(const std::vector<fs::path> &selected, const Limits &limits)
{
  Collector collector{limits, {}, 0};
  for (const auto &path : selected) {
    const auto name = pathToUtf8(path.filename());
    if (name.empty())
      return pathToUtf8(path) + ": a whole drive can't be copied";
    if (auto why = collector.add(path, name, 1))
      return *why;
  }
  if (auto why = invalidList(collector.selection.list, limits))
    return *why;
  return std::move(collector.selection);
}

} // namespace deskflow::files
