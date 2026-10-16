/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2012 Nick Bolton
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/MSWindowsDebugOutputter.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "arch/Utf8.h"

void MSWindowsDebugOutputter::open(const std::string &title)
{
  // do nothing
}

void MSWindowsDebugOutputter::close()
{
  // do nothing
}

bool MSWindowsDebugOutputter::write(LogLevel::Level level, const std::string &msg)
{
  std::wstring out = deskflow::utf8ToWide(msg) + L"\n";
  OutputDebugString(out.c_str());
  return true;
}

void MSWindowsDebugOutputter::flush()
{
  // do nothing
}
