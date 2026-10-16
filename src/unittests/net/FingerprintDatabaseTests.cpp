/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 - 2026 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2021 Barrier Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "FingerprintDatabaseTests.h"

#include "net/Fingerprint.h"
#include "net/FingerprintDatabase.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <sstream>

void FingerprintDatabaseTests::readFile()
{
  std::istringstream stream(R"(
v2:sha1:01020304ab
v2:sha1:03040506ab
AB:CD:EF:00:01:02:03:04:05:06:07:08:09:10:11:12:13:14:15:16
)");

  FingerprintDatabase db;
  db.readStream(stream);

  // Only one will be in our list as only one is valid
  const std::vector<Fingerprint> expected = {
      {Fingerprint::Type::Sha1, Fingerprint::fromHex("ABCDEF0001020304050607080910111213141516")}
  };

  QVERIFY(db.fingerprints() == expected);
}

void FingerprintDatabaseTests::writeFile()
{
  std::ostringstream stream;

  FingerprintDatabase db;
  db.addTrusted({Fingerprint::Type::Sha1, Fingerprint::fromHex("ABCDEF0001020304050607080910111213141516")});
  db.addTrusted({Fingerprint::Type::Sha1, Fingerprint::fromHex("0001020304050607080910111213141516ABCDEF")});
  QVERIFY(db.writeStream(stream));

  QCOMPARE(stream.str(), R"(v2:sha1:abcdef0001020304050607080910111213141516
v2:sha1:0001020304050607080910111213141516abcdef
)");
}

void FingerprintDatabaseTests::fileRoundTrip()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  // a non-ASCII directory, like a user profile with an accented name
  const auto folder = dir.filePath(QStringLiteral("Señal"));
  QVERIFY(QDir().mkpath(folder));
  const auto path = QStringLiteral("%1/trusted-servers").arg(folder);

  const Fingerprint sha256 = {Fingerprint::Type::Sha256, Fingerprint::Bytes(32, 0xab)};
  FingerprintDatabase written;
  written.addTrusted(sha256);
  QVERIFY(written.write(path.toStdString()));

  // lines written on Windows by other tools may end in CRLF
  QFile file(path);
  QVERIFY(file.open(QIODevice::Append));
  file.write("v2:sha1:0001020304050607080910111213141516abcdef\r\n");
  file.close();

  FingerprintDatabase read;
  read.read(path.toStdString());
  QCOMPARE(read.fingerprints().size(), size_t(2));
  QVERIFY(read.isTrusted(sha256));
  QVERIFY(read.isTrusted({Fingerprint::Type::Sha1, Fingerprint::fromHex("0001020304050607080910111213141516abcdef")}));

  FingerprintDatabase missing;
  missing.read((folder + QStringLiteral("/does-not-exist")).toStdString());
  QVERIFY(missing.fingerprints().empty());
}

void FingerprintDatabaseTests::clear()
{
  FingerprintDatabase db;
  db.addTrusted({Fingerprint::Type::Sha1, Fingerprint::fromHex("01020304ab")});
  db.clear();

  QVERIFY(db.fingerprints().empty());
}

void FingerprintDatabaseTests::trusted()
{
  Fingerprint trusted1 = {Fingerprint::Type::Sha1, Fingerprint::fromHex("01020304ab")};
  Fingerprint trusted2 = {Fingerprint::Type::Sha1, Fingerprint::fromHex("03040506ab")};
  Fingerprint untrusted = {Fingerprint::Type::Sha1, Fingerprint::fromHex("01020304ac")};

  FingerprintDatabase db;

  db.addTrusted(trusted1);
  QVERIFY(db.isTrusted(trusted1));
  QVERIFY(!db.isTrusted(trusted2));
  QVERIFY(!db.isTrusted(untrusted));

  db.addTrusted(trusted2);
  QVERIFY(db.isTrusted(trusted1));
  QVERIFY(db.isTrusted(trusted2));
  QVERIFY(!db.isTrusted(untrusted));

  QCOMPARE(db.fingerprints().size(), size_t(2));
}

QTEST_MAIN(FingerprintDatabaseTests)
