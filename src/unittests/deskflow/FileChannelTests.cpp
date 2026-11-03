/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "FileChannelTests.h"

#include "deskflow/ProtocolTypes.h"
#include "deskflow/ProtocolUtil.h"
#include "deskflow/files/FileChannel.h"
#include "deskflow/files/FileHub.h"
#include "deskflow/files/FileReport.h"
#include "io/IStream.h"

#include <deque>
#include <fstream>
#include <random>
#include <set>
#include <utility>

using namespace deskflow::files;
namespace fs = std::filesystem;

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
  std::mt19937 random(42);
  std::string out(size, '\0');
  for (auto &c : out)
    c = static_cast<char>(random() & 0xFF);
  return out;
}

Selection selectionOf(const std::vector<fs::path> &paths)
{
  auto result = collect(paths);
  if (const auto *error = std::get_if<std::string>(&result))
    qWarning("collect: %s", error->c_str());
  return std::get<Selection>(std::move(result));
}

//! A message on its way
struct Message
{
  enum Kind
  {
    List,
    Chunk,
    Done,
    Ack,
    Cancel
  } kind;
  std::string id;
  std::uint32_t index = 0;
  std::string data;
};

//! What one side's owner was told
struct Seen
{
  std::vector<std::string> sent;
  std::vector<std::pair<std::string, std::string>> sendFailed;
  std::vector<std::pair<std::string, std::vector<fs::path>>> received;
  std::vector<std::pair<std::string, std::string>> receiveFailed;
  std::vector<std::uint64_t> progress;
};

//! Two computers' channels connected back to back, messages queued between them
struct Link
{
  explicit Link(const fs::path &cacheA, const fs::path &cacheB)
  {
    a = make(toB, seenA, cacheA);
    b = make(toA, seenB, cacheB);
  }

  std::unique_ptr<FileChannel> make(std::deque<Message> &out, Seen &seen, fs::path cache)
  {
    FileChannel::Wire wire;
    wire.list = [&out](const auto &id, const auto &list) { out.push_back({Message::List, id, 0, list}); };
    wire.chunk = [&out](const auto &id, auto index, const auto &data) {
      out.push_back({Message::Chunk, id, index, data});
    };
    wire.done = [&out](const auto &id, auto index, const auto &digest) {
      out.push_back({Message::Done, id, index, digest});
    };
    wire.ack = [&out](const auto &id) { out.push_back({Message::Ack, id, 0, {}}); };
    wire.cancel = [&out](const auto &id, const auto &reason) { out.push_back({Message::Cancel, id, 0, reason}); };

    FileChannel::Events events;
    events.accept = [this](const auto &, const auto &) -> std::optional<std::string> { return refusal; };
    events.sent = [&seen](const auto &id) { seen.sent.push_back(id); };
    events.sendFailed = [&seen](const auto &id, const auto &why) { seen.sendFailed.emplace_back(id, why); };
    events.received = [&seen](const auto &id, const auto &roots) { seen.received.emplace_back(id, roots); };
    events.receiveFailed = [&seen](const auto &id, const auto &why) { seen.receiveFailed.emplace_back(id, why); };
    events.progress = [&seen](const auto &, bool, const FileList &, std::uint64_t done) {
      seen.progress.push_back(done);
    };

    FileChannel::Options options;
    options.cacheRoot = [this, cache]() -> std::optional<fs::path> {
      if (noUser)
        return std::nullopt;
      return cache;
    };
    return std::make_unique<FileChannel>(std::move(wire), std::move(events), std::move(options));
  }

  static void deliver(FileChannel &to, const Message &m)
  {
    switch (m.kind) {
    case Message::List:
      to.onList(m.id, m.data);
      break;
    case Message::Chunk:
      to.onChunk(m.id, m.index, m.data);
      break;
    case Message::Done:
      to.onDone(m.id, m.index, m.data);
      break;
    case Message::Ack:
      to.onAck(m.id);
      break;
    case Message::Cancel:
      to.onCancel(m.id, m.data);
      break;
    }
  }

  //! Delivers one message from A to B; false if none is waiting
  bool stepAtoB()
  {
    if (toB.empty())
      return false;
    auto m = std::move(toB.front());
    toB.pop_front();
    if (tamper)
      tamper(m);
    deliver(*b, m);
    return true;
  }

  bool stepBtoA()
  {
    if (toA.empty())
      return false;
    auto m = std::move(toA.front());
    toA.pop_front();
    deliver(*a, m);
    return true;
  }

