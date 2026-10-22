/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ClipboardUtilTests.h"

#include "deskflow/Clipboard.h"
#include "deskflow/ClipboardUtil.h"

using Format = IClipboard::Format;
using namespace deskflow::clipboard;

namespace {

struct Content
{
  std::string text;
  std::string html;
  std::string bitmap;
  bool hasText = true;
  bool hasHtml = false;
  bool hasBitmap = false;
};

std::string marshalled(const Content &content)
{
  Clipboard clipboard;
  clipboard.open(0);
  clipboard.empty();
  if (content.hasText)
    clipboard.add(Format::Text, content.text);
  if (content.hasHtml)
    clipboard.add(Format::HTML, content.html);
  if (content.hasBitmap)
    clipboard.add(Format::Bitmap, content.bitmap);
  clipboard.close();
  return clipboard.marshall();
}

std::string text(const std::string &value)
{
  return marshalled({.text = value});
}

bool has(const std::string &data, Format format)
{
  Clipboard clipboard;
  clipboard.unmarshall(data, 0);
  clipboard.open(0);
  const bool result = clipboard.has(format);
  clipboard.close();
  return result;
}

} // namespace

void ClipboardUtilTests::hasContent()
{
  QVERIFY(deskflow::clipboard::hasContent(text("hello")));
  QVERIFY(!deskflow::clipboard::hasContent(text("")));
  QVERIFY(!deskflow::clipboard::hasContent(marshalled({.hasText = false})));

  // an empty clipboard, as the server holds before anyone copies
  Clipboard empty;
  empty.open(0);
  empty.empty();
  empty.close();
  QVERIFY(!deskflow::clipboard::hasContent(empty.marshall()));
}

void ClipboardUtilTests::sameContentIgnoresLineEndings()
{
  // deskflow/deskflow#10139: macOS and Windows passing multiline text back and forth
  QVERIFY(sameContent(text("one\ntwo\n"), text("one\r\ntwo\r\n")));
  QVERIFY(sameContent(text("one\ntwo"), text("one\ntwo\n\n")));
  QVERIFY(sameContent(text("same"), text("same")));
  QVERIFY(!sameContent(text("one\ntwo"), text("one two")));
  QVERIFY(!sameContent(text("hello"), text("Hello")));
}

void ClipboardUtilTests::sameContentComparesFormats()
{
  const auto withHtml = marshalled({.text = "a", .html = "<b>a</b>", .hasHtml = true});
  QVERIFY(!sameContent(text("a"), withHtml));
  QVERIFY(sameContent(withHtml, marshalled({.text = "a\r\n", .html = "<b>a</b>\r\n", .hasHtml = true})));

  const auto picture = marshalled({.bitmap = "BMP-1", .hasText = false, .hasBitmap = true});
  QVERIFY(sameContent(picture, picture));
  QVERIFY(!sameContent(picture, marshalled({.bitmap = "BMP-2", .hasText = false, .hasBitmap = true})));
}

void ClipboardUtilTests::fitKeepsEverythingUnderTheLimit()
{
  const auto data = marshalled({.text = "a", .html = "<b>a</b>", .bitmap = "BMP", .hasHtml = true, .hasBitmap = true});
  const auto fitted = fitToLimit(data, 1024);
  QVERIFY(fitted.fits);
  QVERIFY(fitted.dropped.empty());
  QCOMPARE(fitted.data, data);

  const auto unlimited = fitToLimit(data, 0);
  QVERIFY(unlimited.fits);
  QCOMPARE(unlimited.data, data);
}

void ClipboardUtilTests::fitDropsBitmapThenHtml()
{
  // a screenshot-sized picture with a little text: the text still goes
  const auto big = marshalled(
      {.text = "caption",
       .html = "<p>caption</p>",
       .bitmap = std::string(100000, 'x'),
       .hasHtml = true,
       .hasBitmap = true}
  );
  auto fitted = fitToLimit(big, 1024);
  QVERIFY(fitted.fits);
  QVERIFY(fitted.data.size() <= 1024);
  QVERIFY(fitted.dropped == std::vector<Format>{Format::Bitmap});
  QVERIFY(has(fitted.data, Format::Text));
  QVERIFY(has(fitted.data, Format::HTML));
  QVERIFY(!has(fitted.data, Format::Bitmap));

  // an Office copy whose HTML is large too: only the text goes
  const auto office = marshalled(
      {.text = "cell",
       .html = std::string(5000, 'h'),
       .bitmap = std::string(100000, 'x'),
       .hasHtml = true,
       .hasBitmap = true}
  );
  fitted = fitToLimit(office, 1024);
  QVERIFY(fitted.fits);
  QVERIFY((fitted.dropped == std::vector<Format>{Format::Bitmap, Format::HTML}));
  QVERIFY(has(fitted.data, Format::Text));
  QVERIFY(!has(fitted.data, Format::HTML));
}

void ClipboardUtilTests::fitGivesUpWhenTextIsTooBig()
{
  QVERIFY(!fitToLimit(text(std::string(5000, 't')), 1024).fits);

  // a picture alone can't shrink to anything: sending nothing would empty the others
  const auto picture = marshalled({.bitmap = std::string(5000, 'x'), .hasText = false, .hasBitmap = true});
  const auto fitted = fitToLimit(picture, 1024);
  QVERIFY(!fitted.fits);
  QVERIFY(fitted.dropped == std::vector<Format>{Format::Bitmap});
}

void ClipboardUtilTests::echoGuard()
{
  EchoGuard guard;
  QVERIFY(!guard.armed());

  Clipboard local;
  local.unmarshall(text("from the server\r\n"), 0);
  QVERIFY(!guard.isEcho(local)); // not armed: every change is real

  guard.applied(text("from the server\n"));
  QVERIFY(guard.armed());
  QVERIFY(guard.isEcho(local)); // the same text coming back, CRLF and all

  Clipboard copied;
  copied.unmarshall(text("something new"), 0);
  QVERIFY(!guard.isEcho(copied));

  guard.clear();
  QVERIFY(!guard.armed());
  QVERIFY(!guard.isEcho(local));
}

QTEST_MAIN(ClipboardUtilTests)
