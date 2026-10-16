/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2014 - 2016 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "UnicodeTests.h"

#include "base/Endian.h"
#include "base/Unicode.h"

#include <QString>

void UnicodeTests::initTestCase()
{
  m_log.setFilter(LogLevel::Level::Verbose);
}

void UnicodeTests::UTF16ToUTF8()
{
  bool errors;
  auto result = Unicode::UTF16ToUTF8(std::string("h\0e\0l\0l\0o\0", 10), &errors);

  QVERIFY(!errors);
  QCOMPARE(result.c_str(), "hello");
}

void UnicodeTests::latin1MatchesQt()
{
  // the X11 text clipboard converted with QString before
  const std::string samples[] = {"plain", "Ñandú café", "日本 €", "\xff\xfe broken", "", std::string("a\0b", 3)};
  for (const auto &utf8 : samples) {
    const auto qt = QString::fromStdString(utf8).toLatin1().toStdString();
    QCOMPARE(Unicode::UTF8ToLatin1(utf8), qt);
    QCOMPARE(Unicode::Latin1ToUTF8(qt), QString::fromLatin1(qt).toUtf8().toStdString());
  }

  bool errors = false;
  QCOMPARE(Unicode::UTF8ToLatin1("caf\xc3\xa9", &errors), "caf\xe9");
  QVERIFY(!errors);
  QCOMPARE(Unicode::UTF8ToLatin1("\xe2\x82\xac", &errors), "?");
  QVERIFY(errors);
}

void UnicodeTests::completeUTF8Length()
{
  QCOMPARE(Unicode::completeUTF8Length(""), size_t(0));
  QCOMPARE(Unicode::completeUTF8Length("abc"), size_t(3));
  QCOMPARE(Unicode::completeUTF8Length("ab\xc3"), size_t(2));            // é cut after its first byte
  QCOMPARE(Unicode::completeUTF8Length("ab\xc3\xa9"), size_t(4));        // é complete
  QCOMPARE(Unicode::completeUTF8Length("a\xe2\x82"), size_t(1));         // € cut after two bytes
  QCOMPARE(Unicode::completeUTF8Length("a\xf0\x9f\x98"), size_t(1));     // emoji cut after three bytes
  QCOMPARE(Unicode::completeUTF8Length("a\xf0\x9f\x98\x80"), size_t(5)); // emoji complete
  QCOMPARE(Unicode::completeUTF8Length("a\x80"), size_t(2));             // stray continuation byte: nothing to wait for
}

void UnicodeTests::littleEndian()
{
  const unsigned char bytes[] = {0x28, 0x00, 0x00, 0x00, 0x34, 0x12};
  QCOMPARE(deskflow::fromLittleEndian<uint32_t>(bytes), 40u);
  QCOMPARE(deskflow::fromLittleEndian<uint16_t>(bytes + 4), uint16_t(0x1234));

  unsigned char out[4] = {};
  deskflow::toLittleEndian<uint32_t>(0x0A0B0C0D, out);
  QCOMPARE(out[0], static_cast<unsigned char>(0x0D));
  QCOMPARE(out[3], static_cast<unsigned char>(0x0A));
}

QTEST_MAIN(UnicodeTests)
