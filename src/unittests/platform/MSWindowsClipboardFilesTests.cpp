/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "MSWindowsClipboardFilesTests.h"

#include "deskflow/Clipboard.h"
#include "platform/MSWindowsClipboardFiles.h"

#include <ShlObj.h>

#include <cstring>

namespace fs = std::filesystem;

namespace {

std::vector<fs::path> roundTrip(const std::vector<fs::path> &files)
{
  HGLOBAL data = MSWindowsClipboardFiles::makeDropFiles(files);
  if (data == nullptr)
    return {};
  auto result = MSWindowsClipboardFiles::readDropFiles(static_cast<HDROP>(data));
  GlobalFree(data);
  return result;
}

} // namespace

void MSWindowsClipboardFilesTests::pathsRoundTrip()
{
  const std::vector<fs::path> files = {
      L"C:\\Users\\me\\AppData\\Local\\Deskflow\\Clipboard\\0123456789abcdef\\report.pdf",
      L"C:\\with spaces\\and, commas\\file name.txt",
      L"C:\\\u00F1and\u00FA\\\u65E5\u672C\u8A9E\\\U0001F600.png",
      L"\\\\server\\share\\folder",
      fs::path(L"C:\\") / std::wstring(300, L'x') / L"longer than MAX_PATH.bin",
  };
  QCOMPARE(roundTrip(files), files);
}

void MSWindowsClipboardFilesTests::forwardSlashesBecomeBackslashes()
{
  // cache paths are joined with '/' from the network's relative paths
  const auto read = roundTrip({fs::path(L"C:\\cache\\0a") / fs::path(u8"docs/a.txt")});
  QCOMPARE(read.size(), 1u);
  QCOMPARE(read[0].native(), std::wstring(L"C:\\cache\\0a\\docs\\a.txt"));
}

void MSWindowsClipboardFilesTests::noFilesNoData()
{
  QVERIFY(MSWindowsClipboardFiles::makeDropFiles({}) == nullptr);
  QVERIFY(MSWindowsClipboardFiles::makeDropFiles({fs::path()}) == nullptr);
  QCOMPARE(roundTrip({fs::path(), L"C:\\a.txt"}), std::vector<fs::path>{L"C:\\a.txt"});
}

void MSWindowsClipboardFilesTests::readsAnsiData()
{
  // older programs put ANSI paths
  const char paths[] = "C:\\a.txt\0C:\\b c.txt\0";
  HGLOBAL data = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DROPFILES) + sizeof(paths) + 1);
  auto *bytes = static_cast<char *>(GlobalLock(data));
  auto *header = reinterpret_cast<DROPFILES *>(bytes);
  header->pFiles = sizeof(DROPFILES);
  header->fWide = FALSE;
  std::memcpy(bytes + sizeof(DROPFILES), paths, sizeof(paths));
  GlobalUnlock(data);

  const auto read = MSWindowsClipboardFiles::readDropFiles(static_cast<HDROP>(data));
  GlobalFree(data);
  QCOMPARE(read, (std::vector<fs::path>{L"C:\\a.txt", L"C:\\b c.txt"}));
}

void MSWindowsClipboardFilesTests::readsNothingFromNothing()
{
  QVERIFY(MSWindowsClipboardFiles::readDropFiles(nullptr).empty());
}

void MSWindowsClipboardFilesTests::dropEffectIsCopy()
{
  QVERIFY(MSWindowsClipboardFiles::dropEffectFormat() != 0);
  QCOMPARE(MSWindowsClipboardFiles::dropEffectFormat(), RegisterClipboardFormatW(L"Preferred DropEffect"));

  HGLOBAL data = MSWindowsClipboardFiles::makeDropEffect(DROPEFFECT_COPY);
  QVERIFY(data != nullptr);
  QVERIFY(GlobalSize(data) >= sizeof(DWORD));
  const auto *value = static_cast<const DWORD *>(GlobalLock(data));
  QCOMPARE(*value, static_cast<DWORD>(DROPEFFECT_COPY));
  GlobalUnlock(data);
  GlobalFree(data);
}

void MSWindowsClipboardFilesTests::packedPathsRoundTrip()
{
  const std::vector<fs::path> files = {
      L"C:\\a b\\c.txt",
      L"D:\\\u00F1and\u00FA\\\u65E5\u672C\u8A9E.mp3",
      L"\\\\server\\share\\folder",
  };
  QCOMPARE(MSWindowsClipboardFiles::unpackPaths(MSWindowsClipboardFiles::packPaths(files)), files);
  QVERIFY(MSWindowsClipboardFiles::packPaths({}).empty());
  QVERIFY(MSWindowsClipboardFiles::unpackPaths({}).empty());
}

