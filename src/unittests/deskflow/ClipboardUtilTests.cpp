/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ClipboardUtilTests.h"

#include "base/Endian.h"
#include "deskflow/Clipboard.h"
#include "deskflow/ClipboardUtil.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

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
  std::string png;
  bool hasPng = false;
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
  if (content.hasPng)
    clipboard.add(Format::PNG, content.png);
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

void ClipboardUtilTests::fitDropsTheBitmapBeforeThePng()
{
  // the same image as a bitmap and as a much smaller PNG: the PNG still goes
  const auto both = marshalled(
      {.text = "caption",
       .bitmap = std::string(100000, 'x'),
       .hasBitmap = true,
       .png = std::string(500, 'p'),
       .hasPng = true}
  );
  auto fitted = fitToLimit(both, 1024);
  QVERIFY(fitted.fits);
  QVERIFY(fitted.dropped == std::vector<Format>{Format::Bitmap});
  QVERIFY(has(fitted.data, Format::PNG));
  QVERIFY(has(fitted.data, Format::Text));

  // a PNG too big as well: the text goes alone, and both images are reported
  const auto big = marshalled(
      {.text = "caption",
       .bitmap = std::string(100000, 'x'),
       .hasBitmap = true,
       .png = std::string(5000, 'p'),
       .hasPng = true}
  );
  fitted = fitToLimit(big, 1024);
  QVERIFY(fitted.fits);
  QVERIFY((fitted.dropped == std::vector<Format>{Format::Bitmap, Format::PNG}));
  QVERIFY(!has(fitted.data, Format::PNG));

  // only a PNG, too big: nothing left to share
  const auto lonely = marshalled({.hasText = false, .png = std::string(5000, 'p'), .hasPng = true});
  fitted = fitToLimit(lonely, 1024);
  QVERIFY(!fitted.fits);
  QVERIFY(fitted.dropped == std::vector<Format>{Format::PNG});
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

namespace {

QJsonObject parsedJson(const std::string &json)
{
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(QByteArray::fromStdString(json), &error);
  if (error.error != QJsonParseError::NoError)
    qWarning("invalid json: %s (%s)", json.c_str(), qPrintable(error.errorString()));
  return document.object();
}

//! A DIB as the clipboard carries it: BITMAPINFOHEADER, then pixels
std::string dib(int32_t width, int32_t height)
{
  std::string data(40 + 16, '\0');
  deskflow::toLittleEndian<uint32_t>(40, data.data());
  deskflow::toLittleEndian<uint32_t>(static_cast<uint32_t>(width), data.data() + 4);
  deskflow::toLittleEndian<uint32_t>(static_cast<uint32_t>(height), data.data() + 8);
  return data;
}

} // namespace

void ClipboardUtilTests::activityJsonDescribesContent()
{
  const auto data = marshalled({.text = "line \"one\"\nline\ttwo", .bitmap = dib(1920, -1080), .hasBitmap = true});
  const auto json = parsedJson(
      activityJson({.event = "shared", .computer = "laptop", .dropped = {Format::HTML}, .marshalled = data}, true)
  );

  QCOMPARE(json["event"].toString(), QStringLiteral("shared"));
  QCOMPARE(json["computer"].toString(), QStringLiteral("laptop"));
  QCOMPARE(json["formats"].toArray(), QJsonArray({"text", "bitmap"}));
  QCOMPARE(json["dropped"].toArray(), QJsonArray({"HTML"}));
  QCOMPARE(json["size"].toInteger(), static_cast<qint64>(data.size()));
  QCOMPARE(json["text"].toString(), QStringLiteral("line \"one\"\nline\ttwo")); // escaped and back
  QCOMPARE(json["chars"].toInt(), 19);
  QCOMPARE(json["lines"].toInt(), 2);
  QCOMPARE(json["image"].toObject()["width"].toInt(), 1920);
  QCOMPARE(json["image"].toObject()["height"].toInt(), 1080); // top-down DIBs have a negative height

  // no content: just what happened
  const auto skipped = parsedJson(activityJson({.event = "skipped", .reason = "nothingToShare"}, true));
  QCOMPARE(skipped["reason"].toString(), QStringLiteral("nothingToShare"));
  QVERIFY(!skipped.contains("size"));
  QCOMPARE(skipped["formats"].toArray(), QJsonArray());
}

void ClipboardUtilTests::activityJsonReadsThePngSize()
{
  // the signature, then IHDR: its length, its type, then big-endian width and height
  std::string png = "\x89PNG\r\n\x1a\n";
  png += std::string("\0\0\0\x0d", 4) + "IHDR";
  png += std::string("\0\0\x0f\0", 4) + std::string("\0\0\x08\x70", 4); // 3840 × 2160
  png += std::string(20, '\0');
  const auto data = marshalled({.hasText = false, .png = png, .hasPng = true});
  const auto json = parsedJson(activityJson({.event = "shared", .marshalled = data}, true));
  QCOMPARE(json["formats"].toArray(), QJsonArray({"png"}));
  QCOMPARE(json["image"].toObject()["width"].toInt(), 3840);
  QCOMPARE(json["image"].toObject()["height"].toInt(), 2160);
}

void ClipboardUtilTests::activityJsonPreviewIsOptionalAndShort()
{
  const auto secret = marshalled({.text = "correct horse battery staple"});
  const auto hidden = parsedJson(activityJson({.event = "sent", .marshalled = secret}, false));
  QVERIFY(!hidden.contains("text"));
  QCOMPARE(hidden["chars"].toInt(), 28); // the size is still shown

  // cut on a character boundary, never inside a UTF-8 sequence
  std::string longText;
  for (int i = 0; i < 300; ++i)
    longText += "ñ";
  const auto json = parsedJson(activityJson({.event = "sent", .marshalled = marshalled({.text = longText})}, true));
  QCOMPARE(json["chars"].toInt(), 300);
  QCOMPARE(json["text"].toString().size(), static_cast<qsizetype>(kPreviewChars));
  QVERIFY(json["text"].toString().startsWith(QStringLiteral("ññ")));
}

QTEST_MAIN(ClipboardUtilTests)
