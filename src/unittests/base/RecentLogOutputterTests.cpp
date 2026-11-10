/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "RecentLogOutputterTests.h"

#include "base/RecentLogOutputter.h"

void RecentLogOutputterTests::keepsTheNewestLines()
{
  RecentLogOutputter log(3);
  for (const auto *line : {"one", "two", "three", "four\n", "five\r\n"})
    log.write(LogLevel::Level::Info, line);
  QCOMPARE(log.text(1024), std::string("three\nfour\nfive\n"));
}

void RecentLogOutputterTests::fitsInTheSizeAskedFor()
{
  RecentLogOutputter log;
  log.write(LogLevel::Level::Info, "old line");
  log.write(LogLevel::Level::Info, "newer");
  log.write(LogLevel::Level::Info, "newest");
  // "newer\nnewest\n" is 13 bytes; the old line doesn't fit as well
  QCOMPARE(log.text(13), std::string("newer\nnewest\n"));
  QCOMPARE(log.text(12), std::string("newest\n"));
  QCOMPARE(log.text(0), std::string());
}

void RecentLogOutputterTests::isTheOneTheProcessKeeps()
{
  {
    RecentLogOutputter log;
    QCOMPARE(RecentLogOutputter::instance(), &log);
  }
  QCOMPARE(RecentLogOutputter::instance(), static_cast<const RecentLogOutputter *>(nullptr));
}

QTEST_MAIN(RecentLogOutputterTests)