  //! Everything in both directions, watching how much of A's data waits for B
  void run()
  {
    while (true) {
      maxUnacknowledged = (std::max)(maxUnacknowledged, unacknowledged());
      if (stepAtoB())
        continue;
      if (!stepBtoA())
        break;
    }
  }

  //! A's chunks and file ends that B hasn't acknowledged yet (queued or not)
  std::size_t unacknowledged() const
  {
    std::size_t data = 0;
    for (const auto &m : toB)
      data += (m.kind == Message::Chunk || m.kind == Message::Done) ? 1 : 0;
    std::size_t acks = 0;
    for (const auto &m : toA)
      acks += m.kind == Message::Ack ? 1 : 0;
    return data + acks;
  }

  std::deque<Message> toA;
  std::deque<Message> toB;
  Seen seenA;
  Seen seenB;
  std::optional<std::string> refusal;
  bool noUser = false;
  std::function<void(Message &)> tamper;
  std::size_t maxUnacknowledged = 0;
  std::unique_ptr<FileChannel> a;
  std::unique_ptr<FileChannel> b;
};

bool contains(const std::string &text, std::string_view part)
{
  return text.find(part) != std::string::npos;
}

//! Folders under the cache: received copies and partial ones
std::vector<std::string> cacheEntries(const fs::path &cache)
{
  std::vector<std::string> names;
  std::error_code ec;
  for (fs::directory_iterator it(cache, ec), end; !ec && it != end; it.increment(ec))
    names.push_back(it->path().filename().string());
  std::sort(names.begin(), names.end());
  return names;
}

} // namespace

#define TMP fs::path(m_dir->path().toStdWString())

void FileChannelTests::initTestCase()
{
  m_log.setFilter(LogLevel::Level::Warning);
}

void FileChannelTests::init()
{
  m_dir = new QTemporaryDir();
  QVERIFY(m_dir->isValid());
}

void FileChannelTests::cleanup()
{
  delete m_dir;
  m_dir = nullptr;
}

//
// FileChannel
//

void FileChannelTests::copyArrivesWhole()
{
  const auto src = TMP / "src";
  const auto big = randomBytes(FileChannel::kChunkBytes * 20 + 123);
  fs::create_directories(src / "docs" / "empty");
  writeFile(src / "docs" / "big.bin", big);
  writeFile(src / "docs" / "zero.txt", "");
  writeFile(src / "note.txt", "hello");

  Link link(TMP / "cacheA", TMP / "cacheB");
  link.a->send("abc1", selectionOf({src / "docs", src / "note.txt"}));
  QCOMPARE(link.a->sendingId(), std::optional<std::string>("abc1"));
  link.run();

  QCOMPARE(link.seenA.sent, std::vector<std::string>{"abc1"});
  QVERIFY(link.seenA.sendFailed.empty());
  QCOMPARE(link.seenB.received.size(), 1u);
  QVERIFY(link.seenB.receiveFailed.empty());
  QVERIFY(!link.a->sendingId());
  QVERIFY(!link.b->receivingId());

  const auto &[id, roots] = link.seenB.received[0];
  QCOMPARE(id, std::string("abc1"));
  const auto copy = TMP / "cacheB" / "abc1";
  QCOMPARE(roots, (std::vector<fs::path>{copy / "docs", copy / "note.txt"}));
  QCOMPARE(readFile(copy / "docs" / "big.bin"), big);
  QCOMPARE(readFile(copy / "docs" / "zero.txt"), std::string());
  QCOMPARE(readFile(copy / "note.txt"), std::string("hello"));
  QVERIFY(fs::is_directory(copy / "docs" / "empty"));
  QVERIFY(link.maxUnacknowledged <= FileChannel::kWindow);
}

void FileChannelTests::neverMoreThanTheWindowInFlight()
{
  const auto src = TMP / "src";
  fs::create_directories(src);
  writeFile(src / "big.bin", randomBytes(FileChannel::kChunkBytes * 30));

  Link link(TMP / "cacheA", TMP / "cacheB");
  link.a->send("ab", selectionOf({src / "big.bin"}));

  // nothing acknowledged: A stops after the window, the list aside
  QCOMPARE(link.toB.size(), 1 + FileChannel::kWindow);
  while (link.stepAtoB()) {
  }
  QCOMPARE(link.toA.size(), FileChannel::kWindow); // B's acknowledgments, held back
  QVERIFY(link.toB.empty());

  // each acknowledgment lets exactly one more out
  QVERIFY(link.stepBtoA());
  QCOMPARE(link.toB.size(), 1u);

  link.run();
  QCOMPARE(link.seenB.received.size(), 1u);
  QVERIFY(link.maxUnacknowledged <= FileChannel::kWindow);
}

