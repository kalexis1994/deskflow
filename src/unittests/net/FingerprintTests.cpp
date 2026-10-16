/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "FingerprintTests.h"

#include "net/Fingerprint.h"

#include <QTest>

void FingerprintTests::test_isValid()
{
  Fingerprint f;

  QVERIFY(!f.isValid());

  // Always invalid without a type
  f.data = Fingerprint::Bytes(1, 0x23);
  QVERIFY(!f.isValid());

  // SHA1 Tests
  f.type = Fingerprint::Type::Sha1;

  // Invalid SHA1, no Data
  f.data.clear();
  QVERIFY(!f.isValid());

  // Invalid SHA1, 5 bytes
  f.data = Fingerprint::Bytes(5, 0x23);
  QVERIFY(!f.isValid());

  // Valid SHA1, 20 bytes
  f.data = Fingerprint::Bytes(20, 0x23);
  QVERIFY(f.isValid());

  // Invalid SHA1, 25 bytes
  f.data = Fingerprint::Bytes(25, 0x23);
  QVERIFY(!f.isValid());

  // SHA256 Tests
  f.type = Fingerprint::Type::Sha256;

  // Invalid SHA256, no Data
  f.data.clear();
  QVERIFY(!f.isValid());

  // Invalid SHA256, 16 bytes
  f.data = Fingerprint::Bytes(16, 0x23);
  QVERIFY(!f.isValid());

  // Valid SHA256, 32 bytes
  f.data = Fingerprint::Bytes(32, 0x23);
  QVERIFY(f.isValid());

  // Invalid SHA256, 50 bytes
  f.data = Fingerprint::Bytes(50, 0x23);
  QVERIFY(!f.isValid());
}

void FingerprintTests::test_toDbLine()
{
  Fingerprint f;

  // Invalid Fingerprints return empty string
  QVERIFY(f.toDbLine().empty());

  // Always invalid without a type
  f.data = Fingerprint::Bytes(20, 0x23);
  QVERIFY(f.toDbLine().empty());

  // Invalid SHA1, type w/o data
  f.type = Fingerprint::Type::Sha1;
  f.data.clear();
  QVERIFY(f.toDbLine().empty());

  // Valid Sha1
  f.data = Fingerprint::Bytes(20, 0x23);
  std::string expectedString = ("v2:sha1:2323232323232323232323232323232323232323");
  QCOMPARE(f.toDbLine(), expectedString);

  // Valid Sha256
  f.type = Fingerprint::Type::Sha256;
  f.data = Fingerprint::Bytes(32, 0x23);
  expectedString = ("v2:sha256:2323232323232323232323232323232323232323232323232323232323232323");
  QCOMPARE(f.toDbLine(), expectedString);
}

