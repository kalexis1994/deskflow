/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 - 2026 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2021 Barrier Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "SecureUtilsTests.h"

#include "net/SecureUtils.h"

#include <QFile>
#include <QTemporaryDir>

using namespace deskflow;

namespace {
const auto kFingerprint = Fingerprint::fromHex("28FD0A988A0EA16CD7E86CA7EE584171CAB28E492594902526058DAF63ED2E30");
}

void SecureUtilsTests::checkHex()
{
  QCOMPARE(kFingerprint.size(), size_t(32));
  QCOMPARE(
      deskflow::formatSSLFingerprint(kFingerprint, true),
      "28:FD:0A:98:8A:0E:A1:6C:D7:E8:6C:A7:EE:58:41:71:CA:B2:8E:49:25:94:90:25:26:05:8D:AF:63:ED:2E:30"
  );
  QCOMPARE(
      deskflow::formatSSLFingerprint(kFingerprint, false),
      "28FD0A988A0EA16CD7E86CA7EE584171CAB28E492594902526058DAF63ED2E30"
  );
}

void SecureUtilsTests::checkArt()
{
  QCOMPARE(
      deskflow::generateFingerprintArt(kFingerprint), "╔═════════════════╗\n"
                                                      "║*X+. .           ║\n"
                                                      "║*oo +            ║\n"
                                                      "║ + =             ║\n"
                                                      "║  B  . .         ║\n"
                                                      "║.+... o S        ║\n"
                                                      "║E+ ++. .         ║\n"
                                                      "║B*++..  .        ║\n"
                                                      "║+o*o o .         ║\n"
                                                      "║+o*Bo .          ║\n"
                                                      "╚═════════════════╝"
  );
}

void SecureUtilsTests::createsAMissingCertificate()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const auto path = (dir.path() + QStringLiteral("/not yet/tls/deskflow.pem")).toStdString();

  QVERIFY(deskflow::ensurePemSelfSignedCert(path, 1024));
  QFile file(QString::fromStdString(path));
  QVERIFY(file.open(QIODevice::ReadOnly));
  const auto pem = file.readAll();
  file.close();
  QVERIFY(pem.contains("PRIVATE KEY-----"));
  QVERIFY(pem.contains("-----BEGIN CERTIFICATE-----"));

  // never replaced: the other computers trust its fingerprint
  QVERIFY(!deskflow::ensurePemSelfSignedCert(path, 1024));
  QVERIFY(file.open(QIODevice::ReadOnly));
  QCOMPARE(file.readAll(), pem);
}

void SecureUtilsTests::checkColumns()
{
  QCOMPARE(
      deskflow::formatSSLFingerprintColumns(kFingerprint), "28:FD:0A:98:8A:0E:A1:6C\n"
                                                           "D7:E8:6C:A7:EE:58:41:71\n"
                                                           "CA:B2:8E:49:25:94:90:25\n"
                                                           "26:05:8D:AF:63:ED:2E:30"
  );
  QCOMPARE(deskflow::formatSSLFingerprintColumns({}), "");
}

QTEST_MAIN(SecureUtilsTests)
