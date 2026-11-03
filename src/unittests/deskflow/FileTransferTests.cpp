/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "FileTransferTests.h"

#include "arch/Utf8.h"
#include "deskflow/files/FileList.h"
#include "deskflow/files/FileTransfer.h"

#include <chrono>
#include <fstream>
#include <random>

using namespace deskflow::files;
namespace fs = std::filesystem;
using deskflow::pathToUtf8;
using deskflow::utf8Path;

namespace {

void writeFile(const fs::path &path, std::string_view content)
{
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file.write(content.data(), static_cast<std::streamsize>(content.size()));
}

std::string readFile(const fs::path &path)
{
  std::ifstream file(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(file), {});
}

std::string randomBytes(std::size_t size)
{
  std::mt19937 random(1234);
  std::string out(size, '\0');
  for (auto &c : out)
    c = static_cast<char>(random() & 0xFF);
  return out;
}

Entry file(std::string path, std::uint64_t size = 1)
{
  return Entry{std::move(path), false, size, 0};
}

Entry folder(std::string path)
{
  return Entry{std::move(path), true, 0, 0};
}

Selection collected(const std::vector<fs::path> &paths, const Limits &limits = {})
{
  auto result = collect(paths, limits);
  if (auto *error = std::get_if<std::string>(&result))
    qWarning("collect failed: %s", error->c_str());
  return std::get<Selection>(std::move(result));
}

std::unique_ptr<Writer> begun(const fs::path &root, const std::string &id, const FileList &list)
{
  auto result = Writer::begin(root, id, list);
  if (auto *error = std::get_if<std::string>(&result))
    qWarning("begin failed: %s", error->c_str());
  return std::get<std::unique_ptr<Writer>>(std::move(result));
}

//! Reads everything and writes it; the first error from either side
std::optional<std::string> transfer(Reader &reader, Writer &writer, std::size_t chunkSize)
{
  Chunk chunk;
  std::string error;
  while (true) {
    switch (reader.next(chunkSize, chunk, error)) {
    case Reader::Result::Done:
      return std::nullopt;
    case Reader::Result::Error:
      return "read: " + error;
    case Reader::Result::Chunk:
      if (auto why = writer.write(chunk))
        return "write: " + *why;
    }
  }
}

//! The chunks a selection becomes
std::vector<Chunk> chunksOf(Selection selection, std::size_t chunkSize)
{
  Reader reader(std::move(selection));
  std::vector<Chunk> chunks;
  Chunk chunk;
  std::string error;
  while (reader.next(chunkSize, chunk, error) == Reader::Result::Chunk)
    chunks.push_back(chunk);
  return chunks;
}

bool hasError(const std::optional<std::string> &result, std::string_view text)
{
  if (!result) {
    qWarning("expected an error containing \"%.*s\", got none", static_cast<int>(text.size()), text.data());
    return false;
  }
  if (result->find(text) == std::string::npos) {
    qWarning(
        "expected an error containing \"%.*s\", got \"%s\"", static_cast<int>(text.size()), text.data(), result->c_str()
    );
    return false;
  }
  return true;
}

template <typename T> std::optional<std::string> errorOf(const std::variant<T, std::string> &result)
{
  if (const auto *error = std::get_if<std::string>(&result))
    return *error;
  return std::nullopt;
}

bool emptyFolder(const fs::path &path)
{
  return fs::exists(path) && fs::is_empty(path);
}

} // namespace

void FileTransferTests::init()
{
  m_dir = new QTemporaryDir();
  QVERIFY(m_dir->isValid());
}

void FileTransferTests::cleanup()
{
  delete m_dir;
  m_dir = nullptr;
}

#define TMP fs::path(m_dir->path().toStdWString())

//
// FileList
//

void FileTransferTests::pathsAccepted()
{
  for (const auto *path :
       {"a.txt", "folder/b c.txt", "\xC3\xB1\x61nd\xC3\xBA/\xE6\x97\xA5\xE6\x9C\xAC.txt", "console.txt", "com10", "lpt",
        "a..b", ".hidden", "emoji \xF0\x9F\x98\x80.png", "nul_not.txt"}) {
    const auto why = invalidPath(path);
    QVERIFY2(!why, (std::string(path) + ": " + why.value_or("")).c_str());
  }
}