void FingerprintTests::test_fromDbLine()
{
  Fingerprint expected;

  // The Following are all invalid
  auto actual = Fingerprint::fromDbLine("");
  QCOMPARE(actual, expected);

  actual = Fingerprint::fromDbLine("abcd");
  QCOMPARE(actual, expected);

  actual = Fingerprint::fromDbLine("v1:algo:something");
  QCOMPARE(actual, expected);

  actual = Fingerprint::fromDbLine("v2:algo:something");
  expected.data = Fingerprint::fromHex("something");
  QCOMPARE(actual, expected);

  actual = Fingerprint::fromDbLine("v2:algo:01020304abc");
  expected.data = Fingerprint::fromHex("01020304abc");
  QCOMPARE(actual, expected);

  actual = Fingerprint::fromDbLine("v2:algo:01020304ZZ");
  expected.data = Fingerprint::fromHex("01020304ZZ");
  QCOMPARE(actual, expected);

  // Test V1 Only support Sha1
  expected.type = Fingerprint::Type::Sha1;
  expected.data = Fingerprint::Bytes(20, 0x23);
  actual = Fingerprint::fromDbLine("23:23:23:23:23:23:23:23:23:23:23:23:23:23:23:23:23:23:23:23");
  QCOMPARE(actual, expected);

  // V1 does not support SHA256
  expected.type = Fingerprint::Type::Sha256;
  expected.data = Fingerprint::Bytes(32, 0x23);
  actual = Fingerprint::fromDbLine(
      "23:23:23:23:23:23:23:23:23:23:23:23:23:23:23:23:23:23:23:23:23:23:23:23:23:23:23:23:23:23:23:23"
  );
  QCOMPARE_NE(actual, expected);

  // V2 SHA1 Test
  expected.type = Fingerprint::Type::Sha1;
  expected.data = Fingerprint::Bytes(20, 0x23);
  actual = Fingerprint::fromDbLine("v2:sha1:2323232323232323232323232323232323232323");
  QCOMPARE(actual, expected);

  // V2 SHA1 Invalid Input
  actual = Fingerprint::fromDbLine("v2:sha1:23232323232323232323232323232323232323");
  QCOMPARE_NE(actual, expected);

  // V2 SHA256 Test
  expected.type = Fingerprint::Type::Sha256;
  expected.data = Fingerprint::Bytes(32, 0x23);
  actual = Fingerprint::fromDbLine("v2:sha256:2323232323232323232323232323232323232323232323232323232323232323");
  QCOMPARE(actual, expected);

  // V2 SHA256 Invalid Input
  actual = Fingerprint::fromDbLine("v2:sha256:232323232323232323232323232323232323232323232323232323");
  QCOMPARE_NE(actual, expected);
}

void FingerprintTests::test_typeToString()
{
  Fingerprint expected;

  expected.type = Fingerprint::Type::Invalid;
  QCOMPARE(expected.type, Fingerprint::Type::Invalid);
  QCOMPARE(Fingerprint::typeToString(expected.type), "invalid");

  expected.type = Fingerprint::Type::Sha1;
  QCOMPARE(expected.type, Fingerprint::Type::Sha1);
  QCOMPARE(Fingerprint::typeToString(expected.type), "sha1");

  expected.type = Fingerprint::Type::Sha256;
  QCOMPARE(expected.type, Fingerprint::Type::Sha256);
  QCOMPARE(Fingerprint::typeToString(expected.type), "sha256");
}

void FingerprintTests::test_typeFromString()
{
  QCOMPARE(Fingerprint::Type::Sha1, Fingerprint::typeFromString("sha1"));
  QCOMPARE(Fingerprint::Type::Sha1, Fingerprint::typeFromString("SHA1"));
  QCOMPARE(Fingerprint::Type::Sha256, Fingerprint::typeFromString("sha256"));
  QCOMPARE(Fingerprint::Type::Sha256, Fingerprint::typeFromString("SHA256"));

  QCOMPARE(Fingerprint::Type::Invalid, Fingerprint::typeFromString("invalid"));
  QCOMPARE(Fingerprint::Type::Invalid, Fingerprint::typeFromString(""));
  QCOMPARE(Fingerprint::Type::Invalid, Fingerprint::typeFromString("230p89jivon345"));
}

void FingerprintTests::test_fromHexMatchesQt()
{
  // the files were written and read with QByteArray::fromHex, which is lenient
  const char *samples[] = {"", "2323", "ABcd09", "01020304abc", "01020304ZZ", "something", " 23 23\r", "a:b:c"};
  for (const auto *hex : samples) {
    const auto qt = QByteArray::fromHex(hex);
    QVERIFY2(Fingerprint::fromHex(hex) == Fingerprint::Bytes(qt.begin(), qt.end()), hex);
  }
  QCOMPARE(Fingerprint::toHex({0xab, 0x01}), "ab01");
  QCOMPARE(Fingerprint::toHex({0xab, 0x01}, ':'), "ab:01");
}

QTEST_MAIN(FingerprintTests)