void FileChannelTests::progressGoesUpToTheTotal()
{
  const auto src = TMP / "src";
  fs::create_directories(src);
  const auto size = FileChannel::kChunkBytes * 5 + 10;
  writeFile(src / "a.bin", randomBytes(size));
  writeFile(src / "b.txt", "bee");

  Link link(TMP / "cacheA", TMP / "cacheB");
  link.a->send("10", selectionOf({src / "a.bin", src / "b.txt"}));
  link.run();

  for (const auto *seen : {&link.seenA, &link.seenB}) {
    QCOMPARE(seen->progress.size(), 7u); // one per chunk
    QVERIFY(std::is_sorted(seen->progress.begin(), seen->progress.end()));
    QCOMPARE(seen->progress.back(), static_cast<std::uint64_t>(size + 3));
  }
}

void FileChannelTests::copyOfOnlyFolders()
{
  const auto src = TMP / "src";
  fs::create_directories(src / "empty" / "inner");

  Link link(TMP / "cacheA", TMP / "cacheB");
  link.a->send("0f", selectionOf({src / "empty"}));
  link.run();
  QCOMPARE(link.seenA.sent.size(), 1u);
  QCOMPARE(link.seenB.received.size(), 1u);
  QVERIFY(fs::is_directory(TMP / "cacheB" / "0f" / "empty" / "inner"));
}

void FileChannelTests::receiverRefuses()
{
  const auto src = TMP / "src";
  fs::create_directories(src);
  writeFile(src / "a.txt", "a");

  Link link(TMP / "cacheA", TMP / "cacheB");
  link.refusal = "clipboard sharing is off";
  link.a->send("01", selectionOf({src / "a.txt"}));
  link.run();

  QCOMPARE(link.seenA.sendFailed.size(), 1u);
  QVERIFY(contains(link.seenA.sendFailed[0].second, "clipboard sharing is off"));
  QVERIFY(link.seenB.received.empty());
  QVERIFY(link.seenB.receiveFailed.empty()); // its own decision, not news
  QVERIFY(cacheEntries(TMP / "cacheB").empty());
}

void FileChannelTests::damageStopsBothSides()
{
  const auto src = TMP / "src";
  fs::create_directories(src);
  writeFile(src / "a.bin", randomBytes(FileChannel::kChunkBytes * 3));

  Link link(TMP / "cacheA", TMP / "cacheB");
  link.tamper = [](Message &m) {
    if (m.kind == Message::Chunk && m.index == 0 && m.data.size() > 100)
      m.data[100] ^= 0x20;
  };
  link.a->send("02", selectionOf({src / "a.bin"}));
  link.run();

  QCOMPARE(link.seenB.receiveFailed.size(), 1u);
  QVERIFY(contains(link.seenB.receiveFailed[0].second, "damaged"));
  QCOMPARE(link.seenA.sendFailed.size(), 1u);
  QVERIFY(contains(link.seenA.sendFailed[0].second, "damaged"));
  QVERIFY(link.seenA.sent.empty());
  QVERIFY(cacheEntries(TMP / "cacheB").empty());
}

void FileChannelTests::senderCancels()
{
  const auto src = TMP / "src";
  fs::create_directories(src);
  writeFile(src / "a.bin", randomBytes(FileChannel::kChunkBytes * 20));

  Link link(TMP / "cacheA", TMP / "cacheB");
  link.a->send("03", selectionOf({src / "a.bin"}));
  for (int i = 0; i < 4; ++i)
    link.stepAtoB();
  QCOMPARE(link.b->receivingId(), std::optional<std::string>("03"));

  link.a->cancelSend("a newer copy replaces it");
  QVERIFY(!link.a->sendingId());
  link.run();

  QVERIFY(link.seenA.sendFailed.empty()); // A decided it
  QCOMPARE(link.seenB.receiveFailed.size(), 1u);
  QVERIFY(contains(link.seenB.receiveFailed[0].second, "a newer copy replaces it"));
  QVERIFY(!link.b->receivingId());
  QVERIFY(cacheEntries(TMP / "cacheB").empty());
}