void MSWindowsClipboardFilesTests::truncatedPathIsDropped()
{
  auto packed = MSWindowsClipboardFiles::packPaths({L"C:\\whole.txt", L"C:\\cut.txt"});
  packed.resize(packed.size() - 3 * sizeof(wchar_t));
  QCOMPARE(MSWindowsClipboardFiles::unpackPaths(packed), std::vector<fs::path>{L"C:\\whole.txt"});

  // half a character, as an odd number of bytes
  packed = MSWindowsClipboardFiles::packPaths({L"C:\\whole.txt"});
  packed.pop_back();
  QVERIFY(MSWindowsClipboardFiles::unpackPaths(packed).empty());
}

void MSWindowsClipboardFilesTests::packedClipboardRoundTrips()
{
  MSWindowsClipboardFiles::UserClipboard clipboard;
  clipboard.marshalled = std::string("\0\0\0\x01marshalled", 14);
  clipboard.files = {L"C:\\a.txt", L"D:\\b"};
  const auto back = MSWindowsClipboardFiles::unpackClipboard(MSWindowsClipboardFiles::packClipboard(clipboard));
  QVERIFY(back.has_value());
  QCOMPARE(back->marshalled, clipboard.marshalled);
  QCOMPARE(back->files, clipboard.files);

  // nothing copied: still an answer
  const auto empty = MSWindowsClipboardFiles::unpackClipboard(MSWindowsClipboardFiles::packClipboard({}));
  QVERIFY(empty.has_value());
  QVERIFY(empty->marshalled.empty());
  QVERIFY(empty->files.empty());
}

void MSWindowsClipboardFilesTests::cutShortClipboardIsNoAnswer()
{
  MSWindowsClipboardFiles::UserClipboard clipboard;
  clipboard.marshalled = std::string(100, 'm');
  auto packed = MSWindowsClipboardFiles::packClipboard(clipboard);
  packed.resize(50);
  QVERIFY(!MSWindowsClipboardFiles::unpackClipboard(packed).has_value());
  QVERIFY(!MSWindowsClipboardFiles::unpackClipboard("ab").has_value());
}

void MSWindowsClipboardFilesTests::readsWhatExplorerCopied()
{
  if (qEnvironmentVariableIsEmpty("DESKFLOW_TEST_REAL_CLIPBOARD"))
    QSKIP("reads the real clipboard: run by hand");

  const auto window = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
  QVERIFY(window != nullptr);
  const auto files = MSWindowsClipboardFiles::get(window);
  DestroyWindow(window);
  QVERIFY2(files.has_value(), "no files read from the clipboard");
  QVERIFY(!files->empty());
  for (const auto &file : *files)
    qInfo("%ls", file.c_str());
}

void MSWindowsClipboardFilesTests::readsThroughTheHelper()
{
  if (qEnvironmentVariableIsEmpty("DESKFLOW_TEST_REAL_CLIPBOARD") || qEnvironmentVariableIsEmpty("DESKFLOW_TEST_CORE"))
    QSKIP("reads the real clipboard: run by hand");

  const auto files =
      MSWindowsClipboardFiles::readThroughHelper(qEnvironmentVariable("DESKFLOW_TEST_CORE").toStdWString(), nullptr);
  QVERIFY2(files.has_value(), "the helper failed");
  QVERIFY2(!files->empty(), "no files read from the clipboard");
  for (const auto &file : *files)
    qInfo("%ls", file.c_str());
}

void MSWindowsClipboardFilesTests::readsTheWholeClipboardThroughTheHelper()
{
  if (qEnvironmentVariableIsEmpty("DESKFLOW_TEST_REAL_CLIPBOARD") || qEnvironmentVariableIsEmpty("DESKFLOW_TEST_CORE"))
    QSKIP("reads the real clipboard: run by hand");

  const auto clipboard = MSWindowsClipboardFiles::readClipboardThroughHelper(
      qEnvironmentVariable("DESKFLOW_TEST_CORE").toStdWString(), nullptr
  );
  QVERIFY2(clipboard.has_value(), "the helper failed");
  Clipboard read;
  read.unmarshall(clipboard->marshalled, 0);
  read.open(0);
  for (const auto format :
       {IClipboard::Format::Text, IClipboard::Format::HTML, IClipboard::Format::Bitmap, IClipboard::Format::PNG}) {
    if (read.has(format))
      qInfo("format %d: %zu bytes", static_cast<int>(format), read.get(format).size());
  }
  read.close();
  qInfo("%zu files", clipboard->files.size());
}

QTEST_MAIN(MSWindowsClipboardFilesTests)