void FileTransferTests::pathsRejected()
{
  const std::string longName(256, 'x');
  const std::string longPath = std::string(600, 'a') + "/" + std::string(500, 'b');
  for (const std::string path :
       {std::string(),
        std::string("/etc/passwd"),
        std::string("../x"),
        std::string("a/../b"),
        std::string("a/./b"),
        std::string(".."),
        std::string("a//b"),
        std::string("a/"),
        std::string("C:/x"),
        std::string("a:stream"),
        std::string("a\\b"),
        std::string("NUL"),
        std::string("nul.txt"),
        std::string("Com1"),
        std::string("dir/lpt9.log"),
        std::string("com\xC2\xB9"),
        std::string("CONIN$"),
        std::string("a."),
        std::string("a "),
        std::string("x\x01y"),
        std::string("x\x7Fy"),
        std::string("a*b"),
        std::string("a?b"),
        std::string("a|b"),
        std::string("a<b"),
        std::string("a\"b"),
        std::string("\xC0\xAF"),
        std::string("\xED\xA0\x80"),
        std::string("\xE6\x97"),
        std::string("\xF4\x90\x80\x80"),
        std::string("\xFF"),
        longName,
        longPath,
        std::string("a\0b", 3)}) {
    QVERIFY2(invalidPath(path).has_value(), path.c_str());
  }
}

void FileTransferTests::listRules()
{
  QVERIFY(!invalidList(
      {{folder("photos"), file("photos/a.jpg"), folder("photos/2026"), file("photos/2026/b.jpg"), file("notes.txt")}}
  ));

  QVERIFY(hasError(invalidList({}), "no files"));
  QVERIFY(hasError(invalidList({{file("a.txt"), file("A.TXT")}}), "listed twice"));
  QVERIFY(hasError(invalidList({{folder("x"), file("X")}}), "listed twice"));
  QVERIFY(hasError(invalidList({{file("photos/a.jpg"), folder("photos")}}), "isn't listed before it"));
  QVERIFY(hasError(invalidList({{file("a"), file("a/b")}}), "inside a file"));
  QVERIFY(hasError(invalidList({{Entry{"f", true, 5, 0}}}), "a folder with a size"));
  QVERIFY(hasError(invalidList({{file("ok.txt"), file("../escape.txt")}}), "relative reference"));
}

void FileTransferTests::listLimits()
{
  Limits limits;
  limits.maxEntries = 2;
  QVERIFY(!invalidList({{file("a"), file("b")}}, limits));
  QVERIFY(hasError(invalidList({{file("a"), file("b"), file("c")}}, limits), "too many"));

  limits = {};
  limits.maxBytes = 100;
  QVERIFY(!invalidList({{file("a", 60), file("b", 40)}}, limits));
  QVERIFY(hasError(invalidList({{file("a", 60), file("b", 41)}}, limits), "larger than the limit"));
  // sizes that would wrap around a 64-bit total
  QVERIFY(invalidList({{file("a", ~0ull), file("b", 2)}}).has_value());

  limits = {};
  limits.maxDepth = 2;
  QVERIFY(!invalidList({{folder("a"), file("a/b")}}, limits));
  QVERIFY(hasError(invalidList({{folder("a"), folder("a/b"), file("a/b/c")}}, limits), "too deeply nested"));
}

void FileTransferTests::serializeRoundTrip()
{
  const FileList list{
      {Entry{"photos", true, 0, 1700000000}, Entry{"photos/\xE6\x97\xA5.jpg", false, 123456789012ull, -5},
       Entry{"empty.txt", false, 0, 0}}
  };
  const auto data = serialize(list);
  QVERIFY(data.starts_with("DFL1"));
  const auto back = deserialize(data);
  QVERIFY(back.has_value());
  QVERIFY(*back == list);
}

void FileTransferTests::deserializeRejectsMalformed()
{
  const auto data = serialize({{folder("a"), file("a/b.txt", 3)}});

  // every truncation, a trailing byte, another magic
  for (std::size_t size = 0; size < data.size(); ++size)
    QVERIFY2(!deserialize(std::string_view(data).substr(0, size)), std::to_string(size).c_str());
  QVERIFY(!deserialize(data + '\0'));
  QVERIFY(!deserialize("DFL2" + data.substr(4)));

  // a count far beyond the data must not allocate for it
  std::string huge = "DFL1";
  huge += std::string("\xFF\xFF\xFF\xFF", 4);
  QVERIFY(!deserialize(huge));

  // unknown flags
  auto flags = data;
  flags[8] = 2;
  QVERIFY(!deserialize(flags));

  // a path length past the end
  auto length = data;
  length[8 + 1 + 8 + 8] = '\x7F';
  QVERIFY(!deserialize(length));
}