void FileChannelTests::receiverCancels()
{
  const auto src = TMP / "src";
  fs::create_directories(src);
  writeFile(src / "a.bin", randomBytes(FileChannel::kChunkBytes * 20));

  Link link(TMP / "cacheA", TMP / "cacheB");
  link.a->send("04", selectionOf({src / "a.bin"}));
  for (int i = 0; i < 4; ++i)
    link.stepAtoB();

  link.b->cancelReceive("copied something here");
  QVERIFY(cacheEntries(TMP / "cacheB").empty());
  link.run();

  QCOMPARE(link.seenA.sendFailed.size(), 1u);
  QVERIFY(contains(link.seenA.sendFailed[0].second, "copied something here"));
  QVERIFY(link.seenB.receiveFailed.empty());
  QVERIFY(link.seenB.received.empty());
  QVERIFY(cacheEntries(TMP / "cacheB").empty());
}

void FileChannelTests::newerCopyReplacesOlder()
{
  const auto src = TMP / "src";
  fs::create_directories(src);
  writeFile(src / "old.bin", randomBytes(FileChannel::kChunkBytes * 20));
  writeFile(src / "new.txt", "the newer copy");

  Link link(TMP / "cacheA", TMP / "cacheB");
  link.a->send("05", selectionOf({src / "old.bin"}));
  link.stepAtoB();
  link.stepAtoB();
  link.stepBtoA();
  // the old copy's chunks are still queued when the new one starts
  link.a->send("06", selectionOf({src / "new.txt"}));
  link.run();

  QCOMPARE(link.seenB.received.size(), 1u);
  QCOMPARE(link.seenB.received[0].first, std::string("06"));
  QCOMPARE(link.seenA.sent, std::vector<std::string>{"06"});
  QVERIFY(link.seenA.sendFailed.empty());
  QCOMPARE(cacheEntries(TMP / "cacheB"), std::vector<std::string>{"06"});
  QCOMPARE(readFile(TMP / "cacheB" / "06" / "new.txt"), std::string("the newer copy"));
}

void FileChannelTests::unknownCopyIgnored()
{
  Link link(TMP / "cacheA", TMP / "cacheB");
  link.b->onChunk("ffff", 0, "data");
  link.b->onDone("ffff", 0, std::string(32, '\0'));
  link.b->onCancel("ffff", "whatever");
  link.a->onAck("ffff");
  QVERIFY(link.toA.empty());
  QVERIFY(link.toB.empty());
  QVERIFY(link.seenA.sendFailed.empty());
  QVERIFY(link.seenB.receiveFailed.empty());
}

void FileChannelTests::tooLargeForTheOtherComputer()
{
  const auto src = TMP / "src";
  fs::create_directories(src);
  writeFile(src / "a.bin", std::string(2 * 1024 * 1024, 'x'));

  Link link(TMP / "cacheA", TMP / "cacheB");
  link.a->setPeerMaxBytes(1024 * 1024);
  link.a->send("07", selectionOf({src / "a.bin"}));
  QVERIFY(link.toB.empty()); // nothing sent at all
  QCOMPARE(link.seenA.sendFailed.size(), 1u);
  QVERIFY(contains(link.seenA.sendFailed[0].second, "larger than the 1 MB"));
}

void FileChannelTests::sourceGoneStopsTheCopy()
{
  const auto src = TMP / "src";
  fs::create_directories(src);
  writeFile(src / "a.txt", "first");
  writeFile(src / "b.txt", "second");
  auto selection = selectionOf({src / "a.txt", src / "b.txt"});
  fs::remove(src / "b.txt"); // deleted after it was copied

  Link link(TMP / "cacheA", TMP / "cacheB");
  link.a->send("08", std::move(selection));
  link.run();

  QCOMPARE(link.seenA.sendFailed.size(), 1u);
  QVERIFY(contains(link.seenA.sendFailed[0].second, "b.txt"));
  QCOMPARE(link.seenB.receiveFailed.size(), 1u);
  QVERIFY(cacheEntries(TMP / "cacheB").empty());
}

