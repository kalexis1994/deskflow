/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "MSWindowsClipboardFilesTests.h"

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

QTEST_MAIN(MSWindowsClipboardFilesTests)
