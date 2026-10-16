/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "LogLevelTests.h"

#include <QFile>
#include <QSignalSpy>

#include "common/LogLevel.h"

void LogLevelTests::checkLogLevels_Valid()
{
  QCOMPARE(LogLevel::fromOption("Fatal"), LogLevel::Level::Fatal);
  QCOMPARE(LogLevel::fromOption("erRor"), LogLevel::Level::Error);
  QCOMPARE(LogLevel::fromOption("wArning"), LogLevel::Level::Warning);
  QCOMPARE(LogLevel::fromOption("info"), LogLevel::Level::Info);
  QCOMPARE(LogLevel::fromOption("deBug"), LogLevel::Level::Debug);
  QCOMPARE(LogLevel::fromOption("vERBOse"), LogLevel::Level::Verbose);
}

void LogLevelTests::checkLogLevels_Invalid()
{
  QCOMPARE(LogLevel::fromOption({}), LogLevel::Level::Info);
  QCOMPARE(LogLevel::fromOption("INVALID"), LogLevel::Level::Info);
  QCOMPARE(LogLevel::fromOption("deBug3"), LogLevel::Level::Info);
  QCOMPARE(LogLevel::fromOption("infomatic"), LogLevel::Level::Info);
  QCOMPARE(LogLevel::fromOption("warn"), LogLevel::Level::Info);
}

void LogLevelTests::toOption_namesAndBounds()
{
  QCOMPARE(LogLevel::toOption(LogLevel::Level::Info), std::string("INFO"));
  QCOMPARE(LogLevel::toOption(LogLevel::Level::Verbose), std::string("VERBOSE"));
  // one past the last level used to read past the end of the list
  QCOMPARE(LogLevel::toOption(6), std::string());
  QCOMPARE(LogLevel::toOption(LogLevel::Level::Print), std::string());
  QVERIFY(LogLevel::isOption("debug"));
  QVERIFY(!LogLevel::isOption("debugging"));
  QCOMPARE(LogLevel::logLevelOptions().size(), size_t(6));
}

QTEST_MAIN(LogLevelTests)