void FileChannelTests::malformedMessagesStopTheCopy()
{
  {
    Link link(TMP / "cacheA", TMP / "cacheB");
    link.b->onList("09", "not a list");
    QCOMPARE(link.toA.size(), 1u);
    QCOMPARE(link.toA[0].kind, Message::Cancel);
    QCOMPARE(link.seenB.receiveFailed.size(), 1u);
  }
  {
    // a path from the other computer that would escape the cache
    Link link(TMP / "cacheA", TMP / "cacheB");
    link.b->onList("0a", serialize({{Entry{"../evil.txt", false, 1, 0}}}));
    QCOMPARE(link.toA.size(), 1u);
    QVERIFY(contains(link.toA[0].data, "relative reference"));
    QVERIFY(!fs::exists(TMP / "evil.txt"));
  }
  {
    // an id that isn't one
    Link link(TMP / "cacheA", TMP / "cacheB");
    link.b->onList("../x", serialize({{Entry{"a.txt", false, 1, 0}}}));
    QCOMPARE(link.seenB.receiveFailed.size(), 1u);
    QVERIFY(contains(link.seenB.receiveFailed[0].second, "invalid transfer id"));
  }
  {
    // a digest of the wrong size, an empty chunk
    const auto src = TMP / "src";
    fs::create_directories(src);
    writeFile(src / "a.txt", "abc");
    for (const auto &bad : {Message{Message::Done, "0b", 0, "short"}, Message{Message::Chunk, "0b", 0, ""}}) {
      Link link(TMP / "cacheA", TMP / "cacheB");
      link.b->onList("0b", serialize(selectionOf({src / "a.txt"}).list));
      Link::deliver(*link.b, bad);
      QCOMPARE(link.seenB.receiveFailed.size(), 1u);
      QVERIFY(cacheEntries(TMP / "cacheB").empty());
    }
  }
}

void FileChannelTests::disconnectDeletesPartialCopy()
{
  const auto src = TMP / "src";
  fs::create_directories(src);
  writeFile(src / "a.bin", randomBytes(FileChannel::kChunkBytes * 20));

  Link link(TMP / "cacheA", TMP / "cacheB");
  link.a->send("0c", selectionOf({src / "a.bin"}));
  for (int i = 0; i < 4; ++i)
    link.stepAtoB();
  QCOMPARE(cacheEntries(TMP / "cacheB"), std::vector<std::string>{"0c.partial"});

  link.b.reset(); // the connection is gone
  QVERIFY(cacheEntries(TMP / "cacheB").empty());
}

void FileChannelTests::noUserNoCopy()
{
  const auto src = TMP / "src";
  fs::create_directories(src);
  writeFile(src / "a.txt", "a");

  Link link(TMP / "cacheA", TMP / "cacheB");
  link.noUser = true;
  link.a->send("0d", selectionOf({src / "a.txt"}));
  link.run();
  QCOMPARE(link.seenA.sendFailed.size(), 1u);
  QVERIFY(contains(link.seenA.sendFailed[0].second, "no user is logged on"));
}

//
// the protocol's messages
//

namespace {

//! What is written can be read back
class LoopStream : public deskflow::IStream
{
public:
  void close() override
  {
  }
  uint32_t read(void *buffer, uint32_t n) override
  {
    const auto take = (std::min)(static_cast<std::size_t>(n), m_data.size());
    if (buffer != nullptr)
      std::memcpy(buffer, m_data.data(), take);
    m_data.erase(0, take);
    return static_cast<uint32_t>(take);
  }
  void write(const void *buffer, uint32_t n) override
  {
    m_data.append(static_cast<const char *>(buffer), n);
  }
  void flush() override
  {
  }
  void shutdownInput() override
  {
  }
  void shutdownOutput() override
  {
  }
  void *getEventTarget() const override
  {
    return const_cast<LoopStream *>(this);
  }
  bool isReady() const override
  {
    return !m_data.empty();
  }
  uint32_t getSize() const override
  {
    return static_cast<uint32_t>(m_data.size());
  }

  std::string code()
  {
    std::string code(4, '\0');
    read(code.data(), 4);
    return code;
  }

private:
  std::string m_data;
};

} // namespace