void FileTransferTests::collectWalksFoldersInOrder()
{
  const auto root = TMP / "src";
  fs::create_directories(root / "docs" / "empty");
  fs::create_directories(root / "docs" / "b");
  writeFile(root / "docs" / "z.txt", "zz");
  writeFile(root / "docs" / "b" / "inner.bin", "123");
  writeFile(root / "top.txt", "hello");
  writeFile(root / utf8Path("\xC3\xB1o\xC3\xB1o.txt"), "x");

  const auto selection = collected({root / "top.txt", root / "docs", root / utf8Path("\xC3\xB1o\xC3\xB1o.txt")});
  std::vector<std::string> paths;
  for (const auto &e : selection.list.entries)
    paths.push_back(e.path);
  const std::vector<std::string> expected = {
      "top.txt", "docs", "docs/b", "docs/b/inner.bin", "docs/empty", "docs/z.txt", "\xC3\xB1o\xC3\xB1o.txt"
  };
  QCOMPARE(paths, expected);

  QCOMPARE(selection.sources.size(), selection.list.entries.size());
  QVERIFY(selection.sources[1].empty()); // folders have no source
  QCOMPARE(selection.sources[3], root / "docs" / "b" / "inner.bin");
  QCOMPARE(selection.list.entries[0].size, 5u);
  QCOMPARE(selection.list.totalBytes(), 11u);
  QCOMPARE(selection.list.fileCount(), 4u);
  QCOMPARE(selection.list.roots(), (std::vector<std::string>{"top.txt", "docs", "\xC3\xB1o\xC3\xB1o.txt"}));

  const auto modified = toUnixSeconds(fs::last_write_time(root / "top.txt"));
  QCOMPARE(selection.list.entries[0].modified, modified);
}

void FileTransferTests::collectSkipsLinks()
{
  const auto root = TMP / "links";
  fs::create_directories(root / "real");
  writeFile(root / "real" / "f.txt", "data");
  std::error_code ec;
  fs::create_directory_symlink(root, root / "real" / "loop", ec); // would recurse forever if followed
  if (ec)
    QSKIP("can't create symbolic links here");
  fs::create_symlink(root / "real" / "f.txt", root / "real" / "link.txt", ec);

  const auto selection = collected({root / "real"});
  std::vector<std::string> paths;
  for (const auto &e : selection.list.entries)
    paths.push_back(e.path);
  QCOMPARE(paths, (std::vector<std::string>{"real", "real/f.txt"}));

  // QTemporaryDir can't remove a loop; remove_all deletes the links, not their targets
  fs::remove_all(root / "real", ec);
  QVERIFY(!ec);
  QVERIFY(fs::exists(root));
}

void FileTransferTests::collectEnforcesLimits()
{
  const auto root = TMP / "limits";
  fs::create_directories(root);
  writeFile(root / "a.bin", std::string(100, 'a'));
  writeFile(root / "b.bin", std::string(100, 'b'));

  Limits limits;
  limits.maxBytes = 150;
  QVERIFY(hasError(errorOf(collect({root}, limits)), "larger than the limit"));

  limits = {};
  limits.maxEntries = 2;
  QVERIFY(hasError(errorOf(collect({root}, limits)), "more than 2"));

  limits = {};
  QVERIFY(!errorOf(collect({root}, limits)));
  QVERIFY(errorOf(collect({root / "missing.txt"}, limits)).has_value());
}

void FileTransferTests::collectRefusesADrive()
{
  QVERIFY(hasError(errorOf(collect({TMP.root_path()})), "whole drive"));
}

//
// Reader and Writer
//

