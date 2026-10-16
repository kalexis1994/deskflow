/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 - 2026 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2002 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "base/LogOutputters.h"
#include "arch/Arch.h"

#include <iostream>

#include "arch/Utf8.h"

#include <filesystem>
#include <fstream>

constexpr auto s_logFileSizeLimit = 1024 * 1024; //!< Max Log size before rotating (1Mb)

//
// StopLogOutputter
//

void StopLogOutputter::open(const std::string &)
{
  // do nothing
}

void StopLogOutputter::close()
{
  // do nothing
}

bool StopLogOutputter::write(LogLevel::Level, const std::string &)
{
  return false;
}

//
// ConsoleLogOutputter
//

void ConsoleLogOutputter::open(const std::string &title)
{
  // do nothing
}

void ConsoleLogOutputter::close()
{
  // do nothing
}

bool ConsoleLogOutputter::write(LogLevel::Level level, const std::string &msg)
{
  using enum LogLevel::Level;
  if ((level >= Fatal) && (level <= Warning))
    std::cerr << msg << std::endl;
  else
    std::cout << msg << std::endl;
  std::cout.flush();
  return true;
}

void ConsoleLogOutputter::flush() const
{
  // do nothing
}

//
// SystemLogOutputter
//

void SystemLogOutputter::open(const std::string &title)
{
  ARCH->openLog(title);
}

void SystemLogOutputter::close()
{
  ARCH->closeLog();
}

bool SystemLogOutputter::write(LogLevel::Level level, const std::string &msg)
{
  ARCH->writeLog(level, msg);
  return true;
}

//
// SystemLogger
//

SystemLogger::SystemLogger(const std::string &title, bool blockConsole)
{
  // redirect log messages
  if (blockConsole) {
    m_stop = new StopLogOutputter; // NOSONAR - Adopted by `Log`
    CLOG->insert(m_stop);
  }
  m_syslog = new SystemLogOutputter; // NOSONAR - Adopted by `Log`
  m_syslog->open(title);
  CLOG->insert(m_syslog);
}

SystemLogger::~SystemLogger()
{
  CLOG->remove(m_syslog);
  delete m_syslog;
  if (m_stop != nullptr) {
    CLOG->remove(m_stop);
    delete m_stop;
  }
}

//
// FileLogOutputter
//

FileLogOutputter::FileLogOutputter(const std::string &logFile)
{
  setLogFilename(logFile);
}

void FileLogOutputter::setLogFilename(const std::string &logFile)
{
  m_fileName = logFile;
}

bool FileLogOutputter::write(LogLevel::Level, const std::string &message)
{
  const auto path = deskflow::utf8Path(m_fileName);
  {
    std::ofstream file(path, std::ios::binary | std::ios::app);
    if (!file)
      return false;
    file << message << '\n';
  }

  std::error_code ec;
  if (std::filesystem::file_size(path, ec) > s_logFileSizeLimit && !ec) {
    const auto oldFile = deskflow::utf8Path(m_fileName + ".1");
    std::filesystem::remove(oldFile, ec);
    std::filesystem::rename(path, oldFile, ec);
  }

  return true;
}

void FileLogOutputter::open(const std::string &title)
{
  // do nothing
}

void FileLogOutputter::close()
{
  // do nothing
}