void FileChannelTests::messagesCarryAnyBytes()
{
  std::string bytes;
  for (int i = 0; i < 256; ++i)
    bytes += static_cast<char>(i);
  bytes += randomBytes(FileChannel::kChunkBytes - bytes.size());
  const std::string id = "0123456789abcdef";
  const std::string digest(32, '\0');
  const std::string list = serialize({{Entry{"a/\xC3\xB1.txt", false, 5, 1700000000}}});

  LoopStream stream;
  std::string readId;
  std::string readData;
  uint32_t index = 0;

  ProtocolUtil::writef(&stream, kMsgDFileList, &id, &list);
  QCOMPARE(stream.code(), std::string("DFLS"));
  QVERIFY(ProtocolUtil::readf(&stream, kMsgDFileList + 4, &readId, &readData));
  QCOMPARE(readId, id);
  QCOMPARE(readData, list);

  ProtocolUtil::writef(&stream, kMsgDFileChunk, &id, 0xFFFFFFFEu, &bytes);
  QCOMPARE(stream.code(), std::string("DFCH"));
  QVERIFY(ProtocolUtil::readf(&stream, kMsgDFileChunk + 4, &readId, &index, &readData));
  QCOMPARE(index, 0xFFFFFFFEu);
  QCOMPARE(readData, bytes);

  ProtocolUtil::writef(&stream, kMsgDFileDone, &id, 7u, &digest);
  QCOMPARE(stream.code(), std::string("DFDN"));
  QVERIFY(ProtocolUtil::readf(&stream, kMsgDFileDone + 4, &readId, &index, &readData));
  QCOMPARE(index, 7u);
  QCOMPARE(readData, digest);

  ProtocolUtil::writef(&stream, kMsgDFileAck, &id);
  QCOMPARE(stream.code(), std::string("DFAK"));
  QVERIFY(ProtocolUtil::readf(&stream, kMsgDFileAck + 4, &readId));
  QCOMPARE(readId, id);

  const std::string reason = "a newer copy replaces it";
  ProtocolUtil::writef(&stream, kMsgDFileCancel, &id, &reason);
  QCOMPARE(stream.code(), std::string("DFCN"));
  QVERIFY(ProtocolUtil::readf(&stream, kMsgDFileCancel + 4, &readId, &readData));
  QCOMPARE(readData, reason);

  ProtocolUtil::writef(&stream, kMsgCFileClipboard, 500u);
  QCOMPARE(stream.code(), std::string("CFCL"));
  QVERIFY(ProtocolUtil::readf(&stream, kMsgCFileClipboard + 4, &index));
  QCOMPARE(index, 500u);
  QVERIFY(!stream.isReady());

  // the largest list fits in a message
  QVERIFY(FileChannel::kMaxListBytes < PROTOCOL_MAX_STRING_LENGTH);
}

//
// what the GUI is told
//

void FileChannelTests::activityJsonSummarizesTheCopy()
{
  const FileList list{
      {Entry{"fotos", true, 0, 0}, Entry{"fotos/a.jpg", false, 1000, 0}, Entry{"fotos/b.jpg", false, 2000, 0},
       Entry{"informe \"final\".pdf", false, 500, 0}, Entry{"c.txt", false, 1, 0}, Entry{"d.txt", false, 1, 0}}
  };
  const std::string received =
      "{\"event\":\"received\",\"computer\":\"laptop\",\"dropped\":[],\"formats\":[\"files\"],\"size\":3502,"
      "\"files\":{\"count\":5,\"folders\":1,\"bytes\":3502,"
      "\"names\":[\"fotos\",\"informe \\\"final\\\".pdf\",\"c.txt\"],\"roots\":4}}";
  QCOMPARE(filesActivityJson({.event = "received", .computer = "laptop", .list = &list}), received);

  const std::string failed = "{\"event\":\"failed\",\"reason\":\"noUser\","
                             "\"detail\":\"the other computer stopped it: no user is logged on to receive the files\","
                             "\"dropped\":[],\"formats\":[\"files\"]}";
  const FilesActivity noUser{
      .event = "failed", .detail = "the other computer stopped it: no user is logged on to receive the files"
  };
  QCOMPARE(filesActivityJson(noUser), failed);
}

void FileChannelTests::reasonsBecomeCodes()
{
  QCOMPARE(reasonCode("the copy is larger than the limit of 500 MB"), std::string("tooLarge"));
  QCOMPARE(reasonCode("the copy is larger than the 10 MB the other computer accepts"), std::string("tooLarge"));
  QCOMPARE(reasonCode("too many files to send at once"), std::string("tooLarge"));
  QCOMPARE(reasonCode("a.txt: changed while it was being sent"), std::string("changed"));
  QCOMPARE(reasonCode("a.txt: arrived damaged (SHA-256 differs)"), std::string("damaged"));
  QCOMPARE(reasonCode("not enough free disk space"), std::string("diskFull"));
  QCOMPARE(reasonCode("a.txt: can't be written (is the disk full?)"), std::string("diskFull"));
  QCOMPARE(reasonCode("the other computer stopped it: clipboard sharing is off"), std::string("refused"));
  QCOMPARE(reasonCode("something else"), std::string());
}

void FileChannelTests::progressJson()
{
  const FileList list{{Entry{"a.bin", false, 100, 0}, Entry{"b.bin", false, 50, 0}}};
  const std::string sending = "{\"direction\":\"send\",\"computer\":\"laptop\",\"count\":2,\"bytes\":150,\"done\":60}";
  QCOMPARE(FileProgress::json("send", "laptop", list, 60), sending);
  const std::string receiving = "{\"direction\":\"receive\",\"count\":2,\"bytes\":150,\"done\":150}";
  QCOMPARE(FileProgress::json("receive", "", list, 999), receiving);
}