void FileTransferTests::sha256()
{
  Sha256 empty;
  QCOMPARE(toHex(empty.finish()), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

  Sha256 pieces;
  pieces.update("a");
  pieces.update("");
  pieces.update("bc");
  QCOMPARE(toHex(pieces.finish()), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

void FileTransferTests::roundTrip()
{
  const auto src = TMP / "src";
  const auto big = randomBytes(200 * 1024 + 17);
  fs::create_directories(src / "photos" / "empty folder");
  writeFile(src / "photos" / "big.bin", big);
  writeFile(src / "photos" / "empty.txt", "");
  writeFile(src / utf8Path("\xE6\x97\xA5\xE6\x9C\xAC.txt"), "unicode name");
  const auto oldTime = fromUnixSeconds(1600000000);
  fs::last_write_time(src / "photos" / "big.bin", oldTime);

  auto selection = collected({src / "photos", src / utf8Path("\xE6\x97\xA5\xE6\x9C\xAC.txt")});
  const auto list = selection.list;
  const auto cache = TMP / "cache";
  auto writer = begun(cache, "0123456789abcdef", list);
  Reader reader(std::move(selection));

  const auto result = transfer(reader, *writer, 4096);
  QVERIFY2(!result, result.value_or("").c_str());
  QVERIFY(writer->complete());
  QCOMPARE(writer->receivedBytes(), list.totalBytes());

  const auto committed = writer->commit();
  QVERIFY(!errorOf(committed));
  const auto final = cache / "0123456789abcdef";
  const auto roots = std::get<std::vector<fs::path>>(committed);
  QCOMPARE(roots, (std::vector<fs::path>{final / "photos", final / utf8Path("\xE6\x97\xA5\xE6\x9C\xAC.txt")}));

  QCOMPARE(readFile(final / "photos" / "big.bin"), big);
  QCOMPARE(readFile(final / "photos" / "empty.txt"), std::string());
  QCOMPARE(readFile(final / utf8Path("\xE6\x97\xA5\xE6\x9C\xAC.txt")), std::string("unicode name"));
  QVERIFY(emptyFolder(final / "photos" / "empty folder"));
  QCOMPARE(toUnixSeconds(fs::last_write_time(final / "photos" / "big.bin")), 1600000000);
  QVERIFY(!fs::exists(cache / "0123456789abcdef.partial"));

  // the writer is done: destroying it keeps the files
  writer.reset();
  QVERIFY(fs::exists(final / "photos" / "big.bin"));
}

void FileTransferTests::roundTripInOneChunk()
{
  const auto src = TMP / "one";
  fs::create_directories(src);
  writeFile(src / "a.txt", "alpha");
  writeFile(src / "b.txt", "beta");

  auto selection = collected({src / "a.txt", src / "b.txt"});
  const auto chunks = chunksOf(selection, 1 << 20);
  QCOMPARE(chunks.size(), 2u); // one per file, never two files in a chunk
  QVERIFY(chunks[0].last);
  QCOMPARE(chunks[1].data, std::string("beta"));

  auto writer = begun(TMP / "cache", "ab", selection.list);
  Reader reader(std::move(selection));
  QVERIFY(!transfer(reader, *writer, 1 << 20));
  QVERIFY(!errorOf(writer->commit()));
}

void FileTransferTests::writerRejectsDamage()
{
  const auto src = TMP / "damage";
  fs::create_directories(src);
  writeFile(src / "a.txt", "the original content");
  const auto selection = collected({src / "a.txt"});
  auto chunks = chunksOf(selection, 8);
  QCOMPARE(chunks.size(), 3u);

  // a flipped byte
  chunks[1].data[0] ^= 1;
  const auto cache = TMP / "cache";
  auto writer = begun(cache, "dead", selection.list);
  QVERIFY(!writer->write(chunks[0]));
  QVERIFY(!writer->write(chunks[1]));
  QVERIFY(hasError(writer->write(chunks[2]), "damaged"));
  QVERIFY(!fs::exists(cache / "dead.partial")); // nothing left behind
  QVERIFY(!writer->complete());
  QVERIFY(hasError(errorOf(writer->commit()), "damaged"));
  QVERIFY(!fs::exists(cache / "dead"));

  // once failed, it stays failed
  QVERIFY(hasError(writer->write(chunks[0]), "damaged"));
}

void FileTransferTests::writerRejectsDisorder()
{
  const auto src = TMP / "order";
  fs::create_directories(src);
  writeFile(src / "a.txt", "aaaaaaaaaa");
  writeFile(src / "b.txt", "bbbbbbbbbb");
  const auto selection = collected({src / "a.txt", src / "b.txt"});
  const auto chunks = chunksOf(selection, 4);
  QCOMPARE(chunks.size(), 6u);
  const auto cache = TMP / "cache";

  // skipping a chunk
  auto writer = begun(cache, "01", selection.list);
  QVERIFY(!writer->write(chunks[0]));
  QVERIFY(hasError(writer->write(chunks[2]), "out of order"));

  // the next file too early
  writer = begun(cache, "02", selection.list);
  QVERIFY(hasError(writer->write(chunks[3]), "wrong file"));

  // more than the file's size
  writer = begun(cache, "03", selection.list);
  auto tooLong = chunks[0];
  tooLong.data = std::string(11, 'a');
  QVERIFY(hasError(writer->write(tooLong), "more data than its size"));

  // a chunk after the end
  writer = begun(cache, "04", selection.list);
  for (const auto &chunk : chunks)
    QVERIFY(!writer->write(chunk));
  QVERIFY(writer->complete());
  QVERIFY(hasError(writer->write(chunks[5]), "more data than the list announced"));

  for (const auto *id : {"01", "02", "03", "04"}) {
    QVERIFY(!fs::exists(cache / (std::string(id) + ".partial")));
    QVERIFY(!fs::exists(cache / id));
  }
}

void FileTransferTests::writerRejectsShortFile()
{
  const auto src = TMP / "short";
  fs::create_directories(src);
  writeFile(src / "a.txt", "0123456789");
  const auto selection = collected({src / "a.txt"});
  auto chunks = chunksOf(selection, 4);

  // the sender says it's the end too early
  auto writer = begun(TMP / "cache", "05", selection.list);
  auto early = chunks[0];
  early.last = true;
  QVERIFY(hasError(writer->write(early), "shorter than announced"));

  // the sender stops: not complete, can't be committed
  writer = begun(TMP / "cache", "06", selection.list);
  QVERIFY(!writer->write(chunks[0]));
  QVERIFY(!writer->complete());
  QVERIFY(hasError(errorOf(writer->commit()), "incomplete"));
}

void FileTransferTests::writerRejectsBadIdAndList()
{
  const FileList good{{file("a.txt")}};
  const auto cache = TMP / "cache";
  for (const auto *id : {"", "..", "../x", "ABC", "a/b", "abc.partial"})
    QVERIFY2(hasError(errorOf(Writer::begin(cache, id, good)), "invalid transfer id"), id);
  QVERIFY(errorOf(Writer::begin(cache, std::string(65, 'a'), good)).has_value());

  QVERIFY(hasError(errorOf(Writer::begin(cache, "0a", {{file("../a.txt")}})), "relative reference"));
  QVERIFY(hasError(errorOf(Writer::begin(cache, "0a", {{file("a/b.txt")}})), "isn't listed before it"));
  QVERIFY(!fs::exists(cache / "0a.partial"));

  // an id that is already there
  auto writer = begun(cache, "0b", good);
  QVERIFY(hasError(errorOf(Writer::begin(cache, "0b", good)), "already exists"));
}

void FileTransferTests::writerNeedsDiskSpace()
{
  Limits limits;
  limits.maxBytes = ~0ull;
  const FileList huge{{file("huge.bin", 1ull << 60)}};
  QVERIFY(hasError(errorOf(Writer::begin(TMP / "cache", "0c", huge, limits)), "not enough free disk space"));
  QVERIFY(!fs::exists(TMP / "cache" / "0c.partial"));
}

void FileTransferTests::writerDeletesUnfinishedCopy()
{
  const auto src = TMP / "unfinished";
  fs::create_directories(src / "sub");
  writeFile(src / "sub" / "a.txt", "0123456789");
  const auto selection = collected({src / "sub"});
  const auto chunks = chunksOf(selection, 4);

  const auto cache = TMP / "cache";
  auto writer = begun(cache, "0d", selection.list);
  QVERIFY(!writer->write(chunks[0]));
  QVERIFY(fs::exists(cache / "0d.partial" / "sub" / "a.txt"));
  writer.reset(); // a disconnect, a newer copy
  QVERIFY(!fs::exists(cache / "0d.partial"));
  QVERIFY(!fs::exists(cache / "0d"));
}

void FileTransferTests::writerRefusesCaseClash()
{
#if defined(_WIN32)
  // Ñ and ñ: the list check only folds ASCII, the file system folds more
  const auto cache = TMP / "cache";
  QVERIFY(hasError(errorOf(Writer::begin(cache, "0e", {{folder("\xC3\x91"), folder("\xC3\xB1")}})), "listed twice"));
  QVERIFY(!fs::exists(cache / "0e.partial"));

  Chunk chunk;
  chunk.last = true;
  chunk.digest = Sha256().finish();
  auto writer = begun(cache, "0f", {{file("\xC3\x89.txt", 0), file("\xC3\xA9.txt", 0)}});
  QVERIFY(!writer->write(chunk));
  chunk.index = 1;
  QVERIFY(hasError(writer->write(chunk), "listed twice"));
#else
  QSKIP("case-insensitive file systems only");
#endif
}

void FileTransferTests::writerRefusesLongPaths()
{
#if defined(_WIN32)
  const std::string a(200, 'a');
  const FileList list{{folder(a), file(a + "/" + std::string(100, 'b'))}};
  QVERIFY(!invalidList(list)); // fine as a list...
  QVERIFY(hasError(errorOf(Writer::begin(TMP / "cache", "10", list)), "too long on this computer")); // ...not here
#else
  QSKIP("Windows only");
#endif
}

void FileTransferTests::readerDetectsChangeBeforeReading()
{
  const auto src = TMP / "change";
  fs::create_directories(src);
  writeFile(src / "a.txt", "before");
  auto selection = collected({src / "a.txt"});

  writeFile(src / "a.txt", "after, longer");
  Reader reader(std::move(selection));
  Chunk chunk;
  std::string error;
  QCOMPARE(reader.next(4, chunk, error), Reader::Result::Error);
  QVERIFY(error.find("changed") != std::string::npos);
  QCOMPARE(reader.next(4, chunk, error), Reader::Result::Error); // and stays so

  // the same size, a different time
  writeFile(src / "b.txt", "same");
  auto other = collected({src / "b.txt"});
  writeFile(src / "b.txt", "SAME");
  fs::last_write_time(src / "b.txt", fs::last_write_time(src / "b.txt") + std::chrono::seconds(5));
  Reader reader2(std::move(other));
  QCOMPARE(reader2.next(4, chunk, error), Reader::Result::Error);
}

void FileTransferTests::readerDetectsChangeWhileReading()
{
  const auto src = TMP / "during";
  fs::create_directories(src);
  writeFile(src / "a.txt", "0123456789");
  auto selection = collected({src / "a.txt"});
  Reader reader(std::move(selection));
  Chunk chunk;
  std::string error;
  QCOMPARE(reader.next(4, chunk, error), Reader::Result::Chunk);

  // grows while it is open
  {
    std::ofstream append(src / "a.txt", std::ios::binary | std::ios::app);
    append << "more";
  }
  Reader::Result result = Reader::Result::Chunk;
  while (result == Reader::Result::Chunk)
    result = reader.next(4, chunk, error);
  QCOMPARE(result, Reader::Result::Error);
  QVERIFY2(error.find("changed while it was being sent") != std::string::npos, error.c_str());
}

//
// Cache
//

void FileTransferTests::newIds()
{
  const auto a = Cache::newId();
  const auto b = Cache::newId();
  QCOMPARE(a.size(), 16u);
  QVERIFY(validId(a));
  QVERIFY(a != b);
}

void FileTransferTests::cleanupKeepsNewestAndOnlyItsOwn()
{
  const auto root = TMP / "cache";
  const auto now = fs::file_time_type::clock::now();
  const std::vector<std::string> ids = {"1000000000000000", "2000000000000000", "3000000000000000", "4000000000000000"};
  for (std::size_t i = 0; i < ids.size(); ++i) {
    fs::create_directories(root / ids[i] / "inside");
    writeFile(root / ids[i] / "f.txt", "x");
    fs::last_write_time(root / ids[i], now - std::chrono::hours(10 - i));
  }
  fs::create_directories(root / "5000000000000000.partial" / "half");
  fs::create_directories(root / "not-ours");
  writeFile(root / "readme.txt", "keep");
  fs::create_directories(root / "abc"); // hex, but not an id's length

  Cache(root).cleanup(2);
  QVERIFY(!fs::exists(root / ids[0]));
  QVERIFY(!fs::exists(root / ids[1]));
  QVERIFY(fs::exists(root / ids[2] / "f.txt"));
  QVERIFY(fs::exists(root / ids[3] / "f.txt"));
  QVERIFY(!fs::exists(root / "5000000000000000.partial"));
  QVERIFY(fs::exists(root / "not-ours"));
  QVERIFY(fs::exists(root / "readme.txt"));
  QVERIFY(fs::exists(root / "abc"));

  // a missing folder is nothing to clean
  Cache(TMP / "missing").cleanup();
}

void FileTransferTests::userCacheRoot()
{
  const auto root = deskflow::files::userCacheRoot();
  QVERIFY(root.has_value());
  QVERIFY(root->is_absolute());
  const auto text = pathToUtf8(*root);
#if defined(_WIN32)
  QVERIFY2(text.ends_with("Deskflow\\Clipboard"), text.c_str());
#else
  QVERIFY2(text.ends_with("lipboard"), text.c_str());
#endif
  AsSessionUser asUser; // not SYSTEM here: does nothing
  QVERIFY(asUser.ok());
}

QTEST_MAIN(FileTransferTests)
