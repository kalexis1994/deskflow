/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "MSWindowsImageTests.h"

#include "platform/MSWindowsClipboardPNGConverter.h"
#include "platform/MSWindowsImage.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <cstring>

using namespace deskflow::platform::image;

namespace {

struct Pixel
{
  unsigned char b, g, r;
};

//! A bitmap as the clipboard shares it, from rows given top first
std::string bitmap(int width, const std::vector<std::vector<Pixel>> &rows, int bits, bool bottomUp)
{
  const auto height = static_cast<int>(rows.size());
  BITMAPINFOHEADER header = {};
  header.biSize = sizeof(header);
  header.biWidth = width;
  header.biHeight = bottomUp ? height : -height;
  header.biPlanes = 1;
  header.biBitCount = static_cast<WORD>(bits);
  header.biCompression = BI_RGB;
  const auto stride = ((width * bits + 31) / 32) * 4;

  std::string data(reinterpret_cast<const char *>(&header), sizeof(header));
  for (int i = 0; i < height; ++i) {
    const auto &row = rows[bottomUp ? height - 1 - i : i];
    std::string bytes(stride, '\0');
    for (int x = 0; x < width; ++x) {
      auto *p = bytes.data() + x * (bits / 8);
      p[0] = static_cast<char>(row[x].b);
      p[1] = static_cast<char>(row[x].g);
      p[2] = static_cast<char>(row[x].r);
    }
    data += bytes;
  }
  return data;
}

//! The pixel at (x, y), counting y from the top, of a 32-bit bottom-up bitmap
Pixel pixelAt(const std::string &bitmap, int x, int y)
{
  BITMAPINFOHEADER header;
  std::memcpy(&header, bitmap.data(), sizeof(header));
  const auto row = header.biHeight - 1 - y;
  const auto *p = reinterpret_cast<const unsigned char *>(bitmap.data()) + header.biSize +
                  static_cast<std::size_t>(row) * header.biWidth * 4 + static_cast<std::size_t>(x) * 4;
  return {p[0], p[1], p[2]};
}

bool same(Pixel a, Pixel b)
{
  return a.b == b.b && a.g == b.g && a.r == b.r;
}

const std::vector<std::vector<Pixel>> kRows = {
    {{0, 0, 255}, {0, 255, 0}, {255, 0, 0}},    // red, green, blue
    {{255, 255, 255}, {0, 0, 0}, {10, 20, 30}}, // white, black, a dark one
};

// 2x1 RGBA: opaque red, then a fully transparent blue
const unsigned char kTransparentPng[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00,
    0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0xf4, 0x22, 0x7f, 0x8a, 0x00, 0x00, 0x00,
    0x0d, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8, 0xcf, 0xc0, 0x00, 0x46, 0x00, 0x0e, 0xfa, 0x02, 0xfe,
    0xf5, 0x1e, 0x4c, 0xa9, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

void checkRoundTrip(int bits, bool bottomUp)
{
  const auto png = bitmapToPng(bitmap(3, kRows, bits, bottomUp));
  QVERIFY(png.has_value());
  const auto back = pngToBitmap(*png);
  QVERIFY(back.has_value());

  BITMAPINFOHEADER header;
  std::memcpy(&header, back->data(), sizeof(header));
  QCOMPARE(header.biWidth, 3);
  QCOMPARE(header.biHeight, 2);
  QCOMPARE(header.biBitCount, WORD{32});
  QCOMPARE(back->size(), sizeof(header) + 3 * 2 * 4);
  for (int y = 0; y < 2; ++y) {
    for (int x = 0; x < 3; ++x)
      QVERIFY2(same(pixelAt(*back, x, y), kRows[y][x]), qPrintable(QStringLiteral("pixel %1,%2").arg(x).arg(y)));
  }
}

} // namespace

void MSWindowsImageTests::roundTrips32BitBottomUp()
{
  checkRoundTrip(32, true);
}

void MSWindowsImageTests::roundTrips24BitTopDown()
{
  checkRoundTrip(24, false);
}

void MSWindowsImageTests::makesARealPng()
{
  // a flat 1920×1080 screen: far smaller than its 8 MB bitmap
  const std::vector<std::vector<Pixel>> rows(1080, std::vector<Pixel>(1920, Pixel{200, 120, 40}));
  const auto source = bitmap(1920, rows, 32, true);
  const auto png = bitmapToPng(source);
  QVERIFY(png.has_value());
  QVERIFY(png->starts_with("\x89PNG\r\n\x1a\n"));
  QVERIFY(png->size() * 100 < source.size());
}

void MSWindowsImageTests::laysTransparencyOverWhite()
{
  const auto back =
      pngToBitmap(std::string_view(reinterpret_cast<const char *>(kTransparentPng), sizeof(kTransparentPng)));
  QVERIFY(back.has_value());
  QVERIFY(same(pixelAt(*back, 0, 0), {0, 0, 255}));
  QVERIFY(same(pixelAt(*back, 1, 0), {255, 255, 255}));
}

void MSWindowsImageTests::refusesWhatIsntAnImage()
{
  QVERIFY(!bitmapToPng("").has_value());
  QVERIFY(!bitmapToPng(std::string(40, 'x')).has_value());
  // the header promises more pixels than there are
  auto cut = bitmap(3, kRows, 32, true);
  cut.resize(cut.size() - 4);
  QVERIFY(!bitmapToPng(cut).has_value());

  QVERIFY(!pngToBitmap("").has_value());
  QVERIFY(!pngToBitmap("not a png at all").has_value());
}

void MSWindowsImageTests::trimsWhatFollowsThePng()
{
  const std::string png(reinterpret_cast<const char *>(kTransparentPng), sizeof(kTransparentPng));
  QCOMPARE(std::string(MSWindowsClipboardPNGConverter::trimmed(png + std::string(13, '\0'))), png);
  QCOMPARE(std::string(MSWindowsClipboardPNGConverter::trimmed(png)), png);
  QCOMPARE(std::string(MSWindowsClipboardPNGConverter::trimmed("no end")), std::string("no end"));
}

QTEST_MAIN(MSWindowsImageTests)