//
// FileHub
//

namespace {

//! What the hub asked for
struct HubLog
{
  std::vector<std::string> calls;
  std::set<std::string> receivers = {"c1", "c2"};
  bool localWorks = true;

  FileHub::Actions actions()
  {
    FileHub::Actions a;
    a.canReceive = [this](const std::string &c) { return receivers.contains(c); };
    a.send = [this](const std::string &to, const std::string &id, Selection) {
      calls.push_back("send " + to + " " + id);
    };
    a.cancelSend = [this](const std::string &to, const std::string &) { calls.push_back("cancelSend " + to); };
    a.cancelReceive = [this](const std::string &from, const std::string &) {
      calls.push_back("cancelReceive " + from);
    };
    a.setLocalFiles = [this](const std::vector<fs::path> &roots) {
      calls.push_back("local " + std::to_string(roots.size()));
      return localWorks;
    };
    return a;
  }

  std::vector<std::string> take()
  {
    return std::exchange(calls, {});
  }
};

} // namespace

void FileChannelTests::serverCopyGoesToEnteredClient()
{
  const auto src = TMP / "src";
  fs::create_directories(src);
  writeFile(src / "a.txt", "a");

  HubLog log;
  FileHub hub("server", log.actions());
  hub.grabbed("server");
  hub.localCopy(selectionOf({src / "a.txt"}));
  const auto id = hub.currentId().value();
  QVERIFY(log.take().empty()); // the cursor is on the server: it has them

  hub.entered("c1");
  QCOMPARE(log.take(), std::vector<std::string>{"send c1 " + id});
  hub.entered("c1"); // on its way already
  QVERIFY(log.take().empty());

  hub.sent("c1", id);
  hub.entered("server");
  hub.entered("c1");
  QVERIFY(log.take().empty()); // c1 has it
}

void FileChannelTests::sameServerCopyIsNotSentAgain()
{
  const auto src = TMP / "src";
  fs::create_directories(src);
  writeFile(src / "a.txt", "a");

  HubLog log;
  FileHub hub("server", log.actions());
  hub.grabbed("server");
  hub.localCopy(selectionOf({src / "a.txt"}));
  const auto id = hub.currentId();
  hub.entered("c1");
  hub.sent("c1", *id);
  log.take();

  // the server's clipboard is read again each time the cursor leaves it
  hub.entered("server");
  hub.localCopy(selectionOf({src / "a.txt"}));
  QCOMPARE(hub.currentId(), id);
  hub.entered("c1");
  QVERIFY(log.take().empty());

  // the file changed: a new copy
  writeFile(src / "a.txt", "changed, longer");
  hub.entered("server");
  hub.localCopy(selectionOf({src / "a.txt"}));
  QVERIFY(hub.currentId() != id);
  hub.entered("c1");
  QCOMPARE(log.take(), std::vector<std::string>{"send c1 " + *hub.currentId()});

  // a copy of something else
  hub.localCopy(std::nullopt);
  QVERIFY(!hub.currentId());
}

void FileChannelTests::clientCopyComesThroughTheServer()
{
  const auto cache = TMP / "cache" / "0123";
  fs::create_directories(cache);
  writeFile(cache / "a.txt", "from c1");

  HubLog log;
  FileHub hub("server", log.actions());
  hub.grabbed("c1");
  hub.entered("c1");
  QVERIFY(!hub.accept("c2", "0999")); // c2 didn't copy last
  QVERIFY(hub.accept("c1", "0123"));
  QCOMPARE(hub.receivingFrom(), std::optional<std::string>("c1"));

  hub.entered("server");
  hub.received("c1", "0123", {cache / "a.txt"});
  QCOMPARE(hub.currentId(), std::optional<std::string>("0123"));
  QVERIFY(!hub.receivingFrom());
  QCOMPARE(log.take(), std::vector<std::string>{"local 1"}); // on the server's clipboard

  hub.entered("c2");
  QCOMPARE(log.take(), std::vector<std::string>{"send c2 0123"});
  hub.entered("c1"); // c1 made it: nothing to send, and c2's keeps going
  QVERIFY(log.take().empty());
}

