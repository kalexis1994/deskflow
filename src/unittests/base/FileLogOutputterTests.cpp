/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "FileLogOutputterTests.h"

#include <QFile>
#include <QFileInfo>
#include <QTest>

void FileLogOutputterTests::init()
{
  QVERIFY(m_dir.isValid());
  m_fileName = m_dir.filePath(QStringLiteral("deskflow.log"));
  QFile::remove(m_fileName);
  QFile::remove(rotatedFileName());
}

QString FileLogOutputterTests::rotatedFileName() const
{
  return QStringLiteral("%1.1").arg(m_fileName);
}

QString FileLogOutputterTests::readAll(const QString &fileName)
{
  QFile file(fileName);
  if (!file.open(QFile::ReadOnly))
    return {};
  return QString::fromUtf8(file.readAll());
}

void FileLogOutputterTests::growLogPastSizeLimit(FileLogOutputter &outputter, const QString &marker)
{
  const QString padding(s_paddingSize, QLatin1Char('x'));

  QVERIFY(outputter.write(LogLevel::Level::Info, marker.toStdString()));

  // rotating renames the live log away, so its absence is what ends the loop
  for (int i = 0; i < s_maxPaddingWrites && QFileInfo::exists(m_fileName); ++i)
    QVERIFY(outputter.write(LogLevel::Level::Info, padding.toStdString()));
}

void FileLogOutputterTests::write_multipleMessages_appendsEachLine()
{
  FileLogOutputter outputter(m_fileName.toStdString());

  QVERIFY(outputter.write(LogLevel::Level::Info, "first"));
  QVERIFY(outputter.write(LogLevel::Level::Info, "second"));

  QCOMPARE(readAll(m_fileName), QStringLiteral("first\nsecond\n"));
}

void FileLogOutputterTests::write_sizeLimitExceeded_preservesPreviousLog()
{
  FileLogOutputter outputter(m_fileName.toStdString());

  growLogPastSizeLimit(outputter, QStringLiteral("oldest entry"));

  QVERIFY2(QFileInfo::exists(rotatedFileName()), "log was discarded instead of rotated");
  QVERIFY(readAll(rotatedFileName()).startsWith(QStringLiteral("oldest entry\n")));

  QVERIFY(outputter.write(LogLevel::Level::Info, "after rotating"));
  QCOMPARE(readAll(m_fileName), QStringLiteral("after rotating\n"));
}

void FileLogOutputterTests::write_rotatedLogExists_replacesIt()
{
  FileLogOutputter outputter(m_fileName.toStdString());

  growLogPastSizeLimit(outputter, QStringLiteral("first batch"));
  QVERIFY(readAll(rotatedFileName()).startsWith(QStringLiteral("first batch\n")));

  growLogPastSizeLimit(outputter, QStringLiteral("second batch"));
  QVERIFY(readAll(rotatedFileName()).startsWith(QStringLiteral("second batch\n")));
}

void FileLogOutputterTests::write_nonAsciiPath_writesThatFile()
{
  // e.g. a Windows user name with accents; must not be read in the ANSI code page
  const auto path = m_dir.filePath(QStringLiteral("registro-ñandú-日本.log"));
  FileLogOutputter outputter(path.toStdString());

  QVERIFY(outputter.write(LogLevel::Level::Info, "hola"));
  QCOMPARE(readAll(path), QStringLiteral("hola\n"));
}

QTEST_MAIN(FileLogOutputterTests)