void FileChannelTests::newerCopyStopsTransfers()
{
  const auto src = TMP / "src";
  fs::create_directories(src);
  writeFile(src / "a.txt", "a");

  HubLog log;
  FileHub hub("server", log.actions());
  hub.grabbed("server");
  hub.localCopy(selectionOf({src / "a.txt"}));
  hub.entered("c1");
  log.take();

  hub.grabbed("c2");
  QCOMPARE(log.take(), std::vector<std::string>{"cancelSend c1"});
  QVERIFY(!hub.currentId());
  QVERIFY(!hub.sendingTo());

  QVERIFY(hub.accept("c2", "0aaa"));
  hub.grabbed("server");
  QCOMPARE(log.take(), std::vector<std::string>{"cancelReceive c2"});
  QVERIFY(!hub.receivingFrom());
}

void FileChannelTests::cursorMovingOnRedirectsTheTransfer()
{
  const auto src = TMP / "src";
  fs::create_directories(src);
  writeFile(src / "a.txt", "a");

  HubLog log;
  FileHub hub("server", log.actions());
  hub.grabbed("server");
  hub.localCopy(selectionOf({src / "a.txt"}));
  const auto id = *hub.currentId();

  hub.entered("c1");
  hub.entered("c2"); // passed through c1 on the way
  QCOMPARE(log.take(), (std::vector<std::string>{"send c1 " + id, "cancelSend c1", "send c2 " + id}));

  hub.sent("c2", id);
  hub.entered("c1"); // c1 still needs it
  QCOMPARE(log.take(), std::vector<std::string>{"send c1 " + id});
}

void FileChannelTests::clientsThatCantReceiveAreSkipped()
{
  const auto src = TMP / "src";
  fs::create_directories(src);
  writeFile(src / "a.txt", "a");

  HubLog log;
  log.receivers = {"c1"};
  FileHub hub("server", log.actions());
  hub.grabbed("server");
  hub.localCopy(selectionOf({src / "a.txt"}));
  hub.entered("classic");
  QVERIFY(log.take().empty());
  QVERIFY(!hub.sendingTo());
}

void FileChannelTests::failedCopyIsNotRetried()
{
  const auto src = TMP / "src";
  fs::create_directories(src);
  writeFile(src / "a.txt", "a");

  HubLog log;
  FileHub hub("server", log.actions());
  hub.grabbed("server");
  hub.localCopy(selectionOf({src / "a.txt"}));
  hub.entered("c1");
  hub.sendFailed("c1", *hub.currentId());
  log.take();
  hub.entered("server");
  hub.entered("c1");
  QVERIFY(log.take().empty());

  // putting files on the server's clipboard is tried each time it fails
  const auto cache = TMP / "cache" / "0b0b";
  fs::create_directories(cache);
  writeFile(cache / "b.txt", "b");
  log.localWorks = false;
  hub.grabbed("c1");
  QVERIFY(hub.accept("c1", "0b0b"));
  hub.received("c1", "0b0b", {cache / "b.txt"});
  hub.entered("server");
  log.localWorks = true;
  hub.entered("c2");
  hub.entered("server");
  QCOMPARE(log.take(), (std::vector<std::string>{"local 1", "send c2 0b0b", "local 1"}));
}

void FileChannelTests::lateCopyIsDropped()
{
  const auto cache = TMP / "cache" / "0c0c";
  fs::create_directories(cache);
  writeFile(cache / "a.txt", "a");

  HubLog log;
  FileHub hub("server", log.actions());
  hub.grabbed("c1");
  QVERIFY(hub.accept("c1", "0c0c"));
  hub.grabbed("c2");
  hub.received("c1", "0c0c", {cache / "a.txt"}); // finished just before the cancel arrived
  QVERIFY(!hub.currentId());
}

void FileChannelTests::disconnectForgetsTheClient()
{
  const auto src = TMP / "src";
  fs::create_directories(src);
  writeFile(src / "a.txt", "a");

  HubLog log;
  FileHub hub("server", log.actions());
  hub.grabbed("server");
  hub.localCopy(selectionOf({src / "a.txt"}));
  const auto id = *hub.currentId();
  hub.entered("c1");
  hub.sent("c1", id);
  hub.disconnected("c1");
  log.take();

  // back again: it needs the copy again
  hub.entered("server");
  hub.entered("c1");
  QCOMPARE(log.take(), std::vector<std::string>{"send c1 " + id});
  hub.disconnected("c1");
  QVERIFY(!hub.sendingTo());

  // a client that copied and left: nothing is coming from it
  hub.grabbed("c2");
  hub.disconnected("c2");
  QVERIFY(!hub.accept("c2", "0d0d"));
}

QTEST_MAIN(FileChannelTests)
